#include "qvgalleryview.h"
#include <QDir>
#include <QDragEnterEvent>
#include <QDropEvent>
#include <QLabel>
#include <QListView>
#include <QMimeData>
#include <QMouseEvent>
#include <QPainter>
#include <QPainterPath>
#include <QPushButton>
#include <QSet>
#include <QScrollBar>
#include <QShortcut>
#include <QStackedWidget>
#include <QStyledItemDelegate>
#include <QVBoxLayout>
#include <QTimer>
#include <cmath>

namespace {
constexpr int galleryTopSpace = 64;
QSize galleryCellSize(int viewportWidth, int preferredWidth)
{
    const int columns = qMax(1, viewportWidth / preferredWidth);
    // QListView wraps against an inclusive right edge; reserve its final pixel.
    const int width = qMax(1, viewportWidth - 1);
    return QSize(qMax(1, width / columns), qRound(0.75 * width / columns) + 28);
}

class GalleryListView : public QListView
{
public:
    using QListView::QListView;
protected:
    void updateGeometries() override
    {
        const int position = verticalScrollBar()->value();
        QListView::updateGeometries();
        verticalScrollBar()->setMinimum(-galleryTopSpace);
        if (position < 0) verticalScrollBar()->setValue(position);
    }
public:
    QModelIndex indexAt(const QPoint &point) const override
    {
        const auto index = QListView::indexAt(point);
        // The space between painted cards is empty space, including for drag selection.
        return index.isValid() && !visualRect(index).adjusted(5, 5, -5, -5).contains(point)
                ? QModelIndex() : index;
    }
};

class GalleryDelegate : public QStyledItemDelegate
{
public:
    using QStyledItemDelegate::QStyledItemDelegate;
    QSize sizeHint(const QStyleOptionViewItem &option, const QModelIndex &) const override
    {
        // Layout must never request DecorationRole for offscreen entries.
        const auto *view = qobject_cast<const QListView *>(option.widget);
        return galleryCellSize(view ? view->viewport()->width() : 176,
                               view ? view->property("preferredTileWidth").toInt() : 176);
    }
    void paint(QPainter *p, const QStyleOptionViewItem &option, const QModelIndex &index) const override
    {
        p->save();
        p->setRenderHint(QPainter::Antialiasing);
        const bool selected = option.state & QStyle::State_Selected;
        const bool hovered = option.state & QStyle::State_MouseOver;
        const QRect card = option.rect.adjusted(5, 5, -5, -5);
        QColor surface = option.palette.color(QPalette::AlternateBase);
        if (selected) { surface = option.palette.color(QPalette::Highlight); surface.setAlpha(45); }
        else if (hovered) { surface = option.palette.color(QPalette::Highlight); surface.setAlpha(20); }
        p->setPen(selected ? QPen(option.palette.color(QPalette::Highlight), 1.5) : Qt::NoPen);
        p->setBrush(surface);
        p->drawRoundedRect(card, 8, 8);
        const QRect preview = card.adjusted(8, 8, -8, -30);
        const bool folder = index.data(QVGalleryModel::DirectoryRole).toBool();
        const bool video = index.data(QVGalleryModel::MediaTypeRole).toInt()
                == int(QVMediaCatalog::MediaType::Video);
        const QImage image = qvariant_cast<QImage>(index.data(Qt::DecorationRole));
        if (!image.isNull()) {
            const QSize size = image.size().scaled(preview.size(), Qt::KeepAspectRatio);
            const QRect target(preview.center() - QPoint(size.width() / 2, size.height() / 2), size);
            p->setRenderHint(QPainter::SmoothPixmapTransform);
            p->drawImage(target, image);
        } else {
            const QPointF c = preview.center();
            QColor accent = option.palette.color(QPalette::Highlight);
            p->setPen(Qt::NoPen);
            p->setBrush(accent);
            if (folder) {
                p->drawRoundedRect(QRectF(c.x() - 27, c.y() - 19, 24, 12), 3, 3);
                p->drawRoundedRect(QRectF(c.x() - 27, c.y() - 13, 54, 35), 4, 4);
            } else if (video) {
                QPainterPath play;
                play.moveTo(c + QPointF(-10, -16));
                play.lineTo(c + QPointF(16, 0));
                play.lineTo(c + QPointF(-10, 16));
                play.closeSubpath();
                p->drawPath(play);
            } else {
                p->setPen(QPen(accent, 2));
                p->setBrush(Qt::NoBrush);
                p->drawRoundedRect(QRectF(c.x() - 23, c.y() - 18, 46, 36), 4, 4);
                p->drawEllipse(c + QPointF(9, -6), 4, 4);
                p->drawLine(c + QPointF(-21, 14), c + QPointF(-4, -3));
                p->drawLine(c + QPointF(-4, -3), c + QPointF(12, 14));
            }
        }
        p->setPen(option.palette.color(QPalette::Text));
        p->setFont(option.font);
        const QRect caption(card.left() + 8, card.bottom() - 25, card.width() - 16, 22);
        p->drawText(caption, Qt::AlignCenter,
                    option.fontMetrics.elidedText(index.data().toString(), Qt::ElideMiddle, caption.width()));
        p->restore();
    }
};

QPushButton *button(const QString &text, QWidget *parent)
{
    auto *result = new QPushButton(text, parent);
    result->setMinimumHeight(32);
    result->setCursor(Qt::PointingHandCursor);
    return result;
}
}

