#include "qvfiltereffect.h"

#include <QPainter>
#include <QPixmap>

QVFilterEffect::QVFilterEffect(QObject *parent) : QGraphicsEffect(parent) { }

void QVFilterEffect::setLayerStack(const QVLayers::Stack &newSettings)
{
    if (settings.samePixels(newSettings))
        return;
    bool appended = false;
    if (!cacheDirty && !cachedPixmap.isNull() && !settings.hasCanvas() && !newSettings.hasCanvas()
            && settings.layers.size() == newSettings.layers.size()
            && !settings.layers.isEmpty()) {
        const auto &old = settings.layers[0];
        const auto &next = newSettings.layers[0];
        auto base = next;
        base.strokes = old.strokes;
        bool compatible = old.kind == QVLayers::Kind::Distort && old.visible
                && old.strength == 100 && old.blend == QVLayers::Blend::Normal
                && old.samePixels(base) && next.strokes.size() >= old.strokes.size();
        for (int i = 1; compatible && i < settings.layers.size(); ++i)
            compatible = settings.layers[i].samePixels(newSettings.layers[i]);
        QVLayers::Layer tail = next;
        tail.strokes.clear();
        for (int i = 0; compatible && i < old.strokes.size(); ++i) {
            const auto &a = old.strokes[i];
            const auto &b = next.strokes[i];
            compatible = a.radius == b.radius && b.points.size() >= a.points.size();
            for (int point = 0; compatible && point < a.points.size(); ++point)
                compatible = a.points[point] == b.points[point];
            if (compatible && b.points.size() > a.points.size()) {
                // Only the last stroke may grow; earlier changes require a full render.
                compatible = i == old.strokes.size() - 1 && !a.points.isEmpty();
                if (compatible) {
                    auto extra = b;
                    extra.points = b.points.mid(a.points.size() - 1);
                    tail.strokes.append(extra);
                }
            }
        }
        if (compatible) {
            for (int i = old.strokes.size(); i < next.strokes.size(); ++i)
                tail.strokes.append(next.strokes[i]);
            QVLayers::Stack delta;
            delta.layers.prepend(tail);
            const qreal dpr = cachedPixmap.devicePixelRatio();
            cachedPixmap = QPixmap::fromImage(QVLayers::apply(cachedPixmap.toImage(), delta));
            cachedPixmap.setDevicePixelRatio(dpr);
            appended = true;
        }
    }
    settings = newSettings;
    cacheDirty = !appended;
    updateBoundingRect();
    update();
}

void QVFilterEffect::setCompareOriginal(bool enabled)
{
    if (compareOriginal == enabled) return;
    compareOriginal = enabled;
    updateBoundingRect();
    update();
}

void QVFilterEffect::sourceChanged(ChangeFlags flags)
{
    cacheDirty = true;
    QGraphicsEffect::sourceChanged(flags);
}

QRectF QVFilterEffect::boundingRectFor(const QRectF &rect) const
{
    if (compareOriginal || !settings.hasCanvas()) return rect;
    const auto &canvas = settings.canvas;
    return QRectF(rect.x() + canvas.x()*rect.width(), rect.y() + canvas.y()*rect.height(),
                  canvas.width()*rect.width(), canvas.height()*rect.height());
}

void QVFilterEffect::draw(QPainter *painter)
{
    if (compareOriginal || settings.isNeutral()) {
        drawSource(painter);
        return;
    }

    if (cacheDirty || cachedPixmap.isNull()) {
        const QPixmap source = sourcePixmap(Qt::LogicalCoordinates, &cachedOffset, NoPad);
        if (source.isNull())
            return;
        const QRect bounds = QVLayers::canvasPixels(source.size(), settings.canvas);
        canvasOffset = settings.hasCanvas() ? QPointF(bounds.topLeft()) / source.devicePixelRatio() : QPointF();
        cachedPixmap = QPixmap::fromImage(QVLayers::apply(source.toImage(), settings));
        cachedPixmap.setDevicePixelRatio(source.devicePixelRatio());
        cacheDirty = false;
    }
    painter->drawPixmap(QPointF(cachedOffset) + canvasOffset, cachedPixmap);
}
