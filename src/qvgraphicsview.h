#ifndef QVGRAPHICSVIEW_H
#define QVGRAPHICSVIEW_H

#include "qvimagecore.h"
#include "qvplaybackloopmode.h"
#include "qvexport.h"
#include <QGraphicsView>
#include <QImageReader>
#include <QMimeData>
#include <QDir>
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
