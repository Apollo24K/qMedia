#include "qvmediacatalog.h"

#include <QCollator>
#include <QDir>
#include <QMimeDatabase>

#include <algorithm>
#include <chrono>
#include <random>

void QVMediaCatalog::setCurrentFile(const QFileInfo &fileInfo)
{
    currentState.fileInfo = fileInfo;
    updateCurrentIndex();
}

void QVMediaCatalog::clearCurrentFile()
{
    currentState.fileInfo = QFileInfo();
    currentState.isLoadRequested = false;
}

QList<QVMediaCatalog::MediaFile> QVMediaCatalog::scanFolder(const QString &dirPath,
                                                           const ScanOptions &options)
{
    QList<MediaFile> fileList;
    QMimeDatabase mimeDb;
    const QMimeDatabase::MatchMode mimeMatchMode = options.allowMimeContentDetection
            ? QMimeDatabase::MatchDefault
            : QMimeDatabase::MatchExtension;

    QDir::Filters filters = QDir::Files;
    if (options.includeHidden)
        filters |= QDir::Hidden;

    const QFileInfoList currentFolder = QDir(dirPath).entryInfoList(filters, QDir::Unsorted);
    for (const QFileInfo &fileInfo : currentFolder) {
        const QString absoluteFilePath = fileInfo.absoluteFilePath();
        const QString fileName = fileInfo.fileName();

        // Ignore macOS metadata files even when hidden files are enabled.
        if (fileName.startsWith("._"))
            continue;

        MediaType mediaType = MediaType::Unknown;
        QString mimeType;
        for (const SupportedMedia &supported : options.supportedMedia) {
            for (const QString &extension : supported.extensions) {
                if (fileName.endsWith(extension, Qt::CaseInsensitive)) {
                    mediaType = supported.type;
                    break;
                }
            }
            if (mediaType != MediaType::Unknown)
                break;
        }

        if (mediaType == MediaType::Unknown || options.sortMode == 4) {
            mimeType = mimeDb.mimeTypeForFile(absoluteFilePath, mimeMatchMode).name();
            if (mediaType == MediaType::Unknown) {
                for (const SupportedMedia &supported : options.supportedMedia) {
                    if (supported.mimeTypes.contains(mimeType)) {
                        mediaType = supported.type;
                        break;
                    }
                }
            }
        }

        if (mediaType == MediaType::Unknown)
            continue;

        fileList.append({ absoluteFilePath,
                          fileName,
                          options.sortMode == 1
                                  ? fileInfo.lastModified().toMSecsSinceEpoch()
                                  : 0,
#if QT_VERSION >= QT_VERSION_CHECK(5, 12, 0)
                          options.sortMode == 2 ? fileInfo.birthTime().toMSecsSinceEpoch() : 0,
#else
                          options.sortMode == 2 ? fileInfo.created().toMSecsSinceEpoch() : 0,
#endif
                          options.sortMode == 3 ? fileInfo.size() : 0,
                          options.sortMode == 4 ? mimeType : QString(),
                          mediaType });
    }

    return fileList;
}

void QVMediaCatalog::sortFiles(QList<MediaFile> &files, int sortMode, bool sortDescending)
{
    switch (sortMode) {
    case 0: {
        QCollator collator;
        collator.setNumericMode(true);
        std::sort(files.begin(), files.end(), [&](const MediaFile &file1, const MediaFile &file2) {
            return sortDescending ? collator.compare(file1.fileName, file2.fileName) > 0
                                  : collator.compare(file1.fileName, file2.fileName) < 0;
        });
        break;
    }
    case 1:
        std::sort(files.begin(), files.end(), [&](const MediaFile &file1, const MediaFile &file2) {
            return sortDescending ? file1.lastModified < file2.lastModified
                                  : file1.lastModified > file2.lastModified;
        });
        break;
    case 2:
        std::sort(files.begin(), files.end(), [&](const MediaFile &file1, const MediaFile &file2) {
            return sortDescending ? file1.lastCreated < file2.lastCreated
                                  : file1.lastCreated > file2.lastCreated;
        });
        break;
    case 3:
        std::sort(files.begin(), files.end(), [&](const MediaFile &file1, const MediaFile &file2) {
            return sortDescending ? file1.size < file2.size : file1.size > file2.size;
        });
        break;
    case 4: {
        QCollator collator;
        std::sort(files.begin(), files.end(), [&](const MediaFile &file1, const MediaFile &file2) {
            return sortDescending ? collator.compare(file1.mimeType, file2.mimeType) > 0
                                  : collator.compare(file1.mimeType, file2.mimeType) < 0;
        });
        break;
    }
    case 5:
        std::shuffle(files.begin(), files.end(), std::default_random_engine(
                                                       std::chrono::system_clock::now()
                                                               .time_since_epoch()
                                                               .count()));
        break;
    default:
        Q_ASSERT(false);
        break;
    }
}

void QVMediaCatalog::updateFolder(QString dirPath, const ScanOptions &options)
{
    if (dirPath.isEmpty()) {
        dirPath = currentState.fileInfo.path();
        if (dirPath.isEmpty())
            return;
    }

    currentState.folderFiles = scanFolder(dirPath, options);
    const DirInfo dirInfo = { dirPath, currentState.folderFiles.count(), options.sortMode,
                              options.sortDescending };
    const bool shouldSort = lastDirInfo != dirInfo;
    lastDirInfo = dirInfo;

    if (shouldSort)
        sortFiles(currentState.folderFiles, options.sortMode, options.sortDescending);

    updateCurrentIndex();
}

void QVMediaCatalog::updateCurrentIndex()
{
    const QString targetPath =
            currentState.fileInfo.absoluteFilePath().normalized(QString::NormalizationForm_D);
    for (int i = 0; i < currentState.folderFiles.length(); ++i) {
        const QString candidatePath = currentState.folderFiles[i]
                                              .absoluteFilePath
                                              .normalized(QString::NormalizationForm_D);
        if (candidatePath.compare(targetPath, Qt::CaseInsensitive) == 0
            && QFileInfo(currentState.folderFiles[i].absoluteFilePath)
                    == currentState.fileInfo) {
            currentState.currentIndexInFolder = i;
            return;
        }
    }
    currentState.currentIndexInFolder = -1;
}
