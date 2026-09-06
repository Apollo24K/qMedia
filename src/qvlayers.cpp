#include "qvlayers.h"
#include <QtMath>

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
}

bool QVLayers::Layer::samePixels(const Layer &other) const
{
    return kind == other.kind && visible == other.visible && strength == other.strength
            && blend == other.blend && (kind != Kind::Filter || filter == other.filter);
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
        } else {
            if (source || layer.strength != 100 || layer.blend != Blend::Normal) return false;
            source = true;
        }
    }
    return source;
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
                if (layer.kind == Kind::Filter) {
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
        if (layer.name.isEmpty()) layer.name = layer.kind == QVLayers::Kind::Source ? tr("Source") : tr("Filter");
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