QVGalleryView::QVGalleryView(QWidget *parent) : QWidget(parent)
{
    setObjectName("galleryView");
    setAutoFillBackground(true);
    setBackgroundRole(QPalette::Base);
    setAcceptDrops(true);
    auto *layout = new QVBoxLayout(this);
    layout->setContentsMargins(16, 12, 16, 12);
    pages = new QStackedWidget(this);
    layout->addWidget(pages);

    auto *home = new QWidget(pages);
    auto *homeLayout = new QVBoxLayout(home);
    homeLayout->addStretch();
    auto *content = new QWidget(home);
    content->setMaximumWidth(400);
    auto *intro = new QVBoxLayout(content);
    intro->setSpacing(12);
    auto *open = button(tr("Open"), content);
    open->setObjectName("homeOpen");
    open->setMinimumWidth(160);
    intro->addWidget(open);
    connect(open, &QPushButton::clicked, this, &QVGalleryView::openFileRequested);
    recentLayout = new QVBoxLayout;
    recentLayout->setSpacing(4);
    intro->addLayout(recentLayout);
    homeLayout->addWidget(content, 0, Qt::AlignHCenter);
    homeLayout->addStretch();
    pages->addWidget(home);

    auto *gallery = new QWidget(pages);
    auto *galleryLayout = new QVBoxLayout(gallery);
    galleryLayout->setContentsMargins(0, 0, 0, 0);
    model = new QVGalleryModel(this);
    list = new GalleryListView(gallery);
    list->setProperty("preferredTileWidth", 176);
    layoutTimer = new QTimer(this);
    layoutTimer->setSingleShot(true);
    connect(layoutTimer, &QTimer::timeout, this, [this] {
        list->setGridSize(galleryCellSize(list->maximumViewportSize().width(), list->property("preferredTileWidth").toInt()));
        list->doItemsLayout();
        restoreScrollPosition();
    });
    list->setObjectName("folderGrid");
    list->viewport()->installEventFilter(this);
    list->setModel(model);
    list->setItemDelegate(new GalleryDelegate(list));
    list->setViewMode(QListView::IconMode);
    list->setResizeMode(QListView::Adjust);
    list->setMovement(QListView::Static);
    list->setSpacing(0);
    list->setUniformItemSizes(false);
    list->setLayoutMode(QListView::Batched);
    list->setBatchSize(100);
    list->setSelectionMode(QAbstractItemView::ExtendedSelection);
    list->setSelectionRectVisible(true);
    list->setDragDropMode(QAbstractItemView::NoDragDrop);
    list->setEditTriggers(QAbstractItemView::NoEditTriggers);
    list->setVerticalScrollMode(QAbstractItemView::ScrollPerPixel);
    list->setVerticalScrollBarPolicy(Qt::ScrollBarAlwaysOff);
    list->setHorizontalScrollBarPolicy(Qt::ScrollBarAlwaysOff);
    list->setFrameShape(QFrame::NoFrame);
    list->setMouseTracking(true);
    connect(list->selectionModel(), &QItemSelectionModel::selectionChanged,
            this, &QVGalleryView::selectionChanged);
    connect(list->verticalScrollBar(), &QScrollBar::valueChanged, model,
            [this] { model->retryVisibleThumbnails(); });
    connect(list->verticalScrollBar(), &QScrollBar::rangeChanged, this, [this] {
        if (restoreScroll && !folderLoading)
            QTimer::singleShot(0, this, &QVGalleryView::restoreScrollPosition);
    });
    galleryLayout->addWidget(list, 1);
    selectionBar = new QWidget(list);
    selectionBar->setObjectName("gallerySelectionBar");
    selectionBar->setAttribute(Qt::WA_StyledBackground);
    auto *selectionLayout = new QHBoxLayout(selectionBar);
    selectionLayout->setContentsMargins(12, 6, 12, 6);
    selectionCount = new QLabel(selectionBar);
    selectionLayout->addWidget(selectionCount);
    viewGroup = new QPushButton(tr("View Group"), selectionBar);
    viewGroup->setObjectName("galleryViewGroup");
    selectionLayout->addWidget(viewGroup);
    connect(viewGroup, &QPushButton::clicked, this, &QVGalleryView::groupActivated);
    selectionCount->setObjectName("gallerySelectionCount");
    selectionCount->setAttribute(Qt::WA_TransparentForMouseEvents);
    selectionCount->setAlignment(Qt::AlignCenter);
    selectionBar->hide();
    connect(list->selectionModel(), &QItemSelectionModel::selectionChanged,
            this, &QVGalleryView::updateSelectionCount);
    connect(model, &QAbstractItemModel::modelReset, this, &QVGalleryView::updateSelectionCount);
    empty = new QLabel(gallery);
    empty->setAlignment(Qt::AlignCenter);
    empty->setWordWrap(true);
    galleryLayout->addWidget(empty, 1);
    pages->addWidget(gallery);
    const auto activate = [this](const QModelIndex &index) {
        if (index.isValid()) emit pathActivated(index.data(QVGalleryModel::PathRole).toString());
    };
    connect(list, &QListView::doubleClicked, this, activate);
    // activated can also be emitted for a platform's single-click activation.
    // Keyboard activation is handled separately to avoid opening a file twice.
    const auto activateSelection = [this, activate] {
        if (selectedMediaFiles().size() > 1) emit groupActivated();
        else activate(list->currentIndex());
    };
    auto *enter = new QShortcut(QKeySequence(Qt::Key_Return), list);
    enter->setContext(Qt::WidgetShortcut);
    connect(enter, &QShortcut::activated, this, activateSelection);
    auto *keypadEnter = new QShortcut(QKeySequence(Qt::Key_Enter), list);
    keypadEnter->setContext(Qt::WidgetShortcut);
    connect(keypadEnter, &QShortcut::activated, this, activateSelection);
    connect(model, &QVGalleryModel::folderLoaded, this, [this](const QString &error) {
        folderLoading = false;
        const int count = model->rowCount();
        empty->setText(error);
        empty->setVisible(!error.isEmpty());
        list->setVisible(error.isEmpty());
        layoutTimer->start(0);
        for (int i = 0; i < count; ++i) {
            const QModelIndex index = model->index(i);
            if (index.data(QVGalleryModel::PathRole).toString() == selection) {
                restoreScroll = false;
                list->setCurrentIndex(index);
                list->scrollTo(index, QAbstractItemView::PositionAtCenter);
                break;
            }
        }
        if (count > 0 && !list->currentIndex().isValid())
            list->selectionModel()->setCurrentIndex(model->index(0), QItemSelectionModel::NoUpdate);
        if (count > 0 && isVisible() && !isHome()) list->setFocus();
    });
}

