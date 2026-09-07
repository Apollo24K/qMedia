#include <QtTest>

#include "qvmediacatalog.h"
#include "qvmediaformats.h"

#include <QDir>
#include <QFile>
#include <QSet>
#include <QTemporaryDir>

class MediaCatalogTests : public QObject
{
    Q_OBJECT

private slots:
    void scansAndClassifiesSupportedMedia();
    void sortsUsingRequestedMode();
    void tracksCurrentFileIndex();
    void discoversNormalizedVideoFormats();
    void galleryIncludesFoldersWithoutRecursing();
    void remembersFolderDescent();
    void preservesSortedNeighborsOnRescan();
};

void MediaCatalogTests::remembersFolderDescent()
{
    QVMediaCatalog::FolderHistory history;
    history.remember("/albums/set", "/albums/set/photo.png");
    history.remember("/albums", "/albums/set");
    QCOMPARE(history.childOf("/albums"), QString("/albums/set"));
    QVERIFY(history.takeChild("/elsewhere").isEmpty());
    QCOMPARE(history.takeChild("/albums"), QString("/albums/set"));
    QCOMPARE(history.takeChild("/albums/set"), QString("/albums/set/photo.png"));
    QVERIFY(history.takeChild("/albums/set").isEmpty());
    history.remember("/albums", "/albums/set");
    history.clear();
    QVERIFY(history.childOf("/albums").isEmpty());
}

static QString createFile(const QString &directory, const QString &name, const QByteArray &data = {})
{
    const QString path = directory + QLatin1Char('/') + name;
    QFile file(path);
    if (!file.open(QIODevice::WriteOnly)) {
        qWarning() << "Could not create test file" << path << file.errorString();
        return {};
    }
    file.write(data);
    file.close();
    return path;
}

static QVMediaCatalog::ScanOptions mixedMediaOptions()
{
    QVMediaCatalog::ScanOptions options;
    options.supportedMedia.append(
            { QVMediaCatalog::MediaType::Image, { ".jpg", ".png" }, { "image/jpeg" } });
    options.supportedMedia.append(
            { QVMediaCatalog::MediaType::Video, { ".mp4" }, { "video/mp4" } });
    return options;
}

void MediaCatalogTests::preservesSortedNeighborsOnRescan()
{
    QTemporaryDir directory;
    QVERIFY(!createFile(directory.path(), "photo10.png", "a").isEmpty());
    QVERIFY(!createFile(directory.path(), "photo2.png", "bbb").isEmpty());
    QVERIFY(!createFile(directory.path(), "photo1.png", "cc").isEmpty());
    QVERIFY(!createFile(directory.path(), "clip.mp4", "cc").isEmpty());
    for (int mode = 0; mode < 5; ++mode) {
        for (bool descending : {false, true}) {
            auto options = mixedMediaOptions();
            options.sortMode = mode;
            options.sortDescending = descending;
            QStringList expected;
            for (const auto &entry : QVMediaCatalog::scanGallery(directory.path(), options))
                if (!entry.isDirectory) expected.append(entry.file.absoluteFilePath);
            QVMediaCatalog catalog;
            for (const auto &path : expected) {
                catalog.setCurrentFile(QFileInfo(path));
                catalog.updateFolder(directory.path(), options);
                QStringList actual;
                for (const auto &file : catalog.state().folderFiles) actual.append(file.absoluteFilePath);
                QCOMPARE(actual, expected);
                QCOMPARE(catalog.state().currentIndexInFolder, expected.indexOf(path));
            }
        }
    }
    auto options = mixedMediaOptions();
    options.sortMode = 5;
    QList<QVMediaCatalog::MediaFile> shuffled;
    for (const auto &entry : QVMediaCatalog::scanGallery(directory.path(), options)) shuffled.append(entry.file);
    QVMediaCatalog catalog;
    catalog.setFolderOrder(directory.path(), shuffled, options);
    for (int attempt = 0; attempt < 3; ++attempt) {
        catalog.updateFolder(directory.path(), options);
        for (int i = 0; i < shuffled.size(); ++i)
            QCOMPARE(catalog.state().folderFiles[i].absoluteFilePath, shuffled[i].absoluteFilePath);
    }
}

void MediaCatalogTests::galleryIncludesFoldersWithoutRecursing()
{
    QTemporaryDir directory;
    QVERIFY(directory.isValid());
    QVERIFY(QDir(directory.path()).mkdir("Album10"));
    QVERIFY(QDir(directory.path()).mkdir("Album2"));
    QVERIFY(!createFile(directory.filePath("Album2"), "nested.jpg").isEmpty());
    QVERIFY(!createFile(directory.path(), "photo2.png").isEmpty());
    QVERIFY(!createFile(directory.path(), "photo10.png").isEmpty());
    QVERIFY(!createFile(directory.path(), "clip.mp4").isEmpty());
    QVERIFY(!createFile(directory.path(), "notes.txt").isEmpty());
    auto options = mixedMediaOptions();
    const auto entries = QVMediaCatalog::scanGallery(directory.path(), options);
    QCOMPARE(entries.size(), 5);
    QVERIFY(entries[0].isDirectory);
    QVERIFY(entries[1].isDirectory);
    QCOMPARE(entries[0].file.fileName, QString("Album2"));
    QCOMPARE(entries[1].file.fileName, QString("Album10"));
    QVERIFY(!entries[2].isDirectory);
    QCOMPARE(entries[2].file.mediaType, QVMediaCatalog::MediaType::Video);
    QCOMPARE(entries[3].file.fileName, QString("photo2.png"));
    QCOMPARE(entries[4].file.fileName, QString("photo10.png"));
    options.sortDescending = true;
    const auto reversed = QVMediaCatalog::scanGallery(directory.path(), options);
    QCOMPARE(reversed[0].file.fileName, QString("Album10"));
    QCOMPARE(reversed[2].file.fileName, QString("photo10.png"));
}

