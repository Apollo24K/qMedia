#include "qvlayers.h"
#include <QtMath>
#include <QLineF>

namespace {
double blendChannel(double base, double top, QVLayers::Blend blend)
{
    using QVLayers::Blend;
    switch (blend) {
    case Blend::Multiply: return base * top / 255.0;
    case Blend::Screen: return 255.0 - (255.0 - base) * (255.0 - top) / 255.0;
    case Blend::Overlay: return base < 127.5 ? 2.0 * base * top / 255.0
                         : 255.0 - 2.0 * (255.0 - base) * (255.0 - top) / 255.0;
    case Blend::Darken: return qMin(base, top);
    case Blend::Lighten: return qMax(base, top);
    default: return top;
    }
}

QString blendExpression(const QString &base, const QString &top, QVLayers::Blend blend)
{
    using QVLayers::Blend;
    switch (blend) {
    case Blend::Multiply: return QString("(%1)*(%2)/255").arg(base, top);
    case Blend::Screen: return QString("255-(255-(%1))*(255-(%2))/255").arg(base, top);
    case Blend::Overlay: return QString("if(lt(%1,127.5),2*(%1)*(%2)/255,255-2*(255-(%1))*(255-(%2))/255)").arg(base, top);
    case Blend::Darken: return QString("min(%1,%2)").arg(base, top);
    case Blend::Lighten: return QString("max(%1,%2)").arg(base, top);
    default: return top;
    }
}

int channel(double value) { return qBound(0, qRound(value), 255); }

void pushPixels(QImage &image, QPointF from, QPointF to, double radius)
{
    const QPointF delta = (to - from) * 0.65;
    if (radius <= 0 || delta.isNull()) return;
    const QRect affected = QRectF(to - QPointF(radius, radius), QSizeF(2 * radius, 2 * radius))
            .toAlignedRect().intersected(image.rect());
    if (affected.isEmpty()) return;
    const QRect sampleRect = QRectF(affected).united(QRectF(affected).translated(-delta))
            .adjusted(-1, -1, 1, 1).toAlignedRect().intersected(image.rect());
    const QImage before = image.copy(sampleRect);
    const double inverseRadiusSquared = 1.0 / (radius * radius);
    for (int y = affected.top(); y <= affected.bottom(); ++y) {
        auto *line = reinterpret_cast<QRgb *>(image.scanLine(y));
        for (int x = affected.left(); x <= affected.right(); ++x) {
            const double distance = ((x - to.x()) * (x - to.x()) + (y - to.y()) * (y - to.y()))
                    * inverseRadiusSquared;
            if (distance >= 1) continue;
            const double weight = (1 - distance) * (1 - distance);
            const double sx = qBound(0.0, x - delta.x() * weight - sampleRect.x(), double(before.width() - 1));
            const double sy = qBound(0.0, y - delta.y() * weight - sampleRect.y(), double(before.height() - 1));
            const int x0 = int(sx), y0 = int(sy);
            const int x1 = qMin(x0 + 1, before.width() - 1), y1 = qMin(y0 + 1, before.height() - 1);
            const double fx = sx - x0, fy = sy - y0;
            const QRgb pixels[] = { before.pixel(x0, y0), before.pixel(x1, y0),
                                    before.pixel(x0, y1), before.pixel(x1, y1) };
            const double weights[] = { (1-fx)*(1-fy), fx*(1-fy), (1-fx)*fy, fx*fy };
            double a = 0, r = 0, g = 0, b = 0;
            for (int n = 0; n < 4; ++n) {
                const double alpha = qAlpha(pixels[n]) * weights[n];
                a += alpha;
                r += qRed(pixels[n]) * alpha;
                g += qGreen(pixels[n]) * alpha;
                b += qBlue(pixels[n]) * alpha;
            }
            line[x] = a > 0 ? qRgba(channel(r/a), channel(g/a), channel(b/a), channel(a)) : 0;
        }
    }
}

QImage distort(const QImage &source, const QVector<QVLayers::DistortStroke> &strokes)
{
    QImage result = source;
    result.detach();
    for (const auto &stroke : strokes) {
        const double radius = stroke.radius * qMin(source.width(), source.height());
        if (!qIsFinite(radius) || radius <= 0) continue;
        for (int i = 1; i < stroke.points.size(); ++i) {
            const auto pixelPoint = [&](QPointF point) {
                return QPointF(point.x() * (source.width() - 1), point.y() * (source.height() - 1));
            };
            const QPointF from = pixelPoint(stroke.points[i - 1]), to = pixelPoint(stroke.points[i]);
            const double length = QLineF(from, to).length();
            if (!qIsFinite(length)) continue;
            const int steps = qMax(1, int(qCeil(length / qMax(1.0, radius * 0.25))));
            for (int step = 0; step < steps; ++step)
                pushPixels(result, from + (to-from) * (double(step)/steps),
                           from + (to-from) * (double(step+1)/steps), radius);
        }
    }
    return result;
}
}

