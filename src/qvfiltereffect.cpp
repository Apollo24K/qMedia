#include "qvfiltereffect.h"

#include <QPainter>
#include <QPixmap>

QVFilterEffect::QVFilterEffect(QObject *parent) : QGraphicsEffect(parent) { }

void QVFilterEffect::setLayerStack(const QVLayers::Stack &newSettings)
{
    if (settings.samePixels(newSettings))
        return;
    settings = newSettings;
    cacheDirty = true;
    update();
}

void QVFilterEffect::setCompareOriginal(bool enabled)
{
    if (compareOriginal == enabled) return;
    compareOriginal = enabled;
    update();
}

void QVFilterEffect::sourceChanged(ChangeFlags flags)
{
    cacheDirty = true;
    QGraphicsEffect::sourceChanged(flags);
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
        cachedPixmap = QPixmap::fromImage(QVLayers::apply(source.toImage(), settings));
        cachedPixmap.setDevicePixelRatio(source.devicePixelRatio());
        cacheDirty = false;
    }
    painter->drawPixmap(cachedOffset, cachedPixmap);
}
