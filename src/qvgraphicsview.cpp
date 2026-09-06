#include "qvgraphicsview.h"
#include "qvapplication.h"
#include "qvvideoview.h"
#include "qvfiltereffect.h"
#include "qvinfodialog.h"
#include "qvcocoafunctions.h"
#include "settingsmanager.h"
#include <QWheelEvent>
#include <QGraphicsPixmapItem>
#include <QGraphicsScene>
#include <QGraphicsVideoItem>
#include <QSettings>
#include <QMessageBox>
#include <QMovie>
#include <QtMath>
#include <QGestureEvent>
#include <QScrollBar>
#include <QElapsedTimer>

QVExport::Source QVGraphicsView::exportSource() const
{
    QVExport::Source source;
    source.path = getCurrentMedia().fileInfo.absoluteFilePath();
    source.video = videoCanvasActive;
    source.animated = !source.video && getImageDetails().isMovieLoaded;
    source.size = source.video ? videoNativeSize : getImageDetails().baseImageSize;
    if (source.video && videoView) {
        source.frame = videoView->exportFrame();
        source.positionMs = videoView->exportPosition();
        source.durationMs = videoView->duration();
        source.speed = videoView->playbackSpeed() / 100.0;
        if (!source.frame.isNull())
            source.size = source.frame.size();
    } else if (source.animated) {
        source.frameNumber = qMax(0, getLoadedMovie().currentFrameNumber());
        source.animationFormat = getLoadedMovie().format();
    } else if (source.path.isEmpty()) {
        source.frame = getLoadedPixmap().toImage();
        source.size = source.frame.size();
    }
    if (!source.video && !source.path.isEmpty()) {
        QImageReader reader(source.path);
        if (reader.transformation() & QImageIOHandler::TransformationRotate90)
            source.size.transpose();
    }
    source.rotation = source.video && activeCanvasItem()
            ? (qRound(activeCanvasItem()->rotation()) % 360 + 360) % 360
            : (imageCore.getCurrentRotation() % 360 + 360) % 360;
    source.mirrored = transform().m11() < 0;
    source.flipped = transform().m22() < 0;
    source.loop = loopMode == QVPlaybackLoopMode::ForceLoop
            || (loopMode == QVPlaybackLoopMode::Default && !source.video
                && imageCore.currentAnimationLoopsByDefault());
    source.muted = source.video && isVideoMuted();
    source.layers = layers.stack();
    return source;
}

QVGraphicsView::QVGraphicsView(QWidget *parent) : QGraphicsView(parent)
{
    // GraphicsView setup
    setVerticalScrollBarPolicy(Qt::ScrollBarAlwaysOff);
    setHorizontalScrollBarPolicy(Qt::ScrollBarAlwaysOff);
    setDragMode(QGraphicsView::ScrollHandDrag);
    setFrameShape(QFrame::NoFrame);
    setTransformationAnchor(QGraphicsView::NoAnchor);
    viewport()->setAutoFillBackground(false);

    // part of a pathetic attempt at gesture support
    grabGesture(Qt::PinchGesture);

    // Scene setup
    auto *scene = new QGraphicsScene(-1000000.0, -1000000.0, 2000000.0, 2000000.0, this);
    setScene(scene);

    // Initialize other variables
    currentScale = 1.0;
    scaledSize = QSize();
    isOriginalSize = false;
    lastZoomEventPos = QPoint(-1, -1);
    lastZoomRoundingError = QPointF();
    lastScrollRoundingError = QPointF();
    mousePressButton = Qt::MouseButton::NoButton;
    mousePressModifiers = Qt::KeyboardModifier::NoModifier;
    mousePressPosition = QPoint();
    videoView = nullptr;
    mediaRequestGeneration = 0;
    videoCanvasActive = false;
    loadingNeighbor = false;
    restoreCenterAfterExpensiveScale = false;
    loopMode = QVPlaybackLoopMode::Default;

    zoomBasisScaleFactor = 1.0;

    connect(&imageCore, &QVImageCore::animatedFrameChanged, this,
            &QVGraphicsView::animatedFrameChanged);
    connect(&imageCore, &QVImageCore::fileChanged, this, &QVGraphicsView::postLoad);
    connect(&imageCore, &QVImageCore::updateLoadedPixmapItem, this,
            &QVGraphicsView::updateLoadedPixmapItem);

    // Should replace the other timer eventually
    expensiveScaleTimerNew = new QTimer(this);
    expensiveScaleTimerNew->setSingleShot(true);
    expensiveScaleTimerNew->setInterval(50);
    connect(expensiveScaleTimerNew, &QTimer::timeout, this, [this] {
        if (restoreCenterAfterExpensiveScale)
            expensiveScaleCenter = normalizedCanvasCenter() + canvasCenterRoundingError;
        scaleExpensively();
        if (restoreCenterAfterExpensiveScale) {
            centerOnNormalizedCanvasPoint(expensiveScaleCenter);
            restoreCenterAfterExpensiveScale = false;
        }
    });

    loadedPixmapItem = new QGraphicsPixmapItem();
    scene->addItem(loadedPixmapItem);
    connect(&layers, &QVLayerModel::pixelsChanged, this, &QVGraphicsView::updateLayerEffects);

    // Connect to settings signal
    connect(&qvApp->getSettingsManager(), &SettingsManager::settingsUpdated, this,
            &QVGraphicsView::settingsUpdated);
    settingsUpdated();
}

QVGraphicsView::~QVGraphicsView()
{
    // QGraphicsScene also owns its items, so tear down the video output before
    // QObject deletes the scene inherited from this view.
    delete videoView;
    videoView = nullptr;
}

// Events

void QVGraphicsView::resizeEvent(QResizeEvent *event)
{
    QGraphicsView::resizeEvent(event);
    if (!isOriginalSize)
        resetScale();
    else if (hasActiveCanvasItem())
        centerOn(activeCanvasItem());
}