bool QVLayers::Layer::samePixels(const Layer &other) const
{
    return kind == other.kind && visible == other.visible && strength == other.strength
            && blend == other.blend && (kind != Kind::Filter || filter == other.filter)
            && (kind != Kind::Distort || strokes == other.strokes);
}

bool QVLayers::Stack::samePixels(const Stack &other) const
{
    if (layers.size() != other.layers.size()) return false;
    for (int i = 0; i < layers.size(); ++i)
        if (!layers[i].samePixels(other.layers[i])) return false;
    return true;
}

bool QVLayers::Stack::isNeutral() const
{
    bool source = false;
    for (const auto &layer : layers) {
        if (!layer.visible || layer.strength == 0) continue;
        if (layer.kind == Kind::Filter) {
            if (!layer.filter.isNeutral() || layer.blend != Blend::Normal) return false;
        } else if (layer.kind == Kind::Distort) {
            if (!layer.strokes.isEmpty()) return false;
        } else {
            if (source || layer.strength != 100 || layer.blend != Blend::Normal) return false;
            source = true;
        }
    }
    return source;
}

QPointF QVLayers::rotatePoint(QPointF point, int degrees)
{
    switch ((degrees % 360 + 360) % 360) {
    case 90: return { 1.0 - point.y(), point.x() };
    case 180: return { 1.0 - point.x(), 1.0 - point.y() };
    case 270: return { point.y(), 1.0 - point.x() };
    default: return point;
    }
}

QVLayers::Stack QVLayers::rotated(const Stack &stack, int degrees)
{
    Stack result = stack;
    if (degrees % 360 == 0 || !stack.hasDistortion()) return result;
    for (auto &layer : result.layers)
        if (layer.kind == Kind::Distort)
            for (auto &stroke : layer.strokes)
                for (auto &point : stroke.points) point = rotatePoint(point, degrees);
    return result;
}

bool QVLayers::Stack::hasDistortion() const
{
    for (const auto &layer : layers)
        if (layer.kind == Kind::Distort && layer.visible && layer.strength > 0
                && !layer.strokes.isEmpty()) return true;
    return false;
}

QVLayers::Stack QVLayers::fromFilters(const QVFilters::Settings &filters)
{
    Stack stack;
    for (const auto &filter : filters.layers) {
        Layer layer;
        layer.kind = Kind::Filter;
        layer.filter = filter;
        stack.layers.prepend(layer);
    }
    return stack;
}

