#include "qvimagecore.h"
#include "qvapplication.h"
#include "qvwin32functions.h"
#include "qvcocoafunctions.h"
#include "qvlinuxx11functions.h"
#include <cstring>
#include <QMessageBox>
#include <QUrl>
#include <QSettings>
#include <QtConcurrent/QtConcurrentRun>
#include <QIcon>
#include <QGuiApplication>
#include <QScreen>

QCache<QString, QVImageCore::ReadData> QVImageCore::imageCache;

QVImageCore::QVImageCore(QObject *parent) : QObject(parent)
{
// Set allocation limit to 8 GiB on Qt6
#if QT_VERSION >= QT_VERSION_CHECK(6, 0, 0)
    QImageReader::setAllocationLimit(8192);
#endif

    currentRotation = 0;

    connect(&loadedMovie, &QMovie::updated, this, &QVImageCore::animatedFrameChanged);

    connect(&loadFutureWatcher, &QFutureWatcher<ReadData>::finished, this,
            [this]() { loadPixmap(loadFutureWatcher.result()); });

    largestDimension = 0;
    const auto screenList = QGuiApplication::screens();
    for (auto const &screen : screenList) {
        int largerDimension;
        if (screen->size().height() > screen->size().width()) {
            largerDimension = screen->size().height();
        } else {
            largerDimension = screen->size().width();
        }

        if (largerDimension > largestDimension) {
            largestDimension = largerDimension;
        }
    }

    waitingOnLoad = false;

    // Connect to settings signal
    connect(&qvApp->getSettingsManager(), &SettingsManager::settingsUpdated, this,
            &QVImageCore::settingsUpdated);
    settingsUpdated();
}

void QVImageCore::loadFile(const QString &fileName, bool isReloading)
{
    if (waitingOnLoad) {
        return;
    }

    QString sanitaryFileName = fileName;

    // sanitize file name if necessary
    QUrl sanitaryUrl = QUrl(fileName);
    if (sanitaryUrl.isLocalFile())
        sanitaryFileName = sanitaryUrl.toLocalFile();

    QFileInfo fileInfo(sanitaryFileName);
    sanitaryFileName = fileInfo.absoluteFilePath();

    // Pause playing movie because it feels better that way
    setPaused(true);

    // Record the requested image before decoding starts. Without this transition,
    // an image requested after a video leaves the catalog marked as Video; the
    // completion handler then mistakes the decoded image for an obsolete result
    // and discards it.
    mediaCatalog.setCurrentFile(fileInfo, QVMediaCatalog::MediaType::Image);
    mediaCatalog.state().isLoadRequested = true;
    waitingOnLoad = true;

    QColorSpace targetColorSpace = getTargetColorSpace();
    QString cacheKey = getPixmapCacheKey(sanitaryFileName, fileInfo.size(), targetColorSpace);

    // check if cached already before loading the long way
    auto *cachedData = isReloading ? nullptr : QVImageCore::imageCache.take(cacheKey);
    if (cachedData != nullptr) {
        ReadData readData = *cachedData;
        delete cachedData;
        loadPixmap(readData);
    }
    // or see if the preloader is already working on it
    else if (preloadFilesInProgress.contains(sanitaryFileName)) {
        waitingOnPreloadFile = sanitaryFileName;
    } else {
#if QT_VERSION < QT_VERSION_CHECK(6, 0, 0)
        loadFutureWatcher.setFuture(QtConcurrent::run(this, &QVImageCore::readFile,
                                                      sanitaryFileName, targetColorSpace));
#else
        loadFutureWatcher.setFuture(QtConcurrent::run(&QVImageCore::readFile, this,
                                                      sanitaryFileName, targetColorSpace));
#endif
    }
}

void QVImageCore::activateExternalMedia(const QString &fileName,
                                        QVMediaCatalog::MediaType mediaType)
{
    setPaused(true);
    currentFileDetails = getEmptyFileDetails();
    mediaCatalog.state().isLoadRequested = true;
    mediaCatalog.setCurrentFile(QFileInfo(fileName), mediaType);
    if (mediaCatalog.state().currentIndexInFolder == -1)
        updateFolderInfo();
    loadEmptyPixmap();
}

QVMediaCatalog::MediaType QVImageCore::mediaTypeForFile(const QFileInfo &fileInfo) const
{
    return QVMediaCatalog::mediaTypeForFile(fileInfo, scanOptions());
}