void QVGraphicsView::dropEvent(QDropEvent *event)
{
    QGraphicsView::dropEvent(event);
    loadMimeData(event->mimeData());
}

void QVGraphicsView::dragEnterEvent(QDragEnterEvent *event)
{
    QGraphicsView::dragEnterEvent(event);
    if (event->mimeData()->hasUrls()) {
        event->acceptProposedAction();
    }
}

void QVGraphicsView::dragMoveEvent(QDragMoveEvent *event)
{
    QGraphicsView::dragMoveEvent(event);
    event->acceptProposedAction();
}

void QVGraphicsView::dragLeaveEvent(QDragLeaveEvent *event)
{
    QGraphicsView::dragLeaveEvent(event);
    event->accept();
}

#if QT_VERSION < QT_VERSION_CHECK(6, 0, 0)
void QVGraphicsView::enterEvent(QEvent *event)
#else
void QVGraphicsView::enterEvent(QEnterEvent *event)
#endif
{
    QGraphicsView::enterEvent(event);
    viewport()->setCursor(Qt::ArrowCursor);
}

void QVGraphicsView::mousePressEvent(QMouseEvent *event)
{
    if (event->button() == Qt::BackButton) {
        goToFile(GoToFileMode::previous);
        return;
    }
    if (event->button() == Qt::ForwardButton) {
        goToFile(GoToFileMode::next);
        return;
    }
    if (event->button() == Qt::MiddleButton) {
        resetScale();
        return;
    }

    const auto startWindowMove = [this, event]() {
#ifdef COCOA_LOADED
        return QVCocoaFunctions::startSystemMove(window());
#else
#if QT_VERSION >= QT_VERSION_CHECK(5, 15, 0)
        return window()->windowHandle()->startSystemMove();
#else
        Q_UNUSED(event)
        return false;
#endif
#endif
    };

    const auto startFallbackWindowMove = [this, event]() {
        mousePressButton = event->button();
        mousePressModifiers = event->modifiers();
        mousePressPosition = event->pos();
    };

    // Check for Ctrl/Cmd drag
    if (event->button() == Qt::LeftButton &&
        event->modifiers().testFlag(Qt::ControlModifier) &&
        qvApp->getSettingsManager().getBool(SettingsManager::Setting::CtrlDragWindow)) {
        const auto windowState = window()->windowState();
        if (!windowState.testFlag(Qt::WindowFullScreen)
            && !windowState.testFlag(Qt::WindowMaximized)) {
            if (!startWindowMove()) {
                startFallbackWindowMove();
            }
            return;
        }
    }

    // Check for titlebar region drag
    if (event->button() == Qt::LeftButton) {
        const auto windowState = window()->windowState();
        if (!windowState.testFlag(Qt::WindowFullScreen)
            && !windowState.testFlag(Qt::WindowMaximized)) {
#ifdef COCOA_LOADED
            // Check if click is in titlebar region
            int titlebarHeight = QVCocoaFunctions::getTitlebarHeight(window()->windowHandle());
            if (event->pos().y() <= titlebarHeight) {
                if (!startWindowMove()) {
                    startFallbackWindowMove();
                }
                return;
            }
#endif
        }
    }

    QGraphicsView::mousePressEvent(event);
}

void QVGraphicsView::mouseDoubleClickEvent(QMouseEvent *event)
{
    if (event->button() == Qt::LeftButton) {
        emit fullscreenRequested();
        return;
    }
    QGraphicsView::mouseDoubleClickEvent(event);
}

void QVGraphicsView::mouseMoveEvent(QMouseEvent *event)
{
    if (mousePressButton == Qt::LeftButton) {
        if (mousePressModifiers.testFlag(Qt::ControlModifier)
            && !event->modifiers().testFlag(Qt::ControlModifier)) {
            mousePressButton = Qt::NoButton;
            mousePressModifiers = Qt::NoModifier;
            QGraphicsView::mouseMoveEvent(event);
            return;
        }

        const QPoint delta = event->pos() - mousePressPosition;
        window()->move(window()->pos() + delta);
        return;
    }

    QGraphicsView::mouseMoveEvent(event);
}

void QVGraphicsView::mouseReleaseEvent(QMouseEvent *event)
{
    mousePressButton = Qt::NoButton;
    mousePressModifiers = Qt::NoModifier;
    QGraphicsView::mouseReleaseEvent(event);
    viewport()->setCursor(Qt::ArrowCursor);
}

bool QVGraphicsView::event(QEvent *event)
{
    // this is for touchpad pinch gestures
    if (event->type() == QEvent::Gesture) {
        auto *gestureEvent = static_cast<QGestureEvent *>(event);
        if (QGesture *pinch = gestureEvent->gesture(Qt::PinchGesture)) {
            auto *pinchGesture = static_cast<QPinchGesture *>(pinch);
            QPinchGesture::ChangeFlags changeFlags = pinchGesture->changeFlags();

            if (changeFlags & QPinchGesture::ScaleFactorChanged) {
                const QPoint hotPoint = mapFromGlobal(pinchGesture->hotSpot().toPoint());
                zoom(pinchGesture->scaleFactor(), hotPoint);
            }

            // Fun rotation stuff maybe later
            //            if (changeFlags & QPinchGesture::RotationAngleChanged) {
            //                qreal rotationDelta = pinchGesture->rotationAngle() -
            //                pinchGesture->lastRotationAngle(); rotate(rotationDelta);
            //                centerOn(loadedPixmapItem);
            //            }
            return true;
        }
    } else if (event->type() == QEvent::NativeGesture) {
        auto *nativeEvent = static_cast<QNativeGestureEvent *>(event);
        if (nativeEvent->gestureType() == Qt::ZoomNativeGesture) {
#if (QT_VERSION >= QT_VERSION_CHECK(6, 0, 0))
            const QPoint eventPos = nativeEvent->position().toPoint();
#else
            const QPoint eventPos = nativeEvent->pos();
#endif
            zoom(nativeEvent->value() + 1, eventPos);
            return true;
        }
    }
    return QGraphicsView::event(event);
}

