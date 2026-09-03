#include <QtTest>
#include <QBuffer>
#include <QMimeData>
#include <QTemporaryFile>
#include "qvclipboard.h"

class ClipboardTests : public QObject
{
    Q_OBJECT
private slots:
    void bitmap()
    {
        QMimeData data;
        QImage image(2, 2, QImage::Format_ARGB32);
        image.fill(Qt::red);
        data.setImageData(image);
        QCOMPARE(QVClipboard::read(data).image, image);
    }
    void originalFileWins()
    {
        QMimeData data;
        const QUrl file = QUrl::fromLocalFile(QDir::tempPath() + "/animation.gif");
        data.setUrls({ file });
        data.setText("https://example.test/page");
        data.setImageData(QImage(1, 1, QImage::Format_RGB32));
        QCOMPARE(QVClipboard::read(data).urls, QList<QUrl> { file });
    }
    void encodedAnimation()
    {
        const QByteArray gif = QByteArray::fromHex("47494638396101000100800000000000ffffff21f90400000000002c00000000010001000002024401003b");
        QMimeData data;
        data.setData("image/gif", gif);
        data.setImageData(QImage(1, 1, QImage::Format_RGB32));
        QCOMPARE(QVClipboard::read(data).bytes, gif);
        QCOMPARE(QVClipboard::mediaSuffix(gif, "image/gif"), QString("gif"));
    }
    void htmlOriginalWinsOverPng()
    {
        QMimeData data;
        data.setData("image/png", "flattened preview");
        data.setHtml("<a href='https://example.test/page'><img src=\"https://example.test/animated.gif?a=1&amp;b=2\"></a>");
        QCOMPARE(QVClipboard::read(data).urls.first(),
                 QUrl("https://example.test/animated.gif?a=1&b=2"));
    }
    void dataUrl()
    {
        QMimeData data;
        data.setText("data:video/mp4;base64,AAAAHGZ0eXBpc29t");
        const auto media = QVClipboard::read(data);
        QCOMPARE(media.mimeType, QString("video/mp4"));
        QCOMPARE(media.bytes, QByteArray::fromBase64("AAAAHGZ0eXBpc29t"));
    }
    void nativeGifAndRemoteFallback()
    {
        QMimeData native;
        native.setData("application/x-qt-windows-mime;value=\"GIF\"", "GIF89a");
        QCOMPARE(QVClipboard::read(native).mimeType, QString("image/gif"));

        QMimeData browser;
        QImage image(2, 2, QImage::Format_RGB32);
        image.fill(Qt::blue);
        browser.setImageData(image);
        browser.setText("https://example.test/animation.gif");
        const auto media = QVClipboard::read(browser);
        QCOMPARE(media.urls.first(), QUrl(browser.text()));
        QCOMPARE(media.image, image);
    }
    void localPath()
    {
        QTemporaryFile file;
        QVERIFY(file.open());
        QMimeData data;
        data.setText('"' + QDir::toNativeSeparators(file.fileName()) + '"');
        QCOMPARE(QVClipboard::read(data).urls.first().toLocalFile(), file.fileName());
    }
    void videoAndErrors()
    {
        const QByteArray mp4 = QByteArray::fromHex("000000186674797069736f6d0000000069736f6d6d703432");
        QCOMPARE(QVClipboard::mediaSuffix(mp4, "video/mp4"), QString("mp4"));
        QVERIFY(QVClipboard::mediaSuffix("<!DOCTYPE html><html>Login</html>",
                "video/mp4", QUrl("https://example.test/movie.mp4")).isEmpty());
        QMimeData data;
        data.setText("just some text");
        QVERIFY(QVClipboard::read(data).urls.isEmpty());
    }
};

QTEST_MAIN(ClipboardTests)
#include "tst_clipboardtests.moc"
