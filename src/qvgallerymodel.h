#ifndef QVGALLERYMODEL_H
#define QVGALLERYMODEL_H

#include "qvmediacatalog.h"
#include <QAbstractListModel>
#include <QCache>
#include <QFutureWatcher>
#include <QImage>
#include <QSet>
#include <atomic>
#include <memory>

class QVGalleryModel : public QAbstractListModel
{
    Q_OBJECT
public:
    enum Role { PathRole = Qt::UserRole + 1, DirectoryRole, MediaTypeRole };
    explicit QVGalleryModel(QObject *parent = nullptr);
    ~QVGalleryModel() override;
    int rowCount(const QModelIndex &parent = {}) const override;
    QVariant data(const QModelIndex &index, int role) const override;
    void openFolder(const QString &path, const QVMediaCatalog::ScanOptions &options);
    void clear();
    QList<QVMediaCatalog::MediaFile> mediaFiles() const {
        QList<QVMediaCatalog::MediaFile> files;
        for (const auto &entry : entries) if (!entry.isDirectory) files.append(entry.file);
        return files;
    }
    QString folderPath() const { return folder; }
    void setThumbnailsEnabled(bool enabled) { thumbnailsEnabled = enabled; retryVisibleThumbnails(); }
    void retryVisibleThumbnails() { requestedThumbnails.clear(); }

signals:
    void folderLoaded(const QString &error);

private:
    void requestThumbnail(int row);
    QList<QVMediaCatalog::FolderEntry> entries;
    QString folder;
    QStringList thumbnailKeys;
    quint64 generation = 0;
    std::shared_ptr<std::atomic_bool> cancelled;
    // KiB. Only visible cells ask for thumbnails; there is no decode queue.
    mutable QCache<QString, QImage> thumbnails{24 * 1024};
    QSet<QString> failedThumbnails;
    // Avoid repeated eviction/decoding if an exceptionally large viewport exceeds the cache.
    QSet<QString> requestedThumbnails;
    QFutureWatcher<QImage> thumbnailWatcher;
    bool thumbnailBusy = false;
    bool thumbnailsEnabled = true;
};
#endif