void QVGraphicsView::wheelEvent(QWheelEvent *event)
{
#if (QT_VERSION >= QT_VERSION_CHECK(5, 14, 0))
    const QPoint eventPos = event->position().toPoint();
#else
    const QPoint eventPos = event->pos();
#endif

    const bool modifierPressed = event->modifiers().testFlag(Qt::ControlModifier);
    bool dontZoom = qvGetSettingInt(ScrollZoom) == 2;
    if (modifierPressed) {
        dontZoom = !dontZoom;
    }

    bool touchDeviceDetected = false;
#if QT_VERSION >= QT_VERSION_CHECK(6, 0, 0)
    // Auto-detect touchpad
    touchDeviceDetected = event->device()->type() == QInputDevice::DeviceType::TouchPad
            || event->device()->type() == QInputDevice::DeviceType::TouchScreen;
    // Real touchpads are likely to exhibit these characteristics in empirical testing
    touchDeviceDetected = touchDeviceDetected && event->phase() != Qt::NoScrollPhase;
    if (touchDeviceDetected && qvGetSettingInt(ScrollZoom) == 1) {
        // If this is a touch device, override setting
        dontZoom = !modifierPressed;
    }
#endif

    if (dontZoom) {
        const qreal scrollDivisor = 2.0; // To make scrolling less sensitive
        qreal scrollX = event->angleDelta().x() * (isRightToLeft() ? 1 : -1) / scrollDivisor;
        qreal scrollY = event->angleDelta().y() * -1 / scrollDivisor;

        if (event->modifiers() & Qt::ShiftModifier)
            std::swap(scrollX, scrollY);

        QPointF targetScrollDelta = QPointF(scrollX, scrollY) - lastScrollRoundingError;
        QPoint roundedScrollDelta = targetScrollDelta.toPoint();

        horizontalScrollBar()->setValue(horizontalScrollBar()->value() + roundedScrollDelta.x());
        verticalScrollBar()->setValue(verticalScrollBar()->value() + roundedScrollDelta.y());

        lastScrollRoundingError = roundedScrollDelta - targetScrollDelta;

        return;
    }

    const int yDelta = event->angleDelta().y();
    const qreal yScale = 120.0;

    if (yDelta == 0)
        return;

    const qreal zoomAmountPerWheelClick = qvGetSettingInt(ScaleFactor)/100.0;
    qreal zoomFactor = zoomAmountPerWheelClick;
    if (qvGetSettingBool(FractionalZoom) || touchDeviceDetected) {
        const qreal fractionalWheelClicks = qFabs(yDelta) / yScale;
        zoomFactor *= fractionalWheelClicks;
    }
    zoomFactor += 1.0;

    if (yDelta < 0)
        zoomFactor = qPow(zoomFactor, -1);

    zoom(zoomFactor, eventPos);
}

// Functions

QMimeData *QVGraphicsView::getMimeData() const
{
    auto *mimeData = new QMimeData();
    if (!getImageDetails().isPixmapLoaded)
        return mimeData;

    mimeData->setUrls(
            { QUrl::fromLocalFile(imageCore.getCurrentMedia().fileInfo.absoluteFilePath()) });
    mimeData->setImageData(imageCore.getLoadedPixmap().toImage());
    return mimeData;
}

void QVGraphicsView::loadMimeData(const QMimeData *mimeData)
{
    if (mimeData == nullptr)
        return;

    if (!mimeData->hasUrls())
        return;

    const QList<QUrl> urlList = mimeData->urls();

    bool first = true;
    for (const auto &url : urlList) {
        if (first) {
            loadFile(url.toString());
            emit cancelSlideshow();
            first = false;
            continue;
        }
        QVApplication::openFile(url.toString());
    }
}

void QVGraphicsView::loadFile(const QString &fileName)
{
    if (!loadingNeighbor)
        navigationCanvasState.valid = false;

    const quint64 requestGeneration = ++mediaRequestGeneration;
    QString sanitaryFileName = fileName;
    const QUrl url(fileName);
    if (url.isLocalFile())
        sanitaryFileName = url.toLocalFile();

    const QFileInfo fileInfo(sanitaryFileName);
    sanitaryFileName = fileInfo.absoluteFilePath();
    if (fileInfo.isDir()) {
        imageCore.updateFolderInfo(sanitaryFileName);
        if (getCurrentMedia().folderFiles.isEmpty())
            closeImage();
        else
            loadFile(getCurrentMedia().folderFiles.constFirst().absoluteFilePath);
        return;
    }

    const auto mediaType = imageCore.mediaTypeForFile(fileInfo);
    if (mediaType == QVMediaCatalog::MediaType::Video) {
        resetCanvasForNewMedia();
        videoNativeSize = QSize();
        videoLayoutSize = QSize();
        activateVideoCanvas();
        imageCore.activateExternalMedia(sanitaryFileName, mediaType);
        // Let the already-visible window paint before the multimedia backend is
        // initialized. This keeps launch responsive even when the backend is cold.
        QTimer::singleShot(0, this, [this, sanitaryFileName, requestGeneration]() {
            if (requestGeneration == mediaRequestGeneration
                && getCurrentMedia().fileInfo.absoluteFilePath() == sanitaryFileName
                && getCurrentMedia().mediaType == QVMediaCatalog::MediaType::Video) {
                ensureVideoView();
                videoView->graphicsItem()->show();
                videoView->loadFile(sanitaryFileName);
            }
        });
    } else {
        if (imageCore.isLoadInProgress()) {
            navigationCanvasState.valid = false;
            return;
        }
        const bool wasVideoCanvasActive = videoCanvasActive;
        // Images bake rotation into their decoded pixels. Transfer the video
        // orientation before decoding, while image update signals are still hidden.
        if (wasVideoCanvasActive && navigationCanvasState.valid) {
            const int rotationDelta = navigationCanvasState.rotation - imageCore.getCurrentRotation();
            if (rotationDelta != 0)
                imageCore.rotateImage(rotationDelta);
        }
        closeVideo();
        if (wasVideoCanvasActive)
            resetCanvasForNewMedia();
        activateImageCanvas();
        imageCore.loadFile(sanitaryFileName);
    }
}

