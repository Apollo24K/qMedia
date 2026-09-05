#ifndef QVEXPORT_H
#define QVEXPORT_H

#include "qvfilters.h"

#include <QImage>
#include <QStringList>
#include <atomic>
#include <memory>

namespace QVExport {
struct Source {
    QString path;
    QImage frame;
    QSize size;
    bool video = false;
    bool animated = false;
    int frameNumber = 0;
    QByteArray animationFormat;
    qint64 positionMs = 0;
    int rotation = 0;
    bool mirrored = false;
    bool flipped = false;
    bool loop = false;
    bool muted = false;
    qint64 durationMs = -1;
    double speed = 1.0;
    QVFilters::Settings filters;
};
struct Options {
    bool wholeMedia = false;
    QString format = QStringLiteral("png");
    QSize size;
    int quality = 90;
    bool audio = true;
    bool loop = true;
    int rotation = 0;
    bool mirrored = false;
    bool flipped = false;
    bool reverse = false;
    double speed = 1.0;
    QVFilters::Settings filters;
};
struct Result {
    QString error;
    bool cancelled = false;
    qint64 durationMs = -1;
};
QString findFFmpeg();
QStringList imageFormats();
qint64 mediaDuration(const Source &source, const std::shared_ptr<std::atomic_bool> &cancel);
QStringList arguments(const QString &input, const QString &output, const Options &options,
                      bool concat = false);
bool sameFile(const QString &source, const QString &destination);
Result run(const Source &source, const Options &options, const QString &destination,
           const QString &ffmpeg, const std::shared_ptr<std::atomic_bool> &cancel);
}
#endif