QVImageCore::ReadData QVImageCore::readFile(const QString &fileName,
                                            const QColorSpace &targetColorSpace)
{
    QImageReader imageReader;
    imageReader.setAutoTransform(true);

    imageReader.setFileName(fileName);

    QImage readImage;
    if (imageReader.format() == "svg" || imageReader.format() == "svgz") {
        // Render vectors into a high resolution
        QIcon icon;
        icon.addFile(fileName);
        readImage = icon.pixmap(largestDimension).toImage();
        // If this fails, try reading the normal way so that a proper error message is given
        if (readImage.isNull())
            readImage = imageReader.read();
    } else {
        readImage = imageReader.read();
    }

#if QT_VERSION >= QT_VERSION_CHECK(5, 14, 0)
    readImage.convertTo(QImage::Format::Format_ARGB32_Premultiplied);
#else
    readImage = readImage.convertToFormat(QImage::Format::Format_ARGB32_Premultiplied);
#endif
    // Handle color space information

#if QT_VERSION >= QT_VERSION_CHECK(5, 14, 0) && QT_VERSION < QT_VERSION_CHECK(6, 7, 2)
    // Work around Qt ICC profile parsing bug
    if (!readImage.colorSpace().isValid() && !readImage.colorSpace().iccProfile().isEmpty()) {
        QByteArray profileData = readImage.colorSpace().iccProfile();
        if (removeTinyDataTagsFromIccProfile(profileData))
            readImage.setColorSpace(QColorSpace::fromIccProfile(profileData));
    }
#endif

#if QT_VERSION >= QT_VERSION_CHECK(5, 14, 0)
    // Assume image is sRGB if it doesn't specify
    if (!readImage.colorSpace().isValid())
        readImage.setColorSpace(QColorSpace::SRgb);

    // Convert image color space if we have a target that's different
    if (targetColorSpace.isValid() && readImage.colorSpace() != targetColorSpace)
        readImage.convertToColorSpace(targetColorSpace);
#endif

    QFileInfo fileInfo(fileName);

    ReadData readData = { readImage,        fileInfo.absoluteFilePath(),
                          fileInfo.size(),  imageReader.size(),
                          targetColorSpace, {} };

    if (readImage.isNull()) {
        readData.errorData = { true, imageReader.error(), imageReader.errorString() };
    }

    return readData;
}

void QVImageCore::loadPixmap(const ReadData &readData)
{
    // An image decode may finish after the user has switched to a video. Keep the
    // decoded image available for later navigation without replacing the active video.
    if (mediaCatalog.state().mediaType == QVMediaCatalog::MediaType::Video) {
        waitingOnLoad = false;
        addToCache(std::move(readData));
        return;
    }

    if (readData.errorData.hasError) {
        currentFileDetails = getEmptyFileDetails();
        currentFileDetails.errorData = readData.errorData;
        mediaCatalog.state().isLoadRequested = false;
    } else {
        currentFileDetails.errorData = {};
    }

    // Do this first so we can keep folder info even when loading errored files
    mediaCatalog.setCurrentFile(QFileInfo(readData.absoluteFilePath));
    if (mediaCatalog.state().currentIndexInFolder == -1)
        updateFolderInfo();

    // Reset mechanism to avoid stalling while loading
    waitingOnLoad = false;

    if (currentFileDetails.errorData.hasError) {
        loadEmptyPixmap();
        return;
    }

    loadedPixmap = QPixmap::fromImage(matchCurrentRotation(readData.image));

    // Set file details
    currentFileDetails.isPixmapLoaded = true;
    currentFileDetails.baseImageSize = readData.imageSize;
    currentFileDetails.loadedPixmapSize = loadedPixmap.size();
    if (currentFileDetails.baseImageSize == QSize(-1, -1)) {
        qInfo() << "QImageReader::size gave an invalid size for "
                        + mediaCatalog.state().fileInfo.fileName()
                        + ", using size from loaded pixmap";
        currentFileDetails.baseImageSize = currentFileDetails.loadedPixmapSize;
    }

    addToCache(std::move(readData));

    // Animation detection
    loadedMovie.setFormat("");
    loadedMovie.stop();
    loadedMovie.setFileName(mediaCatalog.state().fileInfo.absoluteFilePath());

    // APNG workaround
    if (loadedMovie.format() == "png") {
        loadedMovie.setFormat("apng");
        loadedMovie.setFileName(mediaCatalog.state().fileInfo.absoluteFilePath());
    }

    if (loadedMovie.isValid() && loadedMovie.frameCount() != 1)
        loadedMovie.start();

    currentFileDetails.isMovieLoaded = loadedMovie.state() == QMovie::Running;

    if (!currentFileDetails.isMovieLoaded)
        if (auto device = loadedMovie.device())
            device->close();

    currentFileDetails.timeSinceLoaded.start();

    emit fileChanged();

    requestCaching();
}