void QVGraphicsView::reloadFile()
{
    if (!getImageDetails().isPixmapLoaded)
        return;

    imageCore.loadFile(getCurrentMedia().fileInfo.absoluteFilePath(), true);
}

void QVGraphicsView::postLoad()
{
    updateLoadedPixmapItem();
    qvApp->getActionManager().addFileToRecentsList(getCurrentMedia().fileInfo);

    emit fileChanged();
}

void QVGraphicsView::zoomIn(const QPoint &pos)
{
    zoom(qvGetSettingInt(ScaleFactor)/100.0 + 1, pos);
}

void QVGraphicsView::zoomOut(const QPoint &pos)
{
    zoom(qPow(qvGetSettingInt(ScaleFactor)/100.0 + 1, -1), pos);
}

void QVGraphicsView::zoom(qreal scaleFactor, const QPoint &pos)
{
    restoreCenterAfterExpensiveScale = false;
    canvasCenterRoundingError = QPointF();

    // don't zoom too far out, dude
    currentScale *= scaleFactor;
    if (currentScale >= 500 || currentScale <= 0.01) {
        currentScale *= qPow(scaleFactor, -1);
        return;
    }

    updateFilteringMode();

    if (pos != lastZoomEventPos) {
        lastZoomEventPos = pos;
        lastZoomRoundingError = QPointF();
    }
    const QPointF scenePos = mapToScene(pos) - lastZoomRoundingError;

    zoomBasisScaleFactor *= scaleFactor;
    setTransform(QTransform(zoomBasis).scale(zoomBasisScaleFactor, zoomBasisScaleFactor));
    absoluteTransform.scale(scaleFactor, scaleFactor);

    // If we are zooming in, we have a point to zoom towards, the mouse is on top of the viewport,
    // and cursor zooming is enabled
    if (currentScale > 1.00001 && pos != QPoint(-1, -1) && underMouse()
        && qvGetSettingBool(CursorZoom)) {
        const QPointF p1mouse = mapFromScene(scenePos);
        const QPointF move = p1mouse - pos;
        horizontalScrollBar()->setValue(horizontalScrollBar()->value()
                                        + (move.x() * (isRightToLeft() ? -1 : 1)));
        verticalScrollBar()->setValue(verticalScrollBar()->value() + move.y());
        lastZoomRoundingError = mapToScene(pos) - scenePos;
    } else {
        centerOn(activeCanvasItem());
    }

    if (qvGetSettingBool(ScalingEnabled) && !isOriginalSize) {
        expensiveScaleTimerNew->start();
    }
}

void QVGraphicsView::scaleExpensively()
{
    if (videoCanvasActive)
        return;

    // Determine if mirrored or flipped
    bool mirrored = false;
    if (transform().m11() < 0)
        mirrored = true;

    bool flipped = false;
    if (transform().m22() < 0)
        flipped = true;

    // If we are above maximum scaling size
    if ((currentScale >= MAX_EXPENSIVE_SCALING_SIZE)
        || (!qvGetSettingBool(ScalingTwoEnabled) && currentScale > 1.00001)) {
        // Return to original size
        makeUnscaled();
        return;
    }

    // Map size of the original pixmap to the scale acquired in fitting with modification from
    // zooming percentage
    const QRectF mappedRect =
            absoluteTransform.mapRect(QRectF({}, getImageDetails().loadedPixmapSize));
    const QSizeF mappedPixmapSize = mappedRect.size() * devicePixelRatioF();

    // Undo mirror/flip before new transform
    if (mirrored)
        scale(-1, 1);

    if (flipped)
        scale(1, -1);

    // Set image to scaled version
    loadedPixmapItem->setPixmap(imageCore.scaleExpensively(mappedPixmapSize));

    // Reset transformation
    setTransform(
            QTransform::fromScale(qPow(devicePixelRatioF(), -1), qPow(devicePixelRatioF(), -1)));

    // Redo mirror/flip after new transform
    if (mirrored)
        scale(-1, 1);

    if (flipped)
        scale(1, -1);

    // Set zoombasis
    zoomBasis = transform();
    zoomBasisScaleFactor = 1.0;
}

void QVGraphicsView::makeUnscaled()
{
    // Determine if mirrored or flipped
    bool mirrored = false;
    if (transform().m11() < 0)
        mirrored = true;

    bool flipped = false;
    if (transform().m22() < 0)
        flipped = true;

    // Return images to their original pixels. Video frames remain rendered by
    // QGraphicsVideoItem and only need the canvas transform restored.
    if (!videoCanvasActive) {
        if (getImageDetails().isMovieLoaded)
            loadedPixmapItem->setPixmap(getLoadedMovie().currentPixmap());
        else
            loadedPixmapItem->setPixmap(getLoadedPixmap());
    }

    setTransform(absoluteTransform);

    // Redo mirror/flip after new transform
    if (mirrored)
        scale(-1, 1);

    if (flipped)
        scale(1, -1);

    // Reset transformation
    zoomBasis = transform();
    zoomBasisScaleFactor = 1.0;
}

void QVGraphicsView::updateFilteringMode()
{
    if (videoCanvasActive)
        return;

    const bool exceededSmoothScaleLimit = currentScale >= MAX_FILTERING_SIZE;
    loadedPixmapItem->setTransformationMode(!exceededSmoothScaleLimit
                                                            && qvGetSettingBool(FilteringEnabled)
                                                    ? Qt::SmoothTransformation
                                                    : Qt::FastTransformation);
}

void QVGraphicsView::animatedFrameChanged(QRect rect)
{
    Q_UNUSED(rect)

    if (qvGetSettingBool(ScalingEnabled)) {
        scaleExpensively();
    } else {
        loadedPixmapItem->setPixmap(getLoadedMovie().currentPixmap());
    }
}

