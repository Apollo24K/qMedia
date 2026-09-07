#include "qvgallerymodel.h"
#include <QDir>
#include <QImageReader>
#include <QtConcurrent/QtConcurrentRun>

QVGalleryModel::QVGalleryModel(QObject *parent) : QAbstractListModel(parent) {}

QVGalleryModel::~QVGalleryModel()
{
    if (cancelled) *cancelled = true;
    // Workers capture values only. Destroying a window never waits for a decoder.
}

int QVGalleryModel::rowCount(const QModelIndex &parent) const
{
    return parent.isValid() ? 0 : entries.size();
}

QVariant QVGalleryModel::data(const QModelIndex &index, int role) const
{
    if (!index.isValid() || index.row() < 0 || index.row() >= entries.size()) return {};
    const auto &entry = entries[index.row()];
    switch (role) {
    case Qt::DisplayRole: return entry.file.fileName;
    case Qt::ToolTipRole: return QDir::toNativeSeparators(entry.file.absoluteFilePath);
    case PathRole: return entry.file.absoluteFilePath;
    case DirectoryRole: return entry.isDirectory;
    case MediaTypeRole: return int(entry.file.mediaType);
    case Qt::DecorationRole:
        if (entry.isDirectory || entry.file.mediaType != QVMediaCatalog::MediaType::Image)
            return {};
        if (auto *image = thumbnails.object(index.row())) return *image;
        if (thumbnailsEnabled && !thumbnailBusy && !failedThumbnails.contains(index.row())
                && !requestedThumbnails.contains(index.row()))
            const_cast<QVGalleryModel *>(this)->requestThumbnail(index.row());
        return {};
    }
    return {};
}

void QVGalleryModel::clear()
{
    ++generation;
    if (cancelled) *cancelled = true;
    beginResetModel();
    entries.clear();
    folder.clear();
    thumbnails.clear();
    failedThumbnails.clear();
    requestedThumbnails.clear();
    endResetModel();
}

void QVGalleryModel::openFolder(const QString &path, const QVMediaCatalog::ScanOptions &options)
{
    clear();
    folder = QDir(path).absolutePath();
    const auto request = generation;
    cancelled = std::make_shared<std::atomic_bool>(false);
    const auto token = cancelled;
    using Result = QPair<QList<QVMediaCatalog::FolderEntry>, QString>;
    auto *watcher = new QFutureWatcher<Result>(this);
    connect(watcher, &QFutureWatcher<Result>::finished, this, [this, watcher, request] {
        const auto result = watcher->result();
        watcher->deleteLater();
        if (request != generation) return;
        beginResetModel();
        entries = result.first;
        endResetModel();
        emit folderLoaded(result.second);
    });
    watcher->setFuture(QtConcurrent::run([path, options, token]() -> Result {
        if (*token) return {};
        const QFileInfo info(path);
        if (!info.exists() || !info.isDir() || !info.isReadable())
            return { {}, tr("This folder is unavailable or cannot be read.") };
        const auto files = QVMediaCatalog::scanGallery(path, options);
        if (*token) return {};
        return { files, {} };
    }));
}

void QVGalleryModel::requestThumbnail(int row)
{
    requestedThumbnails.insert(row);
    thumbnailBusy = true;
    const auto request = generation;
    const QString path = entries[row].file.absoluteFilePath;
    disconnect(&thumbnailWatcher, nullptr, this, nullptr);
    connect(&thumbnailWatcher, &QFutureWatcher<QImage>::finished, this, [this, request, row] {
        thumbnailBusy = false;
        if (request == generation) {
            const QImage image = thumbnailWatcher.result();
            if (image.isNull()) failedThumbnails.insert(row);
            else thumbnails.insert(row, new QImage(image), qMax(1, int(image.sizeInBytes() / 1024)));
        }
        // Repaint visible cells to request the next thumbnail, including after a folder switch.
        if (!entries.isEmpty()) emit dataChanged(index(0), index(entries.size() - 1), { Qt::DecorationRole });
    });
    thumbnailWatcher.setFuture(QtConcurrent::run([path] {
        QImageReader reader(path);
        reader.setAutoTransform(true);
        const QSize size = reader.size();
        // Keep fallback decoders with no scaled-read support within a modest budget.
        if (!size.isValid() || qint64(size.width()) * size.height() > 32 * 1024 * 1024)
            return QImage();
        reader.setScaledSize(size.scaled(320, 224, Qt::KeepAspectRatio));
        const QImage image = reader.read();
        return image.scaled(320, 224, Qt::KeepAspectRatio, Qt::SmoothTransformation);
    }));
}