void QVImageCore::closeImage()
{
    currentFileDetails = getEmptyFileDetails();
    mediaCatalog.clearCurrentFile();
    loadEmptyPixmap();
}

void QVImageCore::loadEmptyPixmap()
{
    loadedPixmap = QPixmap();
    loadedMovie.stop();
    loadedMovie.setFileName("");

    emit fileChanged();
}

QVImageCore::FileDetails QVImageCore::getEmptyFileDetails()
{
    return { false,
             false,
             QSize(),
             QSize(),
             QElapsedTimer(),
             {} };
}

void QVImageCore::updateFolderInfo(QString dirPath)
{
    mediaCatalog.updateFolder(dirPath, scanOptions());
}

QVMediaCatalog::ScanOptions QVImageCore::scanOptions() const
{
    QVMediaCatalog::ScanOptions options;
    options.supportedMedia.append({ QVMediaCatalog::MediaType::Image,
                                    qvApp->getFileExtensionList(),
                                    qvApp->getMimeTypeNameList() });
    options.supportedMedia.append({ QVMediaCatalog::MediaType::Video,
                                    qvApp->getVideoExtensionList(),
                                    qvApp->getVideoMimeTypeNameList() });
    options.allowMimeContentDetection = qvGetSettingBool(AllowMimeContentDetection);
    options.includeHidden = !qvApp->getSettingsManager().getBool("skiphidden");
    options.sortMode = qvGetSettingInt(SortMode);
    options.sortDescending = qvGetSettingBool(SortDescending);
    return options;
}

void QVImageCore::requestCaching()
{
    int preloadingMode = qvGetSettingInt(PreloadingMode);
    if (preloadingMode == 0) {
        QVImageCore::imageCache.clear();
        return;
    }

    QColorSpace targetColorSpace = getTargetColorSpace();

    int preloadingDistance = 1;

    if (preloadingMode > 1)
        preloadingDistance = 4;

    QStringList filesToPreload;
    const auto &mediaState = mediaCatalog.state();
    for (int i = mediaState.currentIndexInFolder - preloadingDistance;
         i <= mediaState.currentIndexInFolder + preloadingDistance; i++) {
        int index = i;

        // Don't try to cache the currently loaded image
        if (index == mediaState.currentIndexInFolder)
            continue;

        // keep within index range
        if (qvGetSettingBool(LoopFoldersEnabled)) {
            if (index > mediaState.folderFiles.length() - 1)
                index = index - (mediaState.folderFiles.length());
            else if (index < 0)
                index = index + (mediaState.folderFiles.length());
        }

        // if still out of range after looping, just cancel the cache for this index
        if (index > mediaState.folderFiles.length() - 1 || index < 0
            || mediaState.folderFiles.isEmpty())
            continue;

        if (mediaState.folderFiles[index].mediaType != QVMediaCatalog::MediaType::Image)
            continue;

        QString filePath = mediaState.folderFiles[index].absoluteFilePath;
        filesToPreload.append(filePath);

        requestCachingFile(filePath, targetColorSpace);
    }
    lastFilesPreloaded = filesToPreload;
}

void QVImageCore::requestCachingFile(const QString &filePath, const QColorSpace &targetColorSpace)
{
    QFile imgFile(filePath);
    QString cacheKey = getPixmapCacheKey(filePath, imgFile.size(), targetColorSpace);

    // check if image is already loaded or requested
    if (QVImageCore::imageCache.contains(cacheKey) || lastFilesPreloaded.contains(filePath))
        return;

    if (imgFile.size() / 1024 > QVImageCore::imageCache.maxCost() / 2)
        return;

    preloadFilesInProgress.append(filePath);

    auto *cacheFutureWatcher = new QFutureWatcher<ReadData>();
    connect(cacheFutureWatcher, &QFutureWatcher<ReadData>::finished, this,
            [cacheFutureWatcher, this]() {
                const ReadData readData = cacheFutureWatcher->result();
                if (waitingOnPreloadFile == readData.absoluteFilePath) {
                    loadPixmap(readData);
                    waitingOnPreloadFile = QString();
                } else {
                    addToCache(std::move(readData));
                }
                preloadFilesInProgress.removeAll(readData.absoluteFilePath);
                cacheFutureWatcher->deleteLater();
            });

#if QT_VERSION < QT_VERSION_CHECK(6, 0, 0)
    cacheFutureWatcher->setFuture(
            QtConcurrent::run(this, &QVImageCore::readFile, filePath, targetColorSpace));
#else
    cacheFutureWatcher->setFuture(
            QtConcurrent::run(&QVImageCore::readFile, this, filePath, targetColorSpace));
#endif
}