void QVGraphicsView::updateLoadedPixmapItem()
{
    if (getCurrentMedia().mediaType == QVMediaCatalog::MediaType::Video)
        return;

    activateImageCanvas();

    // set pixmap and offset
    loadedPixmapItem->setPixmap(getLoadedPixmap());
    scaledSize = loadedPixmapItem->boundingRect().size().toSize();

    // Window sizing can synchronously reset the view via resizeEvent. Restore
    // navigation framing only after that sizing has completed.
    emit updatedLoadedPixmapItem();
    if (!restoreCanvasStateForLoadedMedia())
        resetScale();
}

void QVGraphicsView::resetScale()
{
    restoreCenterAfterExpensiveScale = false;
    canvasCenterRoundingError = QPointF();

    if (!hasActiveCanvasItem())
        return;

    fitInViewMarginless(activeCanvasItem());

    if (!videoCanvasActive && qvGetSettingBool(ScalingEnabled))
        expensiveScaleTimerNew->start();
}

void QVGraphicsView::resetView()
{
    // An in-flight navigation must not restore the view the user just reset.
    navigationCanvasState.valid = false;
    resetCanvasForNewMedia();

    // Image rotation is baked into decoded pixels; video rotation lives on its
    // graphics item and was cleared above. Also clear the retained image rotation
    // while viewing video so it cannot reappear on the next image.
    const int rotation = imageCore.getCurrentRotation();
    if (rotation != 0) {
        imageCore.rotateImage(-rotation);
        if (!videoCanvasActive)
            return; // The image-core signal already rebuilt and fitted the pixmap.
    }

    if (videoCanvasActive) {
        emit updatedLoadedPixmapItem();
        resetScale();
    } else {
        updateLoadedPixmapItem();
    }
}

void QVGraphicsView::originalSize()
{
    restoreCenterAfterExpensiveScale = false;
    canvasCenterRoundingError = QPointF();
    if (isOriginalSize) {
        // If we are at the actual original size
        if (transform() == QTransform()) {
            resetScale(); // back to normal mode
            return;
        }
    }
    makeUnscaled();

    resetTransform();
    centerOn(activeCanvasItem());

    zoomBasis = transform();
    zoomBasisScaleFactor = 1.0;
    absoluteTransform = transform();

    isOriginalSize = true;
}

void QVGraphicsView::goToFile(const GoToFileMode &mode, int index)
{
    // The image decoder accepts only one request at a time. Ignored key repeats
    // must not overwrite or discard the snapshot belonging to that request.
    if (imageCore.isLoadInProgress())
        return;

    bool shouldRetryFolderInfoUpdate = false;

    // Update folder info only after a little idle time as an optimization for when
    // the user is rapidly navigating through files.
    if (!getImageDetails().timeSinceLoaded.isValid()
        || getImageDetails().timeSinceLoaded.hasExpired(3000)) {
        // Make sure the file still exists because if it disappears from the file listing we'll lose
        // track of our index within the folder. Use the static 'exists' method to avoid caching.
        // If we skip updating now, flag it for retry later once we locate a new file.
        if (QFile::exists(getCurrentMedia().fileInfo.absoluteFilePath()))
            imageCore.updateFolderInfo();
        else
            shouldRetryFolderInfoUpdate = true;
    }

    const auto &fileList = getCurrentMedia().folderFiles;
    if (fileList.isEmpty())
        return;

    int newIndex = getCurrentMedia().currentIndexInFolder;
    int searchDirection = 0;

    switch (mode) {
    case GoToFileMode::constant: {
        newIndex = index;
        break;
    }
    case GoToFileMode::first: {
        newIndex = 0;
        searchDirection = 1;
        break;
    }
    case GoToFileMode::previous: {
        if (newIndex == 0) {
            if (qvGetSettingBool(LoopFoldersEnabled))
                newIndex = fileList.size() - 1;
            else
                emit cancelSlideshow();
        } else
            newIndex--;
        searchDirection = -1;
        break;
    }
    case GoToFileMode::next: {
        if (fileList.size() - 1 == newIndex) {
            if (qvGetSettingBool(LoopFoldersEnabled))
                newIndex = 0;
            else
                emit cancelSlideshow();
        } else
            newIndex++;
        searchDirection = 1;
        break;
    }
    case GoToFileMode::last: {
        newIndex = fileList.size() - 1;
        searchDirection = -1;
        break;
    }
    }

    if (searchDirection != 0) {
        while (searchDirection == 1 && newIndex < fileList.size() - 1
               && !QFile::exists(fileList.value(newIndex).absoluteFilePath))
            newIndex++;
        while (searchDirection == -1 && newIndex > 0
               && !QFile::exists(fileList.value(newIndex).absoluteFilePath))
            newIndex--;
    }

    const QString nextImageFilePath = fileList.value(newIndex).absoluteFilePath;

    if (!QFile::exists(nextImageFilePath)
        || nextImageFilePath == getCurrentMedia().fileInfo.absoluteFilePath())
        return;

    if (shouldRetryFolderInfoUpdate) {
        // If the user just deleted a file through qMedia, closeImage will have been called which
        // clears the current media file. In this case updateFolderInfo can't infer the
        // directory from fileInfo like it normally does, so we'll explicity pass in the folder
        // here.
        imageCore.updateFolderInfo(QFileInfo(nextImageFilePath).path());
    }

    if (mode == GoToFileMode::previous || mode == GoToFileMode::next)
        saveCanvasStateForNeighborNavigation();
    else
        navigationCanvasState.valid = false;

    loadingNeighbor = true;
    loadFile(nextImageFilePath);
    loadingNeighbor = false;
}

