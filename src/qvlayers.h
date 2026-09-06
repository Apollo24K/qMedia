#ifndef QVLAYERS_H
#define QVLAYERS_H

#include "qvfilters.h"
#include <QObject>
#include <QPointF>
#include <QRectF>

// Layer order is visual: the first entry is on top. Source entries refer to the
// current decoded frame, so duplicating video never creates another player.
namespace QVLayers {
enum class Kind { Source, Filter, Distort };
enum class Blend { Normal, Multiply, Screen, Overlay, Darken, Lighten };

// Coordinates are normalized to the unrotated image; radius uses its shorter side.
struct DistortStroke {
    QVector<QPointF> points;
    double radius = 0.1;
    bool operator==(const DistortStroke &other) const {
        return radius == other.radius && points == other.points;
    }
};
QPointF rotatePoint(QPointF point, int degrees);
QRectF rotateRect(const QRectF &rect, int degrees);
// Empty return means an invalid or excessively large canvas allocation.
QRect canvasPixels(const QSize &sourceSize, const QRectF &rect);

struct Layer {
    quint64 id = 0;
    Kind kind = Kind::Source;
    QString name;
    bool visible = true;
    int strength = 100;
    Blend blend = Blend::Normal;
    QVFilters::Layer filter;
    QVector<DistortStroke> strokes;
    QVector<DistortStroke> redoStrokes;
    bool samePixels(const Layer &other) const;
};

struct Stack {
    QVector<Layer> layers{ Layer() };
    QRectF canvas{0, 0, 1, 1};
    bool hasCanvas() const { return canvas != QRectF(0, 0, 1, 1); }
    bool isNeutral() const;
    bool hasDistortion() const;
    bool samePixels(const Stack &other) const;
};

Stack rotated(const Stack &stack, int degrees);
Stack fromFilters(const QVFilters::Settings &filters);
QImage apply(const QImage &source, const Stack &stack);
QString ffmpegFilter(const Stack &stack);
}

// One model per canvas; the HUD and renderer observe the same state.
class QVLayerModel : public QObject
{
    Q_OBJECT
public:
    explicit QVLayerModel(QObject *parent = nullptr);
    const QVLayers::Stack &stack() const { return current; }
    int indexOf(quint64 id) const;
    quint64 addFilter(int above = 0);
    quint64 addDistort(int above = 0);
    void clearDistortions();
    void undoDistort(quint64 id);
    void redoDistort(quint64 id);
    quint64 duplicate(quint64 id);
    bool remove(quint64 id);
    bool move(quint64 id, int index);
    void update(const QVLayers::Layer &layer);
    void setStack(const QVLayers::Stack &stack);
    void setCanvas(const QRectF &rect);
signals:
    void changed();
    void pixelsChanged();
private:
    QVLayers::Stack current;
    quint64 nextId = 1;
};

#endif