bool QVGalleryView::isHome() const { return pages->currentIndex() == 0; }

void QVGalleryView::setBackgroundColor(const QColor &color)
{
    const QColor text = qGray(color.rgb()) < 128 ? QColor(Qt::white) : QColor(Qt::black);
    const auto tint = [&](int amount) {
        return QColor((color.red() * (100 - amount) + text.red() * amount) / 100,
                      (color.green() * (100 - amount) + text.green() * amount) / 100,
                      (color.blue() * (100 - amount) + text.blue() * amount) / 100);
    };
    QPalette colors = palette();
    colors.setColor(QPalette::Base, color);
    colors.setColor(QPalette::Window, color);
    colors.setColor(QPalette::AlternateBase, tint(6));
    colors.setColor(QPalette::Text, text);
    colors.setColor(QPalette::WindowText, text);
    colors.setColor(QPalette::ButtonText, text);
    // Explicit button surfaces also keep text readable with native light styles.
    setStyleSheet(QString("QLabel { color: %1; }"
                          "QWidget#gallerySelectionBar { background: %2; border: 1px solid %3;"
                          " border-radius: 12px; padding: 6px 12px; }"
                          "QPushButton { color: %1; background: %2; border: 1px solid %3;"
                          " border-radius: 6px; padding: 4px 12px; outline: none; }"
                          "QPushButton:flat { background: transparent; border-color: transparent; }"
                          "QPushButton:hover, QPushButton:pressed { background: %3; }"
                          "QPushButton:focus { border-color: %4; }")
                  .arg(text.name(), tint(6).name(), tint(15).name(), colors.color(QPalette::Highlight).name()));
    // Stylesheet widgets do not inherit their parent's palette automatically.
    // Apply after restyling, which restores the previous palette during unpolish.
    setPalette(colors);
    list->setPalette(colors);
    list->viewport()->setPalette(colors);
}

