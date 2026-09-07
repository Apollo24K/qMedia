#include "qvmediacatalog.h"

#include <QCollator>
#include <QDir>
#include <QHash>
#include <QMimeDatabase>

#include <algorithm>
#include <chrono>
#include <random>

void QVMediaCatalog::setCurrentFile(const QFileInfo &fileInfo, MediaType mediaType)
{
    currentState.fileInfo = fileInfo;
    currentState.mediaType = mediaType;
    updateCurrentIndex();
}

void QVMediaCatalog::clearCurrentFile()
{
    currentState.fileInfo = QFileInfo();
    currentState.isLoadRequested = false;
    currentState.mediaType = MediaType::Unknown;
}

QVMediaCatalog::MediaType QVMediaCatalog::mediaTypeForFile(const QFileInfo &fileInfo,
                                                           const ScanOptions &options,
                                                           QString *detectedMimeType)
{
    MediaType mediaType = MediaType::Unknown;
    const QString fileName = fileInfo.fileName();
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

    QString mimeType;
    if (mediaType == MediaType::Unknown || options.sortMode == 4) {
        QMimeDatabase mimeDb;
        const QMimeDatabase::MatchMode matchMode = options.allowMimeContentDetection
                ? QMimeDatabase::MatchDefault
                : QMimeDatabase::MatchExtension;
        mimeType = mimeDb.mimeTypeForFile(fileInfo.absoluteFilePath(), matchMode).name();
        if (mediaType == MediaType::Unknown) {
            for (const SupportedMedia &supported : options.supportedMedia) {
                if (supported.mimeTypes.contains(mimeType)) {
                    mediaType = supported.type;
                    break;
                }
            }
        }
    }

    if (detectedMimeType)
        *detectedMimeType = mimeType;
    return mediaType;
}

QList<QVMediaCatalog::MediaFile> QVMediaCatalog::scanFolder(const QString &dirPath,
                                                           const ScanOptions &options)
{
    QList<MediaFile> fileList;
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

        QString mimeType;
        const MediaType mediaType = mediaTypeForFile(fileInfo, options, &mimeType);

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
    // Equal metadata keys keep a deterministic filename order in every scan.
    if (sortMode > 0 && sortMode < 5) sortFiles(files, 0, sortDescending);
    switch (sortMode) {
    case 0: {
        QCollator collator;
        collator.setNumericMode(true);
        std::stable_sort(files.begin(), files.end(), [&](const MediaFile &file1, const MediaFile &file2) {
            return sortDescending ? collator.compare(file1.fileName, file2.fileName) > 0
                                  : collator.compare(file1.fileName, file2.fileName) < 0;
        });
        break;
    }
    case 1:
        std::stable_sort(files.begin(), files.end(), [&](const MediaFile &file1, const MediaFile &file2) {
            return sortDescending ? file1.lastModified < file2.lastModified
                                  : file1.lastModified > file2.lastModified;
        });
        break;
    case 2:
        std::stable_sort(files.begin(), files.end(), [&](const MediaFile &file1, const MediaFile &file2) {
            return sortDescending ? file1.lastCreated < file2.lastCreated
                                  : file1.lastCreated > file2.lastCreated;
        });
        break;
    case 3:
        std::stable_sort(files.begin(), files.end(), [&](const MediaFile &file1, const MediaFile &file2) {
            return sortDescending ? file1.size < file2.size : file1.size > file2.size;
        });
        break;
    case 4: {
        QCollator collator;
        std::stable_sort(files.begin(), files.end(), [&](const MediaFile &file1, const MediaFile &file2) {
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

QList<QVMediaCatalog::FolderEntry> QVMediaCatalog::scanGallery(
        const QString &dirPath, const ScanOptions &options)
{
    QList<FolderEntry> entries;
    QDir::Filters filters = QDir::Dirs | QDir::NoDotAndDotDot;
    if (options.includeHidden) filters |= QDir::Hidden;
    QList<MediaFile> folders;
    const auto directories = QDir(dirPath).entryInfoList(filters, QDir::Unsorted);
    for (const auto &directory : directories) {
        if (!directory.fileName().startsWith("._"))
            folders.append({ directory.absoluteFilePath(), directory.fileName() });
    }
    sortFiles(folders, 0, options.sortDescending);
    for (const auto &folder : folders) entries.append({ folder, true });
    auto media = scanFolder(dirPath, options);
    sortFiles(media, options.sortMode, options.sortDescending);
    for (const auto &file : media) entries.append({ file, false });
    return entries;
}

void QVMediaCatalog::updateFolder(QString dirPath, const ScanOptions &options)
{
    if (dirPath.isEmpty()) {
        dirPath = currentState.fileInfo.path();
        if (dirPath.isEmpty())
            return;
    }

    auto files = scanFolder(dirPath, options);
    const DirInfo dirInfo = { QDir(dirPath).absolutePath(), files.count(), options.sortMode,
                              options.sortDescending };
    if (options.sortMode == 5 && lastDirInfo.dirPath == dirInfo.dirPath
            && lastDirInfo.sortMode == 5) {
        // A rescan must not reshuffle neighbors. Keep surviving entries in their
        // existing order and append newly discovered files.
        QHash<QString, int> ranks;
        for (int i = 0; i < currentState.folderFiles.size(); ++i)
            ranks.insert(currentState.folderFiles[i].absoluteFilePath, i);
        std::stable_sort(files.begin(), files.end(), [&](const MediaFile &a, const MediaFile &b) {
            return ranks.value(a.absoluteFilePath, ranks.size()) < ranks.value(b.absoluteFilePath, ranks.size());
        });
    } else {
        // scanFolder returns filesystem order, even when the folder/count is unchanged.
        sortFiles(files, options.sortMode, options.sortDescending);
    }
    currentState.folderFiles = files;
    lastDirInfo = dirInfo;

    updateCurrentIndex();
}

void QVMediaCatalog::setFolderOrder(const QString &path, const QList<MediaFile> &files,
                                    const ScanOptions &options)
{
    currentState.folderFiles = files;
    lastDirInfo = { QDir(path).absolutePath(), files.count(), options.sortMode, options.sortDescending };
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
            currentState.mediaType = currentState.folderFiles[i].mediaType;
            return;
        }
    }
    currentState.currentIndexInFolder = -1;
}
