#ifndef QVGALLERYVIEW_H
#define QVGALLERYVIEW_H

#include "qvgallerymodel.h"
#include <QWidget>

class QLabel;
class QListView;
class QStackedWidget;
class QVBoxLayout;
class QTimer;
class QVGalleryView : public QWidget
{
    Q_OBJECT
public:
    explicit QVGalleryView(QWidget *parent = nullptr);
    void showHome(const QList<QPair<QString, QString>> &recents);
    void openFolder(const QString &path, const QVMediaCatalog::ScanOptions &options,
                    const QString &selectedPath = {}, bool preserveScroll = false);
    QString folderPath() const { return model->folderPath(); }
    bool isHome() const;
    QList<QVMediaCatalog::MediaFile> mediaFiles() const { return model->mediaFiles(); }
    void setBackgroundColor(const QColor &color);
    QStringList selectedPaths() const;
    bool hasSelection() const;
    void clearSelection();
    void zoom(int direction);
    void resetZoom();

signals:
    void pathActivated(const QString &path);
    void openFileRequested();
    void fullscreenRequested();
    void selectionChanged();

protected:
    bool eventFilter(QObject *watched, QEvent *event) override;
    void dragEnterEvent(QDragEnterEvent *event) override;
    void dropEvent(QDropEvent *event) override;
    void showEvent(QShowEvent *event) override;
    void hideEvent(QHideEvent *event) override;

private:
    QVGalleryModel *model;
    QListView *list;
    QStackedWidget *pages;
    QLabel *selectionCount;
    void updateSelectionCount();
    void positionSelectionCount();
    QLabel *empty;
    QVBoxLayout *recentLayout;
    QString selection;
    QTimer *layoutTimer;
    int tileZoom = 0;
    int scrollPosition = 0;
    bool restoreScroll = false;
    bool folderLoading = false;
    void restoreScrollPosition();
};
#endif