void QVGraphicsView::fitInViewMarginless(const QRectF &rect)
{
#ifdef COCOA_LOADED
    int obscuredHeight = QVCocoaFunctions::getObscuredHeight(window()->windowHandle());
#else
    int obscuredHeight = 0;
#endif

    // Set adjusted image size / bounding rect based on
    QSize adjustedImageSize = currentMediaSize();
    QRectF adjustedBoundingRect = rect;

    switch (qvGetSettingInt(CropMode)) { // should be enum tbh
    case 1: // only take into account height
    {
        adjustedImageSize.setWidth(1);
        adjustedBoundingRect.setWidth(1);
        break;
    }
    case 2: // only take into account width
    {
        adjustedImageSize.setHeight(1);
        adjustedBoundingRect.setHeight(1);
        break;
    }
    }
    adjustedBoundingRect.moveCenter(rect.center());

    if (!scene() || adjustedBoundingRect.isNull())
        return;

    // Reset the view scale to 1:1.
    QRectF unity = transform().mapRect(QRectF(0, 0, 1, 1));
    if (unity.isEmpty())
        return;
    scale(1 / unity.width(), 1 / unity.height());

    // Determine what we are resizing to
    const int adjWidth = width() - MARGIN;
    const int adjHeight = height() - MARGIN - obscuredHeight;

    QRectF viewRect;
    // Resize to window size unless you are meant to stop at the actual size, basically
    if (qvGetSettingBool(PastActualSizeEnabled)
        || (adjustedImageSize.width() >= adjWidth || adjustedImageSize.height() >= adjHeight)) {
        viewRect = viewport()->rect().adjusted(MARGIN, MARGIN, -MARGIN, -MARGIN);
        viewRect.setHeight(viewRect.height() - obscuredHeight);
    } else {
        // stop at actual size
        viewRect = QRect(QPoint(), currentMediaSize());
        QPoint center = this->rect().center();
        center.setY(center.y() - obscuredHeight);
        viewRect.moveCenter(center);
    }

    if (viewRect.isEmpty())
        return;

    // Find the ideal x / y scaling ratio to fit \a rect in the view.
    QRectF sceneRect = transform().mapRect(adjustedBoundingRect);
    if (sceneRect.isEmpty())
        return;

    qreal xratio = viewRect.width() / sceneRect.width();
    qreal yratio = viewRect.height() / sceneRect.height();

    xratio = yratio = qMin(xratio, yratio);

    // Find and set the transform required to fit the original image
    // Compact version of above code
    QRectF sceneRect2 = transform().mapRect(QRectF({}, adjustedImageSize));
    qreal absoluteRatio =
            qMin(viewRect.width() / sceneRect2.width(), viewRect.height() / sceneRect2.height());

    absoluteTransform = QTransform::fromScale(absoluteRatio, absoluteRatio);

    // Scale and center on the center of \a rect.
    scale(xratio, yratio);
    centerOn(adjustedBoundingRect.center());

    // variables
    zoomBasis = transform();

    isOriginalSize = false;
    currentScale = 1.0;
    updateFilteringMode();
    zoomBasisScaleFactor = 1.0;
}

void QVGraphicsView::fitInViewMarginless(const QGraphicsItem *item)
{
    return fitInViewMarginless(item->sceneBoundingRect());
}

void QVGraphicsView::centerOn(const QPointF &pos)
{
#ifdef COCOA_LOADED
    int obscuredHeight = QVCocoaFunctions::getObscuredHeight(window()->windowHandle());
#else
    int obscuredHeight = 0;
#endif

    qreal width = viewport()->width();
    qreal height = viewport()->height() - obscuredHeight;
    QPointF viewPoint = transform().map(pos);

    if (isRightToLeft()) {
        qint64 horizontal = 0;
        horizontal += horizontalScrollBar()->minimum();
        horizontal += horizontalScrollBar()->maximum();
        horizontal -= int(viewPoint.x() - width / 2.0);
        horizontalScrollBar()->setValue(horizontal);
    } else {
        horizontalScrollBar()->setValue(int(viewPoint.x() - width / 2.0));
    }

    verticalScrollBar()->setValue(int(viewPoint.y() - obscuredHeight - (height / 2.0)));
}

void QVGraphicsView::centerOn(qreal x, qreal y)
{
    centerOn(QPointF(x, y));
}

void QVGraphicsView::centerOn(const QGraphicsItem *item)
{
    centerOn(item->sceneBoundingRect().center());
}

void QVGraphicsView::settingsUpdated()
{
    if (hasActiveCanvasItem())
        resetScale();
}

void QVGraphicsView::ensureVideoView()
{
    if (videoView)
        return;

    QElapsedTimer initializationTimer;
    initializationTimer.start();
    videoView = new QVVideoView(scene(), this);
    if (!layers.stack().isNeutral()) {
        auto *filterEffect = new QVFilterEffect();
        filterEffect->setLayerStack(layers.stack());
        filterEffect->setCompareOriginal(compareOriginal);
        videoView->graphicsItem()->setGraphicsEffect(filterEffect);
    }
    videoView->setLoopMode(loopMode);
    videoView->recordInitializationDuration(initializationTimer.elapsed());
    connect(videoView, &QVVideoView::nativeSizeChanged, this, [this](const QSizeF &size) {
        if (size.isEmpty())
            return;

        updateVideoCanvasSize(size.toSize());
    });
    connect(videoView, &QVVideoView::videoLoadedChanged, this, [this]() {
        if (videoCanvasActive && videoView->isLoaded()) {
            const QSize reportedSize = videoView->graphicsItem()->nativeSize().toSize();
            if (!reportedSize.isEmpty()) {
                videoView->graphicsItem()->setSize(reportedSize);
                updateVideoCanvasSize(reportedSize);
            }
        }
        emit fileChanged();
    });
    connect(videoView, &QVVideoView::playbackStateChanged, this,
            &QVGraphicsView::videoPlaybackStateChanged);
    connect(videoView, &QVVideoView::errorOccurred, this, &QVGraphicsView::videoErrorOccurred);
}

void QVGraphicsView::setCompareOriginal(bool enabled)
{
    if (compareOriginal == enabled) return;
    compareOriginal = enabled;
    updateLayerEffects();
}