bool QVGalleryView::eventFilter(QObject *watched, QEvent *event)
{
    if (watched == list->viewport() && event->type() == QEvent::Resize) {
        model->retryVisibleThumbnails();
        layoutTimer->start(0);
        positionSelectionCount();
    }
    if (watched == list->viewport() && event->type() == QEvent::MouseButtonPress) {
        const auto *mouse = static_cast<QMouseEvent *>(event);
        if (mouse->button() == Qt::LeftButton && mouse->modifiers() == Qt::NoModifier
                && !list->indexAt(mouse->pos()).isValid()) {
            list->clearSelection();
            list->setCurrentIndex(QModelIndex());
        }
    }
    if (watched == list->viewport() && event->type() == QEvent::MouseButtonDblClick) {
        const auto *mouse = static_cast<QMouseEvent *>(event);
        if (mouse->button() == Qt::LeftButton && !list->indexAt(mouse->pos()).isValid()) {
            emit fullscreenRequested();
            event->accept();
            return true;
        }
    }
    return QWidget::eventFilter(watched, event);
}

QStringList QVGalleryView::selectedPaths() const
{
    QStringList paths;
    const auto indexes = list->selectionModel()->selectedIndexes();
    for (const auto &index : indexes) paths.append(index.data(QVGalleryModel::PathRole).toString());
    return paths;
}

QList<QVMediaCatalog::MediaFile> QVGalleryView::selectedMediaFiles() const
{
    const QStringList selected = selectedPaths();
    const QSet<QString> paths(selected.cbegin(), selected.cend());
    QList<QVMediaCatalog::MediaFile> files;
    for (const auto &file : model->mediaFiles()) {
        if (paths.contains(file.absoluteFilePath)) files.append(file);
    }
    return files;
}

bool QVGalleryView::hasSelection() const { return list->selectionModel()->hasSelection(); }

void QVGalleryView::restoreScrollPosition()
{
    if (!restoreScroll || folderLoading) return;
    auto *bar = list->verticalScrollBar();
    bar->setValue(scrollPosition);
    // Later batches may still extend the range. Clamp only once all rows exist.
    if (bar->maximum() >= scrollPosition || model->rowCount() == 0
            || list->visualRect(model->index(model->rowCount() - 1)).isValid())
        restoreScroll = false;
}

