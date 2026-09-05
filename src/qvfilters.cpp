#include "qvfilters.h"

#include <QtGlobal>

namespace {
int boundedChannel(double value)
{
    return qBound(0, qRound(value), 255);
}
}

namespace QVFilters {
QImage apply(const QImage &source, const Settings &settings)
{
    if (source.isNull() || settings.isNeutral())
        return source;

    QImage filtered = source.convertToFormat(QImage::Format_ARGB32);
    const double brightness = qBound(-100, settings.brightness, 100) * 2.55;
    const double contrast = 1.0 + qBound(-100, settings.contrast, 100) / 100.0;
    const double saturation = 1.0 + qBound(-100, settings.saturation, 100) / 100.0;

    for (int y = 0; y < filtered.height(); ++y) {
        QRgb *pixels = reinterpret_cast<QRgb *>(filtered.scanLine(y));
        for (int x = 0; x < filtered.width(); ++x) {
            const QRgb pixel = pixels[x];
            double red = (qRed(pixel) - 127.5) * contrast + 127.5 + brightness;
            double green = (qGreen(pixel) - 127.5) * contrast + 127.5 + brightness;
            double blue = (qBlue(pixel) - 127.5) * contrast + 127.5 + brightness;
            const double luminance = red * 0.2126 + green * 0.7152 + blue * 0.0722;
            red = luminance + (red - luminance) * saturation;
            green = luminance + (green - luminance) * saturation;
            blue = luminance + (blue - luminance) * saturation;
            pixels[x] = qRgba(boundedChannel(red), boundedChannel(green),
                              boundedChannel(blue), qAlpha(pixel));
        }
    }
    return filtered;
}

QString ffmpegFilter(const Settings &settings)
{
    if (settings.isNeutral())
        return {};
    const double brightness = qBound(-100, settings.brightness, 100) / 100.0;
    const double contrast = 1.0 + qBound(-100, settings.contrast, 100) / 100.0;
    const double saturation = 1.0 + qBound(-100, settings.saturation, 100) / 100.0;
    return QStringLiteral("eq=brightness=%1:contrast=%2:saturation=%3")
            .arg(brightness, 0, 'f', 4)
            .arg(contrast, 0, 'f', 4)
            .arg(saturation, 0, 'f', 4);
}
}
