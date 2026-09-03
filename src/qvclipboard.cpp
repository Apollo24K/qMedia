#include "qvclipboard.h"
#include "qvmediaformats.h"

#include <QBuffer>
#include <QDir>
#include <QFileInfo>
#include <QImageReader>
#include <QMimeData>
#include <QMimeDatabase>
#include <QRegularExpression>
#include <QTextDocumentFragment>

namespace {
QUrl mediaUrl(QString text)
{
    text = text.trimmed();
    if (text.startsWith('"') && text.endsWith('"'))
        text = text.mid(1, text.size() - 2);
    if (QDir::isAbsolutePath(text) && QFileInfo::exists(text))
        return QUrl::fromLocalFile(text);
    const QUrl url(text);
    const QString scheme = url.scheme().toLower();
    if (url.isValid() && (url.isLocalFile() || scheme == "http" || scheme == "https"
                         || scheme == "data"))
        return url;
    return {};
}

bool setSource(QVClipboard::Media &result, const QUrl &url)
{
    if (url.isEmpty())
        return false;
    if (url.scheme() != "data") {
        result.urls.append(url);
        return true;
    }
    const QByteArray encoded = url.toEncoded();
    const int comma = encoded.indexOf(',');
    if (comma < 0)
        return false;
    const QByteArray header = encoded.mid(5, comma - 5);
    const QString mime = QString::fromLatin1(header.split(';').first()).toLower();
    if (!mime.startsWith("image/") && !mime.startsWith("video/"))
        return false;
    QByteArray bytes = QByteArray::fromPercentEncoding(encoded.mid(comma + 1));
    if (header.toLower().endsWith(";base64"))
        bytes = QByteArray::fromBase64(bytes);
    if (bytes.isEmpty())
        return false;
    result.bytes = bytes;
    result.mimeType = mime;
    return true;
}
}

QVClipboard::Media QVClipboard::read(const QMimeData &data)
{
    Media result;
    result.image = qvariant_cast<QImage>(data.imageData());
    // File copies (including qMedia's own Copy) preserve the original animation.
    for (const QUrl &url : data.urls()) {
        if (url.isLocalFile())
            result.urls.append(url);
    }
    if (!result.urls.isEmpty())
        return result;

    // Prefer encoded originals to the clipboard's decoded, single-frame bitmap.
    QStringList formats = data.formats();
    for (const QString &preferred : { QString("image/gif"), QString("image/webp"),
                                      QString("image/apng") }) {
        if (formats.removeAll(preferred))
            formats.prepend(preferred);
    }
    Media encodedImage;
    for (const QString &format : formats) {
        QString mime = format.section(';', 0, 0).toLower();
        if (format == "PNG") mime = "image/png";
        if (format == "GIF") mime = "image/gif";
        if (format == "application/x-qt-windows-mime;value=\"PNG\"") mime = "image/png";
        if (format == "application/x-qt-windows-mime;value=\"GIF\"") mime = "image/gif";
        if (format == "application/x-qt-windows-mime;value=\"JFIF\"") mime = "image/jpeg";
        if ((!mime.startsWith("image/") && !mime.startsWith("video/"))
            || mime == "image/x-qt-image")
            continue;
        const QByteArray bytes = data.data(format);
        if (!bytes.isEmpty()) {
            result.bytes = bytes;
            result.mimeType = mime;
            if (mime.startsWith("video/") || mime == "image/gif"
                || mime == "image/webp" || mime == "image/apng")
                return result;
            if (encodedImage.bytes.isEmpty())
                encodedImage = result;
            result.bytes.clear();
            result.mimeType.clear();
        }
    }

    if (result.image.isNull() && !encodedImage.bytes.isEmpty())
        result.image = QImage::fromData(encodedImage.bytes);

    // Browsers often expose the original media in HTML alongside a still bitmap.
    const QRegularExpression source(
            R"html(<(?:img|video|source)\b[^>]*?\ssrc\s*=\s*(?:"([^"]*)"|'([^']*)'|([^\s>]+)))html",
            QRegularExpression::CaseInsensitiveOption);
    auto matches = source.globalMatch(data.html());
    while (matches.hasNext()) {
        const auto match = matches.next();
        QString value = match.captured(1);
        if (value.isEmpty()) value = match.captured(2);
        if (value.isEmpty()) value = match.captured(3);
        value = QTextDocumentFragment::fromHtml(value).toPlainText();
        if (setSource(result, mediaUrl(value)))
            return result;
    }
    for (const QUrl &url : data.urls()) {
        if (setSource(result, mediaUrl(url.toString())))
            return result;
    }
    if (setSource(result, mediaUrl(data.text())))
        return result;
    if (!encodedImage.bytes.isEmpty())
        return encodedImage;
    result.image = qvariant_cast<QImage>(data.imageData());
    return result;
}

QString QVClipboard::mediaSuffix(const QByteArray &prefix, const QString &mimeType, const QUrl &url)
{
    QBuffer buffer;
    buffer.setData(prefix);
    buffer.open(QIODevice::ReadOnly);
    const QByteArray imageFormat = QImageReader::imageFormat(&buffer);
    if (!imageFormat.isEmpty())
        return QString::fromLatin1(imageFormat);

    QMimeDatabase database;
    const auto detected = database.mimeTypeForData(prefix);
    // Never treat an HTML error/login page as media based on its URL suffix.
    if (detected.name() == "text/html" || detected.name() == "text/plain")
        return {};
    const auto videoFormats = QVMediaFormats::supportedVideoFormats();
    // Container signatures can be ambiguous (MP4 is also detected as QuickTime).
    // Prefer an explicit media MIME type after rejecting non-media responses.
    for (const QString &mime : { mimeType.section(';', 0, 0).trimmed().toLower(), detected.name() }) {
        if (videoFormats.mimeTypes.contains(mime)) {
            const QString suffix = database.mimeTypeForName(mime).preferredSuffix();
            if (videoFormats.extensions.contains("." + suffix))
                return suffix;
        }
    }
    const QString suffix = QFileInfo(url.path()).suffix().toLower();
    if (videoFormats.extensions.contains("." + suffix))
        return suffix;
    return {};
}
