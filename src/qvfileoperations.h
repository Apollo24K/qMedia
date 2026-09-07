#ifndef QVFILEOPERATIONS_H
#define QVFILEOPERATIONS_H

#include <QStringList>
#include <QList>
#include <functional>

namespace QVFileOperations {
struct TrashResult {
    QString originalPath;
    QString trashPath;
    QString error;
};
using TrashOperation = std::function<bool(const QString &, QString *, QString *)>;
// A failed trash operation never falls back to permanent deletion.
QList<TrashResult> trash(const QStringList &paths, const TrashOperation &operation = {});
}
#endif
