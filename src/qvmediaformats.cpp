#include "qvmediaformats.h"

#include <QSet>

QVMediaFormats::FormatList QVMediaFormats::supportedVideoFormats()
{
    // Asking the multimedia backend for its supported formats loads that backend
    // and, with Qt's FFmpeg plugin, a large codec stack. Doing so while constructing
    // QApplication added seconds to every launch, including image-only launches.
    // Keep startup classification container-based and let QMediaPlayer report the
    // uncommon case where the installed backend cannot decode a particular file.
    const QSet<QString> extensions = {
        ".3g2", ".3gp", ".asf", ".avi", ".divx", ".dv",  ".f4v",  ".flv",
        ".m1v", ".m2ts", ".m2v", ".m4v", ".mkv",  ".mov", ".mp4",  ".mpe",
        ".mpeg", ".mpg", ".mpv", ".mts", ".mxf",  ".ogm", ".ogv",  ".qt",
        ".ts",  ".vob", ".webm", ".wmv"
    };
    const QSet<QString> mimeTypes = {
        "application/mxf", "video/3gpp",       "video/3gpp2",     "video/dv",
        "video/mp2t",      "video/mp4",        "video/mpeg",      "video/ogg",
        "video/quicktime", "video/webm",       "video/x-flv",     "video/x-matroska",
        "video/x-ms-asf",  "video/x-ms-wmv",   "video/x-msvideo"
    };

    FormatList result = { extensions.values(), mimeTypes.values() };
    result.extensions.sort(Qt::CaseInsensitive);
    result.mimeTypes.sort(Qt::CaseInsensitive);
    return result;
}