void MediaCatalogTests::scansAndClassifiesSupportedMedia()
{
    QTemporaryDir directory(QDir::currentPath() + "/mediacatalogtests-XXXXXX");
    QVERIFY(directory.isValid());
    QVERIFY(!createFile(directory.path(), "photo.JPG").isEmpty());
    QVERIFY(!createFile(directory.path(), "clip.mp4").isEmpty());
    QVERIFY(!createFile(directory.path(), "notes.txt").isEmpty());
    QVERIFY(!createFile(directory.path(), "._metadata.jpg").isEmpty());

    const auto files = QVMediaCatalog::scanFolder(directory.path(), mixedMediaOptions());
    QCOMPARE(files.size(), 2);

    QHash<QString, QVMediaCatalog::MediaType> types;
    for (const auto &file : files)
        types.insert(file.fileName, file.mediaType);

    QCOMPARE(types.value("photo.JPG"), QVMediaCatalog::MediaType::Image);
    QCOMPARE(types.value("clip.mp4"), QVMediaCatalog::MediaType::Video);

    const QFileInfo videoFile(directory.path() + "/clip.mp4");
    QCOMPARE(QVMediaCatalog::mediaTypeForFile(videoFile, mixedMediaOptions()),
             QVMediaCatalog::MediaType::Video);

    QVMediaCatalog catalog;
    catalog.setCurrentFile(videoFile, QVMediaCatalog::MediaType::Video);
    QCOMPARE(catalog.state().mediaType, QVMediaCatalog::MediaType::Video);
}

void MediaCatalogTests::sortsUsingRequestedMode()
{
    QList<QVMediaCatalog::MediaFile> files;
    files.append({ {}, "small.jpg", 0, 0, 10, {}, QVMediaCatalog::MediaType::Image });
    files.append({ {}, "large.jpg", 0, 0, 30, {}, QVMediaCatalog::MediaType::Image });
    files.append({ {}, "medium.jpg", 0, 0, 20, {}, QVMediaCatalog::MediaType::Image });

    QVMediaCatalog::sortFiles(files, 3, false);
    QCOMPARE(files.at(0).fileName, QString("large.jpg"));
    QCOMPARE(files.at(1).fileName, QString("medium.jpg"));
    QCOMPARE(files.at(2).fileName, QString("small.jpg"));

    QVMediaCatalog::sortFiles(files, 3, true);
    QCOMPARE(files.at(0).fileName, QString("small.jpg"));
    QCOMPARE(files.at(2).fileName, QString("large.jpg"));
}

void MediaCatalogTests::tracksCurrentFileIndex()
{
    QTemporaryDir directory(QDir::currentPath() + "/mediacatalogtests-XXXXXX");
    QVERIFY(directory.isValid());
    const QString firstPath = createFile(directory.path(), "image1.jpg");
    const QString secondPath = createFile(directory.path(), "image2.jpg");
    QVERIFY(!firstPath.isEmpty());
    QVERIFY(!secondPath.isEmpty());

    QVMediaCatalog catalog;
    auto options = mixedMediaOptions();
    catalog.updateFolder(directory.path(), options);
    catalog.setCurrentFile(QFileInfo(secondPath));

    QCOMPARE(catalog.state().folderFiles.size(), 2);
    QVERIFY(catalog.state().currentIndexInFolder >= 0);
    QCOMPARE(catalog.state().mediaType, QVMediaCatalog::MediaType::Image);
    QCOMPARE(catalog.state()
                     .folderFiles.at(catalog.state().currentIndexInFolder)
                     .absoluteFilePath,
             QFileInfo(secondPath).absoluteFilePath());

    catalog.state().isLoadRequested = true;
    catalog.clearCurrentFile();
    QVERIFY(catalog.state().fileInfo.filePath().isEmpty());
    QVERIFY(!catalog.state().isLoadRequested);
    QCOMPARE(catalog.state().folderFiles.size(), 2);
}

void MediaCatalogTests::discoversNormalizedVideoFormats()
{
    const auto formats = QVMediaFormats::supportedVideoFormats();
    QVERIFY(!formats.extensions.isEmpty());
    QVERIFY(!formats.mimeTypes.isEmpty());
    QVERIFY(formats.extensions.contains(".mp4"));
    QVERIFY(formats.extensions.contains(".mov"));
    QVERIFY(formats.extensions.contains(".flv"));
    QVERIFY(formats.extensions.contains(".vob"));
    QVERIFY(!formats.extensions.contains(".mp3"));

    QSet<QString> uniqueExtensions;
    for (const QString &extension : formats.extensions) {
        QVERIFY(extension.startsWith(QLatin1Char('.')));
        QCOMPARE(extension, extension.toLower());
        uniqueExtensions.insert(extension);
    }
    QCOMPARE(uniqueExtensions.size(), formats.extensions.size());

    for (const QString &mimeType : formats.mimeTypes)
        QVERIFY(!mimeType.startsWith("audio/"));
}

QTEST_GUILESS_MAIN(MediaCatalogTests)

#include "tst_mediacatalogtests.moc"