QImage QVLayers::apply(const QImage &source, const Stack &stack)
{
    if (source.isNull() || stack.isNeutral()) return source;
    const QImage original = source.convertToFormat(QImage::Format_ARGB32);
    QImage result = original;
    result.fill(Qt::transparent);
    bool empty = true;
    for (int i = stack.layers.size() - 1; i >= 0; --i) {
        const auto &layer = stack.layers[i];
        if (!layer.visible || layer.strength <= 0) continue;
        if (layer.kind == Kind::Distort && layer.strokes.isEmpty()) continue;
        if (layer.kind == Kind::Filter && layer.filter.isNeutral()
                && layer.blend == Blend::Normal) continue;
        if (layer.kind == Kind::Source && empty && layer.strength == 100) {
            result = original;
            empty = false;
            continue;
        }
        QImage top = original;
        if (layer.kind == Kind::Filter) {
            QVFilters::Settings settings;
            settings.layers = { layer.filter };
            top = QVFilters::apply(result, settings);
        } else if (layer.kind == Kind::Distort) {
            top = distort(result, layer.strokes);
        }
        if (layer.kind != Kind::Source) {
            if (layer.strength == 100 && layer.blend == Blend::Normal) {
                result = top;
                continue;
            }
        }
        empty = false;
        const double strength = qBound(0, layer.strength, 100) / 100.0;
        for (int y = 0; y < result.height(); ++y) {
            auto *dst = reinterpret_cast<QRgb *>(result.scanLine(y));
            const auto *src = reinterpret_cast<const QRgb *>(top.constScanLine(y));
            for (int x = 0; x < result.width(); ++x) {
                const double base[] = { double(qRed(dst[x])), double(qGreen(dst[x])), double(qBlue(dst[x])) };
                const double upper[] = { double(qRed(src[x])), double(qGreen(src[x])), double(qBlue(src[x])) };
                double color[3];
                double alpha;
                if (layer.kind != Kind::Source) {
                    for (int c = 0; c < 3; ++c)
                        color[c] = base[c] + strength * (blendChannel(base[c], upper[c], layer.blend) - base[c]);
                    alpha = qAlpha(dst[x]) + strength * (qAlpha(src[x]) - qAlpha(dst[x]));
                } else {
                    const double ab = qAlpha(dst[x]) / 255.0;
                    const double as = qAlpha(src[x]) / 255.0 * strength;
                    const double ao = as + ab * (1.0 - as);
                    for (int c = 0; c < 3; ++c)
                        color[c] = ao == 0 ? 0 : (as * (1 - ab) * upper[c]
                                + as * ab * blendChannel(base[c], upper[c], layer.blend)
                                + (1 - as) * ab * base[c]) / ao;
                    alpha = ao * 255;
                }
                dst[x] = qRgba(channel(color[0]), channel(color[1]), channel(color[2]), channel(alpha));
            }
        }
    }
    return result;
}

QString QVLayers::ffmpegFilter(const Stack &stack)
{
    // Distortion is currently a still-image tool; whole-media export rejects it.
    if (stack.hasDistortion()) return {};
    if (stack.isNeutral()) return {};
    // geq evaluates each output channel independently. Registers 0..3 retain
    // the original frame; 4..7 hold the composite and 8..9 are scratch space.
    // FFmpeg expressions provide exactly ten registers.
    // This keeps the expression linear in layer count, including source copies.
    QString program = "st(0,r(X,Y));st(1,g(X,Y));st(2,b(X,Y));st(3,alpha(X,Y));"
                      "st(4,0);st(5,0);st(6,0);st(7,0);";
    for (int i = stack.layers.size() - 1; i >= 0; --i) {
        const auto &layer = stack.layers[i];
        if (!layer.visible || layer.strength <= 0) continue;
        if (layer.kind == Kind::Distort) continue;
        const QString strength = QString::number(qBound(0, layer.strength, 100) / 100.0, 'f', 8);
        if (layer.kind == Kind::Filter) {
            const auto expressions = QVFilters::channelExpressions(layer.filter,
                                      { "ld(4)", "ld(5)", "ld(6)", "ld(7)" });
            for (int c = 0; c < 2; ++c)
                program += QString("st(%1,round(%2));").arg(c + 8).arg(expressions[c]);
            for (int c : { 2, 0, 1, 3 }) {
                const QString base = QString("ld(%1)").arg(c + 4);
                const QString upper = c < 2 ? QString("ld(%1)").arg(c + 8)
                                            : QString("round(%1)").arg(expressions[c]);
                const QString mixed = c == 3 ? upper : blendExpression(base, upper, layer.blend);
                program += QString("st(%1,round(%2+%3*((%4)-%2)));").arg(c + 4).arg(base, strength, mixed);
            }
        } else {
            program += QString("st(8,ld(7)/255);st(9,ld(3)/255*%1);").arg(strength);
            const QString alpha = "(ld(9)+ld(8)*(1-ld(9)))";
            for (int c = 0; c < 3; ++c) {
                const QString base = QString("ld(%1)").arg(c + 4);
                const QString upper = QString("ld(%1)").arg(c);
                const QString mixed = blendExpression(base, upper, layer.blend);
                program += QString("st(%1,round(if(eq(%5,0),0,(ld(9)*(1-ld(8))*%2+ld(9)*ld(8)*(%3)+(1-ld(9))*ld(8)*%4)/%5)));")
                                   .arg(c + 4).arg(upper, mixed, base, alpha);
            }
            program += QString("st(7,round(%1*255));").arg(alpha);
        }
    }
    return QString("format=rgba,geq=r='%1ld(4)':g='%1ld(5)':b='%1ld(6)':a='%1ld(7)':interpolation=nearest").arg(program);
}

