#ifndef QVFILTERS_H
#define QVFILTERS_H

#include <QImage>
#include <QString>

namespace QVFilters {
struct Settings
{
    int brightness = 0;
    int contrast = 0;
    int saturation = 0;

    bool isNeutral() const
    {
        return brightness == 0 && contrast == 0 && saturation == 0;
    }

    bool operator==(const Settings &other) const
    {
        return brightness == other.brightness && contrast == other.contrast
                && saturation == other.saturation;
    }

    bool operator!=(const Settings &other) const { return !(*this == other); }
};

QImage apply(const QImage &source, const Settings &settings);
QString ffmpegFilter(const Settings &settings);
}

#endif // QVFILTERS_H
