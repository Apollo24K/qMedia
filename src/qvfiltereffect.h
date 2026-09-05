#ifndef QVFILTEREFFECT_H
#define QVFILTEREFFECT_H

#include "qvfilters.h"

#include <QGraphicsEffect>
#include <QPoint>
#include <QPixmap>

class QVFilterEffect : public QGraphicsEffect
{
public:
    explicit QVFilterEffect(QObject *parent = nullptr);

    void setFilterSettings(const QVFilters::Settings &settings);
    const QVFilters::Settings &filterSettings() const { return settings; }

protected:
    void draw(QPainter *painter) override;
    void sourceChanged(ChangeFlags flags) override;

private:
    QVFilters::Settings settings;
    bool cacheDirty = true;
    QPoint cachedOffset;
    QPixmap cachedPixmap;
};

#endif // QVFILTEREFFECT_H