QVLayerModel::QVLayerModel(QObject *parent) : QObject(parent) { setStack(current); }

int QVLayerModel::indexOf(quint64 id) const
{
    for (int i = 0; i < current.layers.size(); ++i)
        if (current.layers[i].id == id) return i;
    return -1;
}

void QVLayerModel::setStack(const QVLayers::Stack &stack)
{
    const bool pixels = !current.samePixels(stack);
    current = stack;
    if (current.layers.isEmpty()) current.layers.append(QVLayers::Layer());
    for (auto &layer : current.layers) {
        layer.id = nextId++;
        layer.strength = qBound(0, layer.strength, 100);
        if (layer.name.isEmpty()) layer.name = layer.kind == QVLayers::Kind::Source ? tr("Source")
                : layer.kind == QVLayers::Kind::Distort ? tr("Distort") : tr("Filter");
    }
    emit changed();
    if (pixels) emit pixelsChanged();
}

quint64 QVLayerModel::addFilter(int above)
{
    QVLayers::Layer layer;
    layer.id = nextId++;
    layer.kind = QVLayers::Kind::Filter;
    layer.name = tr("Filter");
    current.layers.insert(qBound(0, above, int(current.layers.size())), layer);
    emit changed();
    return layer.id;
}

quint64 QVLayerModel::addDistort(int above)
{
    QVLayers::Layer layer;
    layer.id = nextId++;
    layer.kind = QVLayers::Kind::Distort;
    layer.name = tr("Distort");
    current.layers.insert(qBound(0, above, int(current.layers.size())), layer);
    emit changed();
    return layer.id;
}

void QVLayerModel::undoDistort(quint64 id)
{
    const int index = indexOf(id);
    if (index < 0 || current.layers[index].kind != QVLayers::Kind::Distort
            || current.layers[index].strokes.isEmpty()) return;
    auto layer = current.layers[index];
    layer.strokes.removeLast();
    update(layer);
}

void QVLayerModel::clearDistortions()
{
    bool changedStack = false;
    for (int i = current.layers.size() - 1; i >= 0; --i)
        if (current.layers[i].kind == QVLayers::Kind::Distort) {
            current.layers.removeAt(i);
            changedStack = true;
        }
    if (changedStack) { emit changed(); emit pixelsChanged(); }
}

quint64 QVLayerModel::duplicate(quint64 id)
{
    const int index = indexOf(id);
    if (index < 0) return 0;
    auto copy = current.layers[index];
    copy.id = nextId++;
    copy.name = tr("%1 copy").arg(copy.name);
    current.layers.insert(index, copy);
    emit changed();
    emit pixelsChanged();
    return copy.id;
}

bool QVLayerModel::remove(quint64 id)
{
    const int index = indexOf(id);
    if (index < 0 || current.layers.size() == 1) return false;
    if (current.layers[index].kind == QVLayers::Kind::Source) {
        int sources = 0;
        for (const auto &layer : current.layers) sources += layer.kind == QVLayers::Kind::Source;
        if (sources == 1) return false;
    }
    current.layers.removeAt(index);
    emit changed();
    emit pixelsChanged();
    return true;
}

bool QVLayerModel::move(quint64 id, int index)
{
    const int old = indexOf(id);
    if (old < 0 || index < 0 || index >= current.layers.size() || old == index) return false;
    current.layers.move(old, index);
    emit changed();
    emit pixelsChanged();
    return true;
}

void QVLayerModel::update(const QVLayers::Layer &layer)
{
    const int index = indexOf(layer.id);
    if (index < 0) return;
    auto updated = layer;
    updated.kind = current.layers[index].kind;
    updated.strength = qBound(0, updated.strength, 100);
    updated.name = updated.name.trimmed().left(120);
    if (updated.name.isEmpty()) updated.name = current.layers[index].name;
    const bool pixels = !current.layers[index].samePixels(updated);
    current.layers[index] = updated;
    emit changed();
    if (pixels) emit pixelsChanged();
}
