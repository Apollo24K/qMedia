#ifndef QVFILTERS_H
#define QVFILTERS_H

#include <QImage>
#include <QString>
#include <QVector>

namespace QVFilters {
struct Layer
{
    int brightness = 0;
    int contrast = 0;
    int saturation = 0;
    int hue = 0;
    int transparency = 0;
    bool gradient = false;
    int centerX = 50;
    int centerY = 50;
    int direction = 90;
    int softness = 50;

    bool isNeutral() const
    {
        return brightness == 0 && contrast == 0 && saturation == 0 && hue == 0
                && transparency == 0;
    }

    bool operator==(const Layer &other) const
    {
        return brightness == other.brightness && contrast == other.contrast
                && saturation == other.saturation && hue == other.hue
                && transparency == other.transparency && gradient == other.gradient
                && centerX == other.centerX && centerY == other.centerY
                && direction == other.direction && softness == other.softness;
    }

    bool operator!=(const Layer &other) const { return !(*this == other); }
};

struct Settings
{
    QVector<Layer> layers = QVector<Layer>(1);

    bool isNeutral() const;
    bool operator==(const Settings &other) const { return layers == other.layers; }
    bool operator!=(const Settings &other) const { return !(*this == other); }
};

QImage apply(const QImage &source, const Settings &settings);
QString ffmpegFilter(const Settings &settings);
}

#endif // QVFILTERS_H
