#include "qvmediaformats.h"

#include <QSet>

QVMediaFormats::FormatList QVMediaFormats::supportedVideoFormats()
{
    // Asking the multimedia backend for its supported formats loads that backend
    // and, with Qt's FFmpeg plugin, a large codec stack. Doing so while constructing
    // QApplication added seconds to every launch, including image-only launches.
    // Keep startup classification container-based and let QMediaPlayer report the
    // uncommon case where the installed backend cannot decode a particular file.
    const QSet<QString> extensions = { ".3g2",  ".3gp", ".avi", ".m2ts", ".m4v",
                                       ".mkv",  ".mov", ".mp4", ".mpeg", ".mpg",
                                       ".mts",  ".ogv", ".ts",  ".webm", ".wmv" };
    const QSet<QString> mimeTypes = { "video/3gpp",       "video/3gpp2",
                                      "video/mp4",        "video/mpeg",
                                      "video/ogg",        "video/quicktime",
                                      "video/webm",       "video/x-matroska",
                                      "video/x-ms-wmv",   "video/x-msvideo",
                                      "video/mp2t" };

    FormatList result = { extensions.values(), mimeTypes.values() };
    result.extensions.sort(Qt::CaseInsensitive);
    result.mimeTypes.sort(Qt::CaseInsensitive);
    return result;
}
