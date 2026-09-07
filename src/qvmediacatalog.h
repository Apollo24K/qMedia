#ifndef QVMEDIACATALOG_H
#define QVMEDIACATALOG_H

#include <QFileInfo>
#include <QList>
#include <QStringList>

class QVMediaCatalog
{
public:
    enum class MediaType { Unknown, Image, Video, Audio };

    struct SupportedMedia
    {
        MediaType type = MediaType::Unknown;
        QStringList extensions;
        QStringList mimeTypes;
    };

    struct ScanOptions
    {
        QList<SupportedMedia> supportedMedia;
        bool allowMimeContentDetection = false;
        bool includeHidden = false;
        int sortMode = 0;
        bool sortDescending = false;
    };

    struct MediaFile
    {
        QString absoluteFilePath;
        QString fileName;

        // Only populated if needed for sorting.
        qint64 lastModified = 0;
        qint64 lastCreated = 0;
        qint64 size = 0;
        QString mimeType;
        MediaType mediaType = MediaType::Unknown;
    };

    struct State
    {
        QFileInfo fileInfo;
        QList<MediaFile> folderFiles;
        int currentIndexInFolder = -1;
        bool isLoadRequested = false;
        MediaType mediaType = MediaType::Unknown;
    };

    struct FolderEntry
    {
        MediaFile file;
        bool isDirectory = false;
    };

    // The inverse of successive Up operations, independent of media decoding.
    class FolderHistory
    {
    public:
        void clear() { steps.clear(); }
        void remember(const QString &parent, const QString &child) { steps.append({parent, child}); }
        QString childOf(const QString &parent) const {
            return !steps.isEmpty() && steps.last().first == parent ? steps.last().second : QString();
        }
        QString takeChild(const QString &parent) {
            const QString child = childOf(parent);
            if (!child.isEmpty()) steps.removeLast();
            return child;
        }
    private:
        QList<QPair<QString, QString>> steps;
    };

    // One level only: folders first, followed by supported media in viewer order.
    static QList<FolderEntry> scanGallery(const QString &dirPath, const ScanOptions &options);

    const State &state() const { return currentState; }
    State &state() { return currentState; }

    void setCurrentFile(const QFileInfo &fileInfo, MediaType mediaType = MediaType::Unknown);
    void clearCurrentFile();
    void updateFolder(QString dirPath, const ScanOptions &options);
    void updateCurrentIndex();
    void setFolderOrder(const QString &path, const QList<MediaFile> &files, const ScanOptions &options, bool selectedGroup = false);
    void clearNavigationGroup();

    static QList<MediaFile> scanFolder(const QString &dirPath, const ScanOptions &options);
    static MediaType mediaTypeForFile(const QFileInfo &fileInfo, const ScanOptions &options,
                                      QString *mimeType = nullptr);
    static void sortFiles(QList<MediaFile> &files, int sortMode, bool sortDescending);

private:
    struct DirInfo
    {
        QString dirPath;
        qsizetype fileCount = 0;
        int sortMode = 0;
        bool sortDescending = false;

        bool operator!=(const DirInfo &other) const
        {
            return dirPath != other.dirPath || fileCount != other.fileCount
                    || sortMode != other.sortMode || sortDescending != other.sortDescending;
        }
    };

    bool navigationGroup = false;
    State currentState;
    DirInfo lastDirInfo;
};

#endif // QVMEDIACATALOG_H