void QVGraphicsView::updateLayerEffects()
{
    const auto updateEffect = [this](QGraphicsItem *item) {
        if (layers.stack().isNeutral()) {
            item->setGraphicsEffect(nullptr);
            return;
        }
        auto *effect = static_cast<QVFilterEffect *>(item->graphicsEffect());
        if (!effect) {
            effect = new QVFilterEffect();
            item->setGraphicsEffect(effect);
        }
        effect->setLayerStack(layers.stack());
        effect->setCompareOriginal(compareOriginal);
    };
    updateEffect(loadedPixmapItem);
    if (videoView) updateEffect(videoView->graphicsItem());
    viewport()->update();
}

void QVGraphicsView::activateImageCanvas()
{
    videoCanvasActive = false;
    if (videoView)
        videoView->graphicsItem()->hide();
    loadedPixmapItem->show();
}

void QVGraphicsView::activateVideoCanvas()
{
    videoCanvasActive = true;
    loadedPixmapItem->hide();
    if (videoView)
        videoView->graphicsItem()->show();
}

void QVGraphicsView::resetCanvasForNewMedia()
{
    expensiveScaleTimerNew->stop();
    resetTransform();
    horizontalScrollBar()->setValue(0);
    verticalScrollBar()->setValue(0);

    currentScale = 1.0;
    isOriginalSize = false;
    lastZoomEventPos = QPoint(-1, -1);
    lastZoomRoundingError = QPointF();
    lastScrollRoundingError = QPointF();
    absoluteTransform = QTransform();
    zoomBasis = QTransform();
    zoomBasisScaleFactor = 1.0;
    restoreCenterAfterExpensiveScale = false;
    canvasCenterRoundingError = QPointF();

    if (videoView) {
        QGraphicsVideoItem *item = videoView->graphicsItem();
        item->setPos(QPointF());
        item->setRotation(0);
        item->setTransform(QTransform());
        item->setTransformOriginPoint(QPointF());
    }
}

void QVGraphicsView::saveCanvasStateForNeighborNavigation()
{
    // Video dimensions arrive asynchronously. Keep the last visible framing
    // when another navigation supersedes a video that has not finished loading.
    if (navigationCanvasState.valid)
        return;

    navigationCanvasState.valid = hasActiveCanvasItem() && !currentMediaSize().isEmpty();
    if (!navigationCanvasState.valid)
        return;

    navigationCanvasState.mediaSize = orientedMediaSize();
    navigationCanvasState.normalizedCenter = normalizedCanvasCenter() + canvasCenterRoundingError;
    navigationCanvasState.viewportWidthRatio = canvasDisplayWidth() / viewport()->width();
    navigationCanvasState.originalSize = isOriginalSize;
    navigationCanvasState.rotation = videoCanvasActive
            ? (qRound(activeCanvasItem()->rotation()) % 360 + 360) % 360
            : imageCore.getCurrentRotation();
    navigationCanvasState.mirrored = transform().m11() < 0;
    navigationCanvasState.flipped = transform().m22() < 0;
}

bool QVGraphicsView::restoreCanvasStateForLoadedMedia()
{
    if (!navigationCanvasState.valid)
        return false;

    if (currentMediaSize().isEmpty()) {
        navigationCanvasState.valid = false;
        return false;
    }

    const NavigationCanvasState savedState = navigationCanvasState;
    navigationCanvasState.valid = false;
    const QSize newSize = orientedMediaSize();
    // Orientation is carried between neighbors just as it has always been for
    // images. Only pan/zoom restoration depends on a matching aspect ratio.
    setTransform(QTransform::fromScale(savedState.mirrored ? -1 : 1,
                                       savedState.flipped ? -1 : 1));
    // Exact rational equality, without floating-point aspect-ratio tolerances.
    if (qint64(savedState.mediaSize.width()) * newSize.height()
        != qint64(newSize.width()) * savedState.mediaSize.height())
        return false;

    resetScale();
    if (savedState.originalSize && savedState.mediaSize == newSize) {
        expensiveScaleTimerNew->stop();
        originalSize();
        scale(savedState.mirrored ? -1 : 1, savedState.flipped ? -1 : 1);
        zoomBasis = transform();
    } else {
        // Preserve the fraction of media visible, including when fit-to-view
        // stops at native size for one resolution but not the other.
        const qreal fittedWidth = canvasDisplayWidth();
        const qreal factor = savedState.viewportWidthRatio * viewport()->width() / fittedWidth;
        if (!qFuzzyCompare(factor, qreal(1.0)))
            zoom(factor);
    }

    centerOnNormalizedCanvasPoint(savedState.normalizedCenter);
    if (!videoCanvasActive && expensiveScaleTimerNew->isActive()) {
        expensiveScaleCenter = savedState.normalizedCenter;
        restoreCenterAfterExpensiveScale = true;
    }
    return true;
}

QSize QVGraphicsView::orientedMediaSize() const
{
    QSize size = currentMediaSize();
    if (videoCanvasActive && qRound(activeCanvasItem()->rotation()) % 180 != 0)
        size.transpose();
    return size;
}

qreal QVGraphicsView::canvasDisplayWidth() const
{
    if (videoCanvasActive)
        return transform().mapRect(activeCanvasItem()->sceneBoundingRect()).width();
    // Image pixmaps may have been resampled; use the logical scale to avoid
    // accumulating resampling-rounding errors over repeated navigation.
    return absoluteTransform.mapRect(QRectF(QPointF(), currentMediaSize())).width();
}

QPointF QVGraphicsView::normalizedCanvasCenter() const
{
    // Scene bounds include video rotation; image rotation is already baked into
    // pixels. This gives both representations the same oriented coordinates.
    const QRectF bounds = activeCanvasItem()->sceneBoundingRect();
    const QPointF itemCenter = viewportTransform().inverted().map(canvasViewportCenter());
    return QPointF((itemCenter.x() - bounds.left()) / bounds.width(),
                   (itemCenter.y() - bounds.top()) / bounds.height());
}

