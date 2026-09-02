#ifndef QVIMAGECORE_H
#define QVIMAGECORE_H

#include <QObject>
#include <QImageReader>
#include <QPixmap>
#include <QMovie>
#include <QFileInfo>
#include <QFutureWatcher>
#include <QTimer>
#include <QCache>
#include <QElapsedTimer>

#include "qvmediacatalog.h"

#if QT_VERSION >= QT_VERSION_CHECK(5, 14, 0)
#  include <QColorSpace>
#else
typedef QString QColorSpace;
#endif

class QVImageCore : public QObject
{
    Q_OBJECT

public:
    struct ErrorData
    {
        bool hasError = false;
        int errorNum = 0;
        QString errorString;
    };

    struct FileDetails
    {
        bool isPixmapLoaded = false;
        bool isMovieLoaded = false;
        QSize baseImageSize;
        QSize loadedPixmapSize;
        QElapsedTimer timeSinceLoaded;
        ErrorData errorData;
    };

    struct ReadData
    {
        QImage image;
        QString absoluteFilePath;
        qint64 fileSize;
        QSize imageSize;
        QColorSpace targetColorSpace;
        ErrorData errorData;
    };

    explicit QVImageCore(QObject *parent = nullptr);

    void loadFile(const QString &fileName, bool isReloading = false);
    void activateExternalMedia(const QString &fileName, QVMediaCatalog::MediaType mediaType);
    QVMediaCatalog::MediaType mediaTypeForFile(const QFileInfo &fileInfo) const;
    ReadData readFile(const QString &fileName, const QColorSpace &targetColorSpace);
    void loadPixmap(const ReadData &readData);
    void closeImage();
    void updateFolderInfo(QString dirPath = QString());
    void requestCaching();
    void requestCachingFile(const QString &filePath, const QColorSpace &targetColorSpace);
    void addToCache(const ReadData &&readImageAndFileInfo);
    static QString getPixmapCacheKey(const QString &absoluteFilePath, const qint64 &fileSize,
                                     const QColorSpace &targetColorSpace);
    QColorSpace getTargetColorSpace() const;
    QColorSpace detectDisplayColorSpace() const;
#if QT_VERSION >= QT_VERSION_CHECK(5, 14, 0) && QT_VERSION < QT_VERSION_CHECK(6, 7, 2)
    static bool removeTinyDataTagsFromIccProfile(QByteArray &profile);
#endif

    void settingsUpdated();

    void jumpToNextFrame();
    void setPaused(bool desiredState);
    void setSpeed(int desiredSpeed);

    void rotateImage(int rotation);
    QImage matchCurrentRotation(const QImage &imageToRotate);
    QPixmap matchCurrentRotation(const QPixmap &pixmapToRotate);

    QPixmap scaleExpensively(const int desiredWidth, const int desiredHeight);
    QPixmap scaleExpensively(const QSizeF desiredSize);

    // returned const reference is read-only
    const QPixmap &getLoadedPixmap() const { return loadedPixmap; }
    const QMovie &getLoadedMovie() const { return loadedMovie; }
    const QVMediaCatalog::State &getCurrentMedia() const { return mediaCatalog.state(); }
    const FileDetails &getImageDetails() const { return currentFileDetails; }
    bool isLoadInProgress() const { return waitingOnLoad; }
    int getCurrentRotation() const { return currentRotation; }

signals:
    void animatedFrameChanged(QRect rect);

    void updateLoadedPixmapItem();

    void fileChanged();

protected:
    void loadEmptyPixmap();
    FileDetails getEmptyFileDetails();

private:
    QVMediaCatalog::ScanOptions scanOptions() const;

    QPixmap loadedPixmap;
    QMovie loadedMovie;

    QVMediaCatalog mediaCatalog;
    FileDetails currentFileDetails;
    int currentRotation;

    QFutureWatcher<ReadData> loadFutureWatcher;

    int colorSpaceConversion;

    static QCache<QString, ReadData> imageCache;

    QStringList lastFilesPreloaded;
    QStringList preloadFilesInProgress;
    QString waitingOnPreloadFile;

    int largestDimension;

    bool waitingOnLoad;
};

#endif // QVIMAGECORE_H
