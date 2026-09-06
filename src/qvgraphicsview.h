#ifndef QVGRAPHICSVIEW_H
#define QVGRAPHICSVIEW_H

#include "qvimagecore.h"
#include "qvplaybackloopmode.h"
#include "qvexport.h"
#include "qvlayers.h"
#include <QGraphicsView>
#include <QImageReader>
#include <QMimeData>
#include <QDir>
#include <QHash>
#include <QTimer>
#include <QFileInfo>

class QGraphicsItem;
class QVVideoView;

class QVGraphicsView : public QGraphicsView
{
    Q_OBJECT

public:
    QVGraphicsView(QWidget *parent = nullptr);
    ~QVGraphicsView() override;

    enum class ScaleMode { resetScale, zoom };
    Q_ENUM(ScaleMode)

    enum class GoToFileMode { constant, first, previous, next, last };
    Q_ENUM(GoToFileMode)

    QMimeData *getMimeData() const;
    void loadMimeData(const QMimeData *mimeData);
    void loadFile(const QString &fileName);

    void reloadFile();

    void zoomIn(const QPoint &pos = QPoint(-1, -1));

    void zoomOut(const QPoint &pos = QPoint(-1, -1));

    void zoom(qreal scaleFactor, const QPoint &pos = QPoint(-1, -1));

    void scaleExpensively();
    void makeUnscaled();

    void resetScale();
    void resetView();
    void originalSize();

    void goToFile(const GoToFileMode &mode, int index = 0);

    void settingsUpdated();

    void closeImage();
    void jumpToNextFrame();
    void jumpToPreviousFrame();
    void seekToPercent(int percent);
    void setPaused(const bool &desiredState);
    void setSpeed(const int &desiredSpeed);
    void rotateImage(int rotation);

    bool isVideoLoaded() const;
    bool isVideoPlaying() const;
    bool isMediaLoaded() const;
    QString videoErrorString() const;
    QSize currentMediaSize() const;
    QVExport::Source exportSource() const;
    void setCompareOriginal(bool enabled);
    bool canDistort() const;
    void setDistortActive(bool active);
    void setDistortRadius(int radius);
    void undoDistort();
    void redoDistort();
    void setDistortLayer(quint64 id) { selectedDistortLayer = id; }
    bool isDistortActive() const { return distortActive; }
    bool isCropActive() const { return cropActive; }
    void setCropActive(bool active);
    void applyCrop();
    void resetCrop();
    bool setCropDraft(const QRectF &rect);
    QRectF cropDraftRect() const { return cropDraft; }
    bool isComparingOriginal() const { return compareOriginal; }
    QVLayerModel *layerModel() { return &layers; }
    void reloadVideo();
    void closeVideo();
    void toggleVideoPaused();
    void toggleVideoMuted();
    bool isVideoMuted() const;
    int videoPlaybackSpeed() const;
    void setVideoPlaybackSpeed(int percent);
    void togglePlaybackLoopMode();
    bool isLoopingForced() const { return loopMode == QVPlaybackLoopMode::ForceLoop; }

    const QVMediaCatalog::State &getCurrentMedia() const { return imageCore.getCurrentMedia(); }
    const QVImageCore::FileDetails &getImageDetails() const { return imageCore.getImageDetails(); }
    const QPixmap &getLoadedPixmap() const { return imageCore.getLoadedPixmap(); }
    const QMovie &getLoadedMovie() const { return imageCore.getLoadedMovie(); }

signals:
    void cancelSlideshow();

    void fileChanged();

    void updatedLoadedPixmapItem();

    void videoPlaybackStateChanged();
    void videoErrorOccurred();
    void fullscreenRequested();
    void distortLayerCreated(quint64 id);
    void distortRadiusChanged(int radius);
    void cropActiveChanged(bool active);

protected:
    void wheelEvent(QWheelEvent *event) override;

    void resizeEvent(QResizeEvent *event) override;

    void dropEvent(QDropEvent *event) override;

    void dragEnterEvent(QDragEnterEvent *event) override;

    void dragMoveEvent(QDragMoveEvent *event) override;

    void dragLeaveEvent(QDragLeaveEvent *event) override;

#if QT_VERSION < QT_VERSION_CHECK(6, 0, 0)
    void enterEvent(QEvent *event) override;
#else
    void enterEvent(QEnterEvent *event) override;
#endif

    void mousePressEvent(QMouseEvent *event) override;

    void mouseDoubleClickEvent(QMouseEvent *event) override;

