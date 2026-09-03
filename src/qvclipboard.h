#ifndef QVCLIPBOARD_H
#define QVCLIPBOARD_H

#include <QByteArray>
#include <QImage>
#include <QList>
#include <QUrl>

class QMimeData;

namespace QVClipboard {
struct Media {
    QList<QUrl> urls;
    QByteArray bytes;
    QString mimeType;
    QImage image;
};

Media read(const QMimeData &data);
// Returns a safe media suffix, or an empty string for unsupported content.
QString mediaSuffix(const QByteArray &prefix, const QString &mimeType, const QUrl &url = {});
}

#endif
