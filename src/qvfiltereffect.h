#ifndef QVFILTEREFFECT_H
#define QVFILTEREFFECT_H

#include "qvlayers.h"

#include <QGraphicsEffect>
#include <QPoint>
#include <QPixmap>

class QVFilterEffect : public QGraphicsEffect
{
public:
    explicit QVFilterEffect(QObject *parent = nullptr);

    void setCompareOriginal(bool enabled);
    void setLayerStack(const QVLayers::Stack &settings);
    // Still images retain native pixels independently of the view's display pixmap.
    void setSourcePixmap(const QPixmap &pixmap);
    void setSmoothScaling(bool enabled) { smoothScaling = enabled; }
    const QVLayers::Stack &layerStack() const { return settings; }
    quint64 compositionCount() const { return compositions; }
    QPixmap cachedComposite() const { return !cacheDirty && !nativeSource.isNull() ? cachedPixmap : QPixmap(); }
    bool restoreComposite(const QPixmap &composite, const QImage &source, const QVLayers::Stack &layers);

protected:
    void draw(QPainter *painter) override;
    QRectF boundingRectFor(const QRectF &rect) const override;
    void sourceChanged(ChangeFlags flags) override;

private:
    QVLayers::Stack settings;
    quint64 compositions = 0;
    bool compareOriginal = false;
    bool cacheDirty = true;
    bool sourceDirty = true;
    qint64 cachedSourceKey = 0;
    QPoint cachedOffset;
    QPointF canvasOffset;
    QPixmap cachedPixmap;
    QPixmap nativeSource;
    bool smoothScaling = true;
};

#endif // QVFILTEREFFECT_H
