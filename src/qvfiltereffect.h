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
    const QVLayers::Stack &layerStack() const { return settings; }

protected:
    void draw(QPainter *painter) override;
    void sourceChanged(ChangeFlags flags) override;

private:
    QVLayers::Stack settings;
    bool compareOriginal = false;
    bool cacheDirty = true;
    QPoint cachedOffset;
    QPixmap cachedPixmap;
};

#endif // QVFILTEREFFECT_H
