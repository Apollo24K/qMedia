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
    };

    const State &state() const { return currentState; }
    State &state() { return currentState; }

    void setCurrentFile(const QFileInfo &fileInfo);
    void clearCurrentFile();
    void updateFolder(QString dirPath, const ScanOptions &options);
    void updateCurrentIndex();

    static QList<MediaFile> scanFolder(const QString &dirPath, const ScanOptions &options);
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

    State currentState;
    DirInfo lastDirInfo;
};

#endif // QVMEDIACATALOG_H