void QVGalleryView::zoom(int direction)
{
    const int next = qBound(-4, tileZoom + direction, 6);
    if (next == tileZoom) return;
    const int oldHeight = qMax(1, list->gridSize().height());
    tileZoom = next;
    const int width = qRound(176 * std::pow(1.25, tileZoom));
    list->setProperty("preferredTileWidth", width);
    const int newHeight = galleryCellSize(list->viewport()->width(), width).height();
    scrollPosition = list->verticalScrollBar()->value();
    if (scrollPosition > 0) scrollPosition = qRound(double(scrollPosition) * newHeight / oldHeight);
    restoreScroll = true;
    layoutTimer->start(0);
}

void QVGalleryView::resetZoom()
{
    zoom(-tileZoom);
}

void QVGalleryView::clearSelection()
{
    list->clearSelection();
    list->setCurrentIndex(QModelIndex());
}

void QVGalleryView::updateSelectionCount()
{
    const int count = list->selectionModel()->selectedIndexes().size();
    selectionCount->setText(tr("%1 selected").arg(count));
    viewGroup->setVisible(selectedMediaFiles().size() > 1);
    selectionBar->adjustSize();
    positionSelectionCount();
    selectionBar->setVisible(count > 0);
    selectionBar->raise();
}

void QVGalleryView::positionSelectionCount()
{
    selectionBar->move(qMax(0, (list->viewport()->width() - selectionBar->width()) / 2),
                         qMax(0, list->viewport()->height() - selectionBar->height() - 12));
}

void QVGalleryView::showHome(const QList<QPair<QString, QString>> &recents)
{
    restoreScroll = false;
    model->clear();
    pages->setCurrentIndex(0);
    while (auto *item = recentLayout->takeAt(0)) { delete item->widget(); delete item; }
    if (!recents.isEmpty()) {
        auto *label = new QLabel(tr("Recently opened"), this);
        recentLayout->addWidget(label);
        for (const auto &recent : recents) {
            auto *entry = button(fontMetrics().elidedText(recent.first, Qt::ElideMiddle, 340), this);
            entry->setFlat(true);
            entry->setToolTip(QDir::toNativeSeparators(recent.second));
            entry->setStyleSheet("text-align: left; padding: 4px 8px;");
            connect(entry, &QPushButton::clicked, this, [this, recent] { emit pathActivated(recent.second); });
            recentLayout->addWidget(entry);
        }
    }
}

void QVGalleryView::openFolder(const QString &path, const QVMediaCatalog::ScanOptions &options,
                               const QString &selectedPath, bool preserveScroll)
{
    scrollPosition = preserveScroll ? list->verticalScrollBar()->value() : -galleryTopSpace;
    restoreScroll = true;
    folderLoading = true;
    selection = selectedPath;
    pages->setCurrentIndex(1);
    empty->hide();
    list->show();
    model->openFolder(path, options);
    list->setFocus();
}

void QVGalleryView::dragEnterEvent(QDragEnterEvent *event)
{
    if (event->mimeData()->hasUrls() && !event->mimeData()->urls().isEmpty()
            && event->mimeData()->urls().first().isLocalFile()) event->acceptProposedAction();
}

void QVGalleryView::dropEvent(QDropEvent *event)
{
    if (event->mimeData()->hasUrls() && !event->mimeData()->urls().isEmpty()
            && event->mimeData()->urls().first().isLocalFile()) {
        emit pathActivated(event->mimeData()->urls().first().toLocalFile());
        event->acceptProposedAction();
    }
}

void QVGalleryView::showEvent(QShowEvent *event)
{
    model->setThumbnailsEnabled(true);
    list->viewport()->update();
    QWidget::showEvent(event);
}

void QVGalleryView::hideEvent(QHideEvent *event)
{
    model->setThumbnailsEnabled(false);
    QWidget::hideEvent(event);
}
