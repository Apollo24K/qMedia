#include "qvfileoperations.h"
#include <QCoreApplication>
#include <QFile>
#include <QSet>

QList<QVFileOperations::TrashResult> QVFileOperations::trash(
        const QStringList &paths, const TrashOperation &operation)
{
    QList<TrashResult> results;
    QSet<QString> seen;
    for (const QString &path : paths) {
        if (path.isEmpty() || seen.contains(path)) continue;
        seen.insert(path);
        TrashResult result{path, {}, {}};
        bool success = false;
        if (operation) {
            success = operation(path, &result.trashPath, &result.error);
        } else {
#if QT_VERSION >= QT_VERSION_CHECK(5, 15, 0)
            QFile file(path);
            success = file.moveToTrash();
            if (success) result.trashPath = file.fileName();
            else result.error = file.errorString();
#else
            result.error = QCoreApplication::translate("QVFileOperations",
                    "Moving gallery selections to the trash requires Qt 5.15 or later.");
#endif
        }
        if (!success && result.error.isEmpty())
            result.error = QCoreApplication::translate("QVFileOperations", "Could not move this item to the trash.");
        if (!success) result.trashPath.clear();
        results.append(result);
    }
    return results;
}