void QVImageCore::addToCache(const ReadData &&readData)
{
    if (readData.image.isNull())
        return;

    QString cacheKey = getPixmapCacheKey(readData.absoluteFilePath, readData.fileSize,
                                         readData.targetColorSpace);
    qint64 pixmapMemoryBytes = static_cast<qint64>(readData.image.width()) * readData.image.height()
            * readData.image.depth() / 8;

    qint64 pixmapMemoryKiB = qMax(pixmapMemoryBytes / 1024, 1LL);
    QVImageCore::imageCache.insert(cacheKey, new ReadData(std::move(readData)), pixmapMemoryKiB);
}

QString QVImageCore::getPixmapCacheKey(const QString &absoluteFilePath, const qint64 &fileSize,
                                       const QColorSpace &targetColorSpace)
{
#if QT_VERSION >= QT_VERSION_CHECK(5, 14, 0)
    QString targetColorSpaceHash =
            QCryptographicHash::hash(targetColorSpace.iccProfile(), QCryptographicHash::Md5)
                    .toHex();
#else
    QString targetColorSpaceHash = "";
#endif
    return absoluteFilePath + "\n" + QString::number(fileSize) + "\n" + targetColorSpaceHash;
}

QColorSpace QVImageCore::getTargetColorSpace() const
{
#if QT_VERSION >= QT_VERSION_CHECK(5, 14, 0)
    return colorSpaceConversion == 1    ? detectDisplayColorSpace()
            : colorSpaceConversion == 2 ? QColorSpace::SRgb
            : colorSpaceConversion == 3 ? QColorSpace::DisplayP3
                                        : QColorSpace();
#else
    return {};
#endif
}

QColorSpace QVImageCore::detectDisplayColorSpace() const
{
#if QT_VERSION >= QT_VERSION_CHECK(5, 14, 0)
    QWindow *window = static_cast<QWidget *>(parent())->window()->windowHandle();

    QByteArray profileData;
#  ifdef WIN32_LOADED
    profileData = QVWin32Functions::getIccProfileForWindow(window);
#  endif
#  ifdef COCOA_LOADED
    profileData = QVCocoaFunctions::getIccProfileForWindow(window);
#  endif
#  ifdef X11_LOADED
    profileData = QVLinuxX11Functions::getIccProfileForWindow(window);
#  endif

    if (!profileData.isEmpty()) {
        QColorSpace colorSpace = QColorSpace::fromIccProfile(profileData);
#  if QT_VERSION < QT_VERSION_CHECK(6, 7, 2)
        if (!colorSpace.isValid() && removeTinyDataTagsFromIccProfile(profileData))
            colorSpace = QColorSpace::fromIccProfile(profileData);
#  endif
        return colorSpace;
    }
#endif

    return {};
}

#if QT_VERSION >= QT_VERSION_CHECK(5, 14, 0) && QT_VERSION < QT_VERSION_CHECK(6, 7, 2)
// Workaround for QTBUG-125241
bool QVImageCore::removeTinyDataTagsFromIccProfile(QByteArray &profile)
{
    const int offsetTagCount = 128;
    const qsizetype length = profile.length();
    qsizetype offset = offsetTagCount;
    char *data = profile.data();
    bool foundTinyData = false;
    // read tag count
    if (length - offset < 4)
        return false;
    quint32 tagCount = qFromBigEndian<quint32>(data + offset);
    offset += 4;
    // so we don't have to worry about overflows
    if (tagCount > 99999)
        return false;
    // loop through tags
    if (length - offset < qsizetype(tagCount * 12))
        return false;
    while (tagCount) {
        tagCount -= 1;
        const quint32 dataSize = qFromBigEndian<quint32>(data + offset + 8);
        if (dataSize >= 12) {
            // this tag is fine
            offset += 12;
            continue;
        }
        // qt will fail on this tag, remove it
        foundTinyData = true;
        if (tagCount) {
            // shift subsequent tags back
            std::memmove(data + offset, data + offset + 12, tagCount * 12);
        }
        // zero fill gap at end
        std::memset(data + offset + (tagCount * 12), 0, 12);
        // decrement tag count
        qToBigEndian(qFromBigEndian<quint32>(data + offsetTagCount) - 1, data + offsetTagCount);
    }
    return foundTinyData;
}
#endif

