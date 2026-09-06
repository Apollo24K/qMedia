#include "qvfiltereffect.h"

#include <QPainter>
#include <QPixmap>
#include <QColorSpace>

QVFilterEffect::QVFilterEffect(QObject *parent) : QGraphicsEffect(parent) { }

bool QVFilterEffect::restoreComposite(const QPixmap &composite, const QImage &source,
                                      const QVLayers::Stack &layers)
{
    if (composite.isNull() || nativeSource.isNull() || !settings.samePixels(layers)) return false;
    const QImage current = nativeSource.toImage();
    // Decoding creates new pixmap keys. Compare actual pixels and color space so
    // reloads, replaced files, and color-management changes cannot reuse stale edits.
    if (source.colorSpace() != current.colorSpace() || source != current) return false;
    cachedPixmap = composite;
    cachedSourceKey = nativeSource.cacheKey();
    cacheDirty = false;
    sourceDirty = false;
    update();
    return true;
}

void QVFilterEffect::setSourcePixmap(const QPixmap &pixmap)
{
    if (nativeSource.cacheKey() == pixmap.cacheKey()) return;
    nativeSource = pixmap;
    cacheDirty = true;
    sourceDirty = true;
    update();
}

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
    sourceDirty = true;
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
    const bool fixedSource = !nativeSource.isNull();
    const auto drawNative = [&](const QPixmap &pixmap, bool cropped) {
        const QRectF sourceRect = sourceBoundingRect(Qt::LogicalCoordinates);
        const QRectF target = cropped ? boundingRectFor(sourceRect) : sourceRect;
        painter->save();
        painter->setRenderHint(QPainter::SmoothPixmapTransform, smoothScaling);
        painter->drawPixmap(target, pixmap, QRectF(pixmap.rect()));
        painter->restore();
    };
    if (compareOriginal || settings.isNeutral()) {
        if (fixedSource) drawNative(nativeSource, false);
        else drawSource(painter);
        return;
    }

    // A native still-image composite is independent of viewport movement and
    // display-pixmap resampling. Only actual pixel edits invalidate it.
    if (cacheDirty || cachedPixmap.isNull()
            || (!fixedSource && (sourceDirty || sourceIsPixmap()))) {
        const QPixmap source = fixedSource ? nativeSource
                : sourcePixmap(Qt::LogicalCoordinates, &cachedOffset, NoPad);
        if (source.isNull()) return;
        sourceDirty = false;
        if (cacheDirty || cachedPixmap.isNull() || source.cacheKey() != cachedSourceKey) {
            cachedSourceKey = source.cacheKey();
            const QRect bounds = QVLayers::canvasPixels(source.size(), settings.canvas);
            canvasOffset = settings.hasCanvas() ? QPointF(bounds.topLeft()) / source.devicePixelRatio() : QPointF();
            ++compositions;
            cachedPixmap = QPixmap::fromImage(QVLayers::apply(source.toImage(), settings));
            cachedPixmap.setDevicePixelRatio(source.devicePixelRatio());
            cacheDirty = false;
        }
    }
    if (fixedSource) drawNative(cachedPixmap, true);
    else painter->drawPixmap(QPointF(cachedOffset) + canvasOffset, cachedPixmap);
}
