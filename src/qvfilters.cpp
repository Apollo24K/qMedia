#include "qvfilters.h"

#include <QtMath>
#include <QtGlobal>
#include <QStringList>

namespace {
int boundedChannel(double value)
{
    return qBound(0, qRound(value), 255);
}

struct ColorTransform
{
    double matrix[3][3];
    double offset;
};

ColorTransform colorTransform(const QVFilters::Layer &layer)
{
    const double contrast = 1.0 + qBound(-100, layer.contrast, 100) / 100.0;
    const double saturation = 1.0 + qBound(-100, layer.saturation, 100) / 100.0;
    const double radians = qDegreesToRadians(double(qBound(-180, layer.hue, 180)));
    const double cosine = qCos(radians);
    const double sine = qSin(radians);
    const double luminance[] = { 0.2126, 0.7152, 0.0722 };
    double saturationMatrix[3][3];
    for (int row = 0; row < 3; ++row) {
        for (int column = 0; column < 3; ++column) {
            saturationMatrix[row][column] = luminance[column] * (1.0 - saturation)
                    + (row == column ? saturation : 0.0);
        }
    }
    const double hueMatrix[3][3] = {
        { 0.213 + cosine * 0.787 - sine * 0.213,
          0.715 - cosine * 0.715 - sine * 0.715,
          0.072 - cosine * 0.072 + sine * 0.928 },
        { 0.213 - cosine * 0.213 + sine * 0.143,
          0.715 + cosine * 0.285 + sine * 0.140,
          0.072 - cosine * 0.072 - sine * 0.283 },
        { 0.213 - cosine * 0.213 - sine * 0.787,
          0.715 - cosine * 0.715 + sine * 0.715,
          0.072 + cosine * 0.928 + sine * 0.072 }
    };
    ColorTransform result{};
    for (int row = 0; row < 3; ++row) {
        for (int column = 0; column < 3; ++column) {
            for (int inner = 0; inner < 3; ++inner) {
                result.matrix[row][column] += hueMatrix[row][inner]
                        * saturationMatrix[inner][column] * contrast;
            }
        }
    }
    result.offset = 127.5 * (1.0 - contrast)
            + qBound(-100, layer.brightness, 100) * 2.55;
    return result;
}

double gradientAmount(const QVFilters::Layer &layer, int x, int y, int width, int height)
{
    if (!layer.gradient)
        return 1.0;
    const double normalizedX = width > 1 ? x / double(width - 1) : 0.5;
    const double normalizedY = height > 1 ? y / double(height - 1) : 0.5;
    const double radians = qDegreesToRadians(double((layer.direction % 360 + 360) % 360));
    const double projected = (normalizedX - qBound(0, layer.centerX, 100) / 100.0)
                    * qCos(radians)
            + (normalizedY - qBound(0, layer.centerY, 100) / 100.0) * qSin(radians);
    const double softness = qMax(1, qBound(1, layer.softness, 100)) / 100.0;
    return qBound(0.0, projected / softness + 0.5, 1.0);
}

QString number(double value)
{
    if (qAbs(value) < 0.00000001)
        value = 0.0;
    return QString::number(value, 'f', 8);
}

QString maskExpression(const QVFilters::Layer &layer)
{
    if (!layer.gradient)
        return QStringLiteral("1");
    const double radians = qDegreesToRadians(double((layer.direction % 360 + 360) % 360));
    return QStringLiteral("clip((((X/max(W-1\\,1))-%1)*%2+((Y/max(H-1\\,1))-%3)*%4)/%5+0.5\\,0\\,1)")
            .arg(number(qBound(0, layer.centerX, 100) / 100.0), number(qCos(radians)),
                 number(qBound(0, layer.centerY, 100) / 100.0), number(qSin(radians)),
                 number(qMax(1, qBound(1, layer.softness, 100)) / 100.0));
}

QString channelExpression(const ColorTransform &transform, int row, const QString &mask)
{
    static const char *channels[] = { "r(X,Y)", "g(X,Y)", "b(X,Y)" };
    QString adjustment = number(transform.offset);
    for (int column = 0; column < 3; ++column) {
        adjustment += QStringLiteral("+(%1)*%2")
                .arg(number(transform.matrix[row][column] - (row == column ? 1.0 : 0.0)),
                     QString::fromLatin1(channels[column]));
    }
    return QStringLiteral("clip(%1+%2*(%3)\\,0\\,255)")
            .arg(QString::fromLatin1(channels[row]), mask, adjustment);
}
}

namespace QVFilters {
QStringList channelExpressions(const Layer &layer, const QStringList &channels)
{
    const auto transform = colorTransform(layer);
    const QString mask = maskExpression(layer);
    QStringList result;
    for (int c = 0; c < 3; ++c)
        result << channelExpression(transform, c, mask);
    result << QStringLiteral("alpha(X,Y)*(1-%1*%2)")
                      .arg(number(qBound(0, layer.transparency, 100) / 100.0), mask);
    const QStringList original{ "r(X,Y)", "g(X,Y)", "b(X,Y)", "alpha(X,Y)" };
    for (auto &expression : result) {
        for (int c = 0; c < 4; ++c) expression.replace(original[c], channels[c]);
        expression.replace("\\,", ",");
    }
    return result;
}

bool Settings::isNeutral() const
{
    for (const auto &layer : layers) {
        if (!layer.isNeutral())
            return false;
    }
    return true;
}

QImage apply(const QImage &source, const Settings &settings)
{
    if (source.isNull() || settings.isNeutral())
        return source;

    QImage filtered = source.convertToFormat(QImage::Format_ARGB32);
    for (const auto &layer : settings.layers) {
        if (layer.isNeutral())
            continue;
        const ColorTransform transform = colorTransform(layer);
        const double transparency = qBound(0, layer.transparency, 100) / 100.0;
        for (int y = 0; y < filtered.height(); ++y) {
            QRgb *pixels = reinterpret_cast<QRgb *>(filtered.scanLine(y));
            for (int x = 0; x < filtered.width(); ++x) {
                const QRgb pixel = pixels[x];
                const double amount = gradientAmount(layer, x, y,
                                                     filtered.width(), filtered.height());
                const double channels[] = { double(qRed(pixel)), double(qGreen(pixel)),
                                            double(qBlue(pixel)) };
                double transformed[3] = { transform.offset, transform.offset, transform.offset };
                for (int row = 0; row < 3; ++row)
                    for (int column = 0; column < 3; ++column)
                        transformed[row] += transform.matrix[row][column] * channels[column];
                pixels[x] = qRgba(
                        boundedChannel(channels[0] + amount * (transformed[0] - channels[0])),
                        boundedChannel(channels[1] + amount * (transformed[1] - channels[1])),
                        boundedChannel(channels[2] + amount * (transformed[2] - channels[2])),
                        boundedChannel(qAlpha(pixel) * (1.0 - transparency * amount)));
            }
        }
    }
    return filtered;
}

QString ffmpegFilter(const Settings &settings)
{
    QStringList result;
    for (const auto &layer : settings.layers) {
        if (layer.isNeutral())
            continue;
        const ColorTransform transform = colorTransform(layer);
        const QString mask = maskExpression(layer);
        const QString alpha = QStringLiteral("alpha(X,Y)*(1-%1*%2)")
                .arg(number(qBound(0, layer.transparency, 100) / 100.0), mask);
        result << QStringLiteral("format=rgba,geq=r='%1':g='%2':b='%3':a='%4'")
                          .arg(channelExpression(transform, 0, mask),
                               channelExpression(transform, 1, mask),
                               channelExpression(transform, 2, mask), alpha);
    }
    return result.join(',');
}
}
