#include "qvmediaformats.h"

#include <QMimeDatabase>
#include <QSet>

#if QT_VERSION >= QT_VERSION_CHECK(6, 0, 0)
#  include <QMediaFormat>
#else
#  include <QMediaPlayer>
#endif

QVMediaFormats::FormatList QVMediaFormats::supportedVideoFormats()
{
    QSet<QString> mimeTypes;
    QMimeDatabase mimeDatabase;

#if QT_VERSION >= QT_VERSION_CHECK(6, 0, 0)
    QMediaFormat mediaFormat;
    const auto fileFormats = mediaFormat.supportedFileFormats(QMediaFormat::Decode);
    for (const auto fileFormat : fileFormats) {
        QMediaFormat candidate(fileFormat);
        if (candidate.supportedVideoCodecs(QMediaFormat::Decode).isEmpty())
            continue;

        const QMimeType mimeType = candidate.mimeType();
        if (mimeType.isValid() && !mimeType.name().startsWith("audio/"))
            mimeTypes.insert(mimeType.name());
    }
#else
    const auto knownMimeTypes = mimeDatabase.allMimeTypes();
    for (const QMimeType &mimeType : knownMimeTypes) {
        if (mimeType.name().startsWith("video/")
            && QMediaPlayer::hasSupport(mimeType.name(), QStringList(), QMediaPlayer::VideoSurface)
                    != QMultimedia::NotSupported) {
            mimeTypes.insert(mimeType.name());
        }
    }
#endif

    QSet<QString> extensions;
    for (const QString &mimeTypeName : mimeTypes) {
        const QMimeType mimeType = mimeDatabase.mimeTypeForName(mimeTypeName);
        for (const QString &suffix : mimeType.suffixes())
            extensions.insert(QLatin1Char('.') + suffix.toLower());
    }

    FormatList result = { extensions.values(), mimeTypes.values() };
    result.extensions.sort(Qt::CaseInsensitive);
    result.mimeTypes.sort(Qt::CaseInsensitive);
    return result;
}