void QVImageCore::jumpToNextFrame()
{
    if (currentFileDetails.isMovieLoaded)
        loadedMovie.jumpToNextFrame();
}

void QVImageCore::setPaused(bool desiredState)
{
    if (currentFileDetails.isMovieLoaded)
        loadedMovie.setPaused(desiredState);
}

void QVImageCore::setSpeed(int desiredSpeed)
{
    if (desiredSpeed < 0)
        desiredSpeed = 0;

    if (desiredSpeed > 1000)
        desiredSpeed = 1000;

    if (currentFileDetails.isMovieLoaded)
        loadedMovie.setSpeed(desiredSpeed);
}

void QVImageCore::rotateImage(int rotation)
{
    currentRotation += rotation;

    // normalize between 360 and 0
    currentRotation = (currentRotation % 360 + 360) % 360;
    QTransform transform;

    QImage transformedImage;
    if (currentFileDetails.isMovieLoaded) {
        transform.rotate(currentRotation);
        transformedImage = loadedMovie.currentImage().transformed(transform);
    } else {
        transform.rotate(rotation);
        transformedImage = loadedPixmap.toImage().transformed(transform);
    }

    loadedPixmap.convertFromImage(transformedImage);

    currentFileDetails.loadedPixmapSize = QSize(loadedPixmap.width(), loadedPixmap.height());
    emit updateLoadedPixmapItem();
}

QImage QVImageCore::matchCurrentRotation(const QImage &imageToRotate)
{
    if (!currentRotation)
        return imageToRotate;

    QTransform transform;
    transform.rotate(currentRotation);
    return imageToRotate.transformed(transform);
}

// TODO: Remove this function---extremely inefficient
QPixmap QVImageCore::matchCurrentRotation(const QPixmap &pixmapToRotate)
{
    if (!currentRotation)
        return pixmapToRotate;

    return QPixmap::fromImage(matchCurrentRotation(pixmapToRotate.toImage()));
}

QPixmap QVImageCore::scaleExpensively(const int desiredWidth, const int desiredHeight)
{
    return scaleExpensively(QSizeF(desiredWidth, desiredHeight));
}

QPixmap QVImageCore::scaleExpensively(const QSizeF desiredSize)
{
    if (!currentFileDetails.isPixmapLoaded)
        return QPixmap();

    QSize size = QSize(loadedPixmap.width(), loadedPixmap.height());
    size.scale(desiredSize.toSize(), Qt::KeepAspectRatio);

    // Get the current frame of the animation if this is an animation
    QPixmap relevantPixmap;
    if (!currentFileDetails.isMovieLoaded) {
        relevantPixmap = loadedPixmap;
    } else {
        relevantPixmap = loadedMovie.currentPixmap();
        relevantPixmap = matchCurrentRotation(relevantPixmap);
    }

    // If we are really close to the original size, just return the original
    if (abs(desiredSize.width() - relevantPixmap.width()) < 1
        && abs(desiredSize.height() - relevantPixmap.height()) < 1) {
        return relevantPixmap;
    }

    return relevantPixmap.scaled(size, Qt::KeepAspectRatio, Qt::SmoothTransformation);
    ;
}

void QVImageCore::settingsUpdated()
{
    auto &settingsManager = qvApp->getSettingsManager();

    // preloading mode
    // Cost is in KiB
    switch (qvGetSettingInt(PreloadingMode)) {
    case 0: {
        QVImageCore::imageCache.setMaxCost(0);
        break;
    }
    case 1: {
        QVImageCore::imageCache.setMaxCost(256000);
        break;
    }
    case 2: {
        QVImageCore::imageCache.setMaxCost(2048000);
        break;
    }
    default:
        Q_ASSERT(false);
    }

    // update folder info to reflect new settings (e.g. sort order)
    updateFolderInfo();

    bool changedImagePreprocessing = false;

    // colorspaceconversion
    if (colorSpaceConversion != qvGetSettingInt(ColorSpaceConversion)) {
        colorSpaceConversion = qvGetSettingInt(ColorSpaceConversion);
        changedImagePreprocessing = true;
    }

    if (changedImagePreprocessing && currentFileDetails.isPixmapLoaded)
        loadFile(mediaCatalog.state().fileInfo.absoluteFilePath());
}