QPointF QVGraphicsView::canvasViewportCenter() const
{
    qreal obscuredHeight = 0;
#ifdef COCOA_LOADED
    obscuredHeight = QVCocoaFunctions::getObscuredHeight(window()->windowHandle());
#endif
    return QPointF(viewport()->width() / 2.0, (viewport()->height() + obscuredHeight) / 2.0);
}

void QVGraphicsView::centerOnNormalizedCanvasPoint(const QPointF &normalizedPoint)
{
    if (!hasActiveCanvasItem())
        return;

    const QRectF bounds = activeCanvasItem()->sceneBoundingRect();
    const QPointF itemPoint(bounds.left() + normalizedPoint.x() * bounds.width(),
                            bounds.top() + normalizedPoint.y() * bounds.height());
    const QPointF delta = viewportTransform().map(itemPoint)
            - canvasViewportCenter();
    horizontalScrollBar()->setValue(horizontalScrollBar()->value()
                                     + qRound(delta.x()) * (isRightToLeft() ? -1 : 1));
    verticalScrollBar()->setValue(verticalScrollBar()->value() + qRound(delta.y()));
    // Scroll bars store integers. Retain the subpixel remainder instead of
    // feeding the rounded position back into the next navigation snapshot.
    canvasCenterRoundingError = normalizedPoint - normalizedCanvasCenter();
}

void QVGraphicsView::updateVideoCanvasSize(const QSize &size)
{
    videoNativeSize = size;
    if (!videoCanvasActive || videoLayoutSize == size)
        return;

    videoLayoutSize = size;
    if (navigationCanvasState.valid) {
        QGraphicsItem *item = activeCanvasItem();
        item->setTransformOriginPoint(item->boundingRect().center());
        item->setRotation(navigationCanvasState.rotation);
    }
    emit updatedLoadedPixmapItem();
    if (!restoreCanvasStateForLoadedMedia())
        resetScale();
}

bool QVGraphicsView::hasActiveCanvasItem() const
{
    return videoCanvasActive ? videoView && !videoNativeSize.isEmpty()
                             : getImageDetails().isPixmapLoaded;
}

QGraphicsItem *QVGraphicsView::activeCanvasItem() const
{
    return videoCanvasActive && videoView
            ? static_cast<QGraphicsItem *>(videoView->graphicsItem())
            : static_cast<QGraphicsItem *>(loadedPixmapItem);
}

bool QVGraphicsView::isVideoLoaded() const
{
    return videoView && videoView->isLoaded();
}

bool QVGraphicsView::isVideoPlaying() const
{
    return videoView && videoView->isPlaying();
}

bool QVGraphicsView::isMediaLoaded() const
{
    return getImageDetails().isPixmapLoaded || isVideoLoaded();
}

QString QVGraphicsView::videoErrorString() const
{
    return videoView ? videoView->errorString() : QString();
}

QSize QVGraphicsView::currentMediaSize() const
{
    return videoCanvasActive ? videoNativeSize : getImageDetails().loadedPixmapSize;
}

void QVGraphicsView::reloadVideo()
{
    if (videoView)
        videoView->reloadFile();
}

void QVGraphicsView::closeVideo()
{
    if (!videoView)
        return;

    videoView->closeVideo();
    videoView->graphicsItem()->hide();
    videoNativeSize = QSize();
    videoLayoutSize = QSize();
}

void QVGraphicsView::toggleVideoPaused()
{
    if (videoView)
        videoView->togglePaused();
}

void QVGraphicsView::closeImage()
{
    ++mediaRequestGeneration;
    navigationCanvasState.valid = false;
    restoreCenterAfterExpensiveScale = false;
    closeVideo();
    activateImageCanvas();
    imageCore.closeImage();
}

void QVGraphicsView::jumpToNextFrame()
{
    if (videoCanvasActive) {
        if (videoView)
            videoView->stepFrame(1);
    } else {
        imageCore.jumpToNextFrame();
    }
}

void QVGraphicsView::jumpToPreviousFrame()
{
    if (videoCanvasActive) {
        if (videoView)
            videoView->stepFrame(-1);
    } else {
        imageCore.jumpToPreviousFrame();
    }
}

void QVGraphicsView::seekToPercent(int percent)
{
    if (videoCanvasActive) {
        if (videoView)
            videoView->seekToPercent(percent);
    } else {
        imageCore.seekToPercent(percent);
    }
}

void QVGraphicsView::setPaused(const bool &desiredState)
{
    imageCore.setPaused(desiredState);
}

void QVGraphicsView::setSpeed(const int &desiredSpeed)
{
    imageCore.setSpeed(desiredSpeed);
}

void QVGraphicsView::toggleVideoMuted()
{
    if (videoView)
        videoView->toggleMuted();
}

bool QVGraphicsView::isVideoMuted() const
{
    return videoView && videoView->isMuted();
}

int QVGraphicsView::videoPlaybackSpeed() const
{
    return videoView ? videoView->playbackSpeed() : 100;
}

void QVGraphicsView::setVideoPlaybackSpeed(int percent)
{
    if (videoView)
        videoView->setPlaybackSpeed(percent);
}

void QVGraphicsView::togglePlaybackLoopMode()
{
    const bool mediaLoopsByDefault = !videoCanvasActive
            && imageCore.currentAnimationLoopsByDefault();
    loopMode = nextManualLoopMode(loopMode, mediaLoopsByDefault);
    imageCore.setLoopMode(loopMode);
    if (videoView)
        videoView->setLoopMode(loopMode);
}

void QVGraphicsView::rotateImage(int rotation)
{
    if (!videoCanvasActive) {
        imageCore.rotateImage(rotation);
        return;
    }

    QGraphicsItem *item = activeCanvasItem();
    item->setTransformOriginPoint(item->boundingRect().center());
    item->setRotation(item->rotation() + rotation);
    resetScale();
}
