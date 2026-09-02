#ifndef QVMEDIAFORMATS_H
#define QVMEDIAFORMATS_H

#include <QStringList>

class QVMediaFormats
{
public:
    struct FormatList
    {
        QStringList extensions;
        QStringList mimeTypes;
    };

    static FormatList supportedVideoFormats();
};

#endif // QVMEDIAFORMATS_H