    void mouseMoveEvent(QMouseEvent *event) override;

    void mouseReleaseEvent(QMouseEvent *event) override;

    bool event(QEvent *event) override;
    void drawForeground(QPainter *painter, const QRectF &rect) override;
    void drawBackground(QPainter *painter, const QRectF &rect) override;
    void leaveEvent(QEvent *event) override;

    void fitInViewMarginless(const QRectF &rect);
    void fitInViewMarginless(const QGraphicsItem *item);

    void centerOn(const QPointF &pos);

    void centerOn(qreal x, qreal y);

    void centerOn(const QGraphicsItem *item);

private slots:
    void animatedFrameChanged(QRect rect);

    void postLoad();

    void updateLoadedPixmapItem();

private:
    struct NavigationCanvasState
    {
        bool valid = false;
        QSize mediaSize;
        QPointF normalizedCenter;
        qreal viewportWidthRatio = 1.0;
        bool originalSize = false;
        int rotation = 0;
        bool mirrored = false;
        bool flipped = false;
    };

    void updateFilteringMode();
    void activateImageCanvas();
    void activateVideoCanvas();
    void resetCanvasForNewMedia();
    void saveCanvasStateForNeighborNavigation();
    bool restoreCanvasStateForLoadedMedia();
    QPointF normalizedCanvasCenter() const;
    QSize orientedMediaSize() const;
    qreal canvasDisplayWidth() const;
    QPointF canvasViewportCenter() const;
    void centerOnNormalizedCanvasPoint(const QPointF &normalizedPoint);
    void updateVideoCanvasSize(const QSize &size);
    void ensureVideoView();
    bool hasActiveCanvasItem() const;
    QGraphicsItem *activeCanvasItem() const;

    QGraphicsPixmapItem *loadedPixmapItem;
    QVVideoView *videoView;
    QSize videoNativeSize;
    QSize videoLayoutSize;
    quint64 mediaRequestGeneration;
    bool videoCanvasActive;
    bool loadingNeighbor;
    bool restoreCenterAfterExpensiveScale;
    QPointF expensiveScaleCenter;
    QPointF canvasCenterRoundingError;
    NavigationCanvasState navigationCanvasState;
    QVPlaybackLoopMode loopMode;
    struct SessionEdits {
        QVLayers::Stack layers;
        int rotation = 0;
        bool mirrored = false;
        bool flipped = false;
    };
    QHash<QString, SessionEdits> sessionEdits;
    void saveSessionEdits();
    void restoreSessionEdits(const QString &path);
    bool cropActive = false;
    bool cropDragging = false;
    bool cropMoving = false;
    Qt::Edges cropEdges;
    QRectF cropDraft{0, 0, 1, 1};
    QRectF cropDragRect;
    QPoint cropDragOrigin;
    QRectF cropScreenRect() const;
    Qt::Edges cropEdgesAt(const QPoint &position) const;
    void moveCrop(const QPoint &position);
    QRectF canvasSceneRect() const;
    bool distortActive = false;
    bool distortDragging = false;
    bool distortHover = false;
    int distortRadius = 48;
    quint64 selectedDistortLayer = 0;
    quint64 strokeLayer = 0;
    QPoint distortPosition; // Viewport coordinates: the brush stays with the cursor during zoom.
    QPointF strokeStart;
    double strokeRadius = 0.1;
    QString distortSource;
    QPointF distortPoint(const QPoint &position) const;
    void continueDistort(const QPoint &position);
    bool compareOriginal = false;
    QVLayerModel layers{ this };
    void updateLayerEffects();

    constexpr static int MARGIN = -2;
    constexpr static qreal MAX_EXPENSIVE_SCALING_SIZE = 3;

    // Set to too high a value to activate for now...
    constexpr static qreal MAX_FILTERING_SIZE = 5000;

    qreal currentScale;
    QSize scaledSize;
    bool isOriginalSize;
    QPoint lastZoomEventPos;
    QPointF lastZoomRoundingError;
    QPointF lastScrollRoundingError;

    QTransform absoluteTransform;
    QTransform zoomBasis;
    qreal zoomBasisScaleFactor;

    QVImageCore imageCore{ this };

    QTimer *expensiveScaleTimerNew;
    QPointF centerPoint;
    Qt::MouseButton mousePressButton;
    Qt::KeyboardModifiers mousePressModifiers;
    QPoint mousePressPosition;
};
#endif // QVGRAPHICSVIEW_H
