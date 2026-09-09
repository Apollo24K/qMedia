#include "qvlayershud.h"
#include <QWidgetAction>
#include <QApplication>
#include <QAction>
#include <QChildEvent>
#include <QStyle>
#include <QStyleOptionViewItem>
#include <QComboBox>
#include <QDragEnterEvent>
#include <QDropEvent>
#include <QKeyEvent>
#include <QLabel>
#include <QListWidget>
#include <QMouseEvent>
#include <QMenu>
#include <QPainter>
#include <QStyledItemDelegate>
#include <QWheelEvent>
#include <QScrollBar>
#include <QSettings>
#include <QSignalBlocker>
#include <QSlider>
#include <QToolButton>
#include <QVBoxLayout>
#include <functional>

namespace {
constexpr int margin = 12;
constexpr int snapDistance = 16;

QIcon icon(const QString &name) { return QIcon(":/layers/" + name + ".svg"); }

QToolButton *button(QWidget *parent, const QString &glyph, const QString &tip, const QString &name)
{
    auto *result = new QToolButton(parent);
    result->setIcon(icon(glyph));
    result->setIconSize(QSize(18, 18));
    result->setFixedSize(30, 30);
    result->setToolTip(tip);
    result->setAccessibleName(tip);
    result->setObjectName(name);
    result->setAutoRaise(true);
    return result;
}

class StrengthSlider : public QSlider
{
public:
    explicit StrengthSlider(QWidget *parent) : QSlider(Qt::Horizontal, parent) { }
protected:
    void wheelEvent(QWheelEvent *event) override
    {
        remainder += event->angleDelta().y() ? event->angleDelta().y() : event->angleDelta().x();
        setValue(value() + remainder / 120);
        remainder %= 120;
        event->accept();
    }
private:
    int remainder = 0;
};

class ElidedLabel : public QLabel
{
public:
    explicit ElidedLabel(QWidget *parent) : QLabel(parent) { }
protected:
    void paintEvent(QPaintEvent *) override
    {
        QPainter painter(this);
        painter.setPen(palette().color(QPalette::WindowText));
        painter.drawText(rect(), Qt::AlignVCenter | Qt::AlignLeft,
                         fontMetrics().elidedText(text(), Qt::ElideMiddle, width()));
    }
};

class LayerDelegate : public QStyledItemDelegate
{
public:
    explicit LayerDelegate(QObject *parent) : QStyledItemDelegate(parent) { }
    QRect iconRect(const QModelIndex &index, const QRect &rect, const QListWidget *view) const
    {
        QStyleOptionViewItem option;
        option.initFrom(view);
        option.widget = view;
        option.decorationSize = view->iconSize();
        initStyleOption(&option, index);
        option.rect = rect;
        return view->style()->subElementRect(QStyle::SE_ItemViewItemDecoration, &option, view);
    }
};

class LayerList : public QListWidget
{
public:
    explicit LayerList(QWidget *parent) : QListWidget(parent)
    {
        setIconSize(QSize(18, 18));
        setItemDelegate(new LayerDelegate(this));
    }
    std::function<void(quint64)> iconClicked;
    std::function<void(quint64, int)> moved;
    std::function<void()> removed;
protected:
    void mousePressEvent(QMouseEvent *event) override
    {
        pressedIcon = 0;
        auto *item = itemAt(event->pos());
        if (event->button() == Qt::LeftButton && item && item->data(Qt::UserRole + 1).toBool()
                && decorationRect(item).contains(event->pos())) {
            setCurrentItem(item);
            pressedIcon = item->data(Qt::UserRole).toULongLong();
            event->accept();
            return;
        }
        QListWidget::mousePressEvent(event);
    }
    void mouseReleaseEvent(QMouseEvent *event) override
    {
        if (pressedIcon) {
            const auto id = pressedIcon;
            pressedIcon = 0;
            auto *item = itemAt(event->pos());
            if (item && item->data(Qt::UserRole).toULongLong() == id
                    && decorationRect(item).contains(event->pos()) && iconClicked) iconClicked(id);
            event->accept();
            return;
        }
        QListWidget::mouseReleaseEvent(event);
    }
    void mouseDoubleClickEvent(QMouseEvent *event) override
    {
        auto *item = itemAt(event->pos());
        if (item && item->data(Qt::UserRole + 1).toBool() && decorationRect(item).contains(event->pos())) {
            event->accept();
            return;
        }
        QListWidget::mouseDoubleClickEvent(event);
    }
    void dropEvent(QDropEvent *event) override
    {
        if (event->source() != this || !currentItem()) { event->ignore(); return; }
#if QT_VERSION >= QT_VERSION_CHECK(6, 0, 0)
        const QPoint point = event->position().toPoint();
#else
        const QPoint point = event->pos();
#endif
        auto *target = itemAt(point);
        int destination = target ? row(target) : count();
        if (target && point.y() > visualItemRect(target).center().y()) ++destination;
        const int source = currentRow();
        if (destination > source) --destination;
        const quint64 id = currentItem()->data(Qt::UserRole).toULongLong();
        // Only the shared model mutates order. Never allow QListWidget to remove
        // its drag source a second time after rebuilding from that model.
        event->setDropAction(Qt::IgnoreAction);
        event->accept();
        if (moved) moved(id, qBound(0, destination, count() - 1));
    }
    bool event(QEvent *event) override
    {
        if (event->type() == QEvent::ShortcutOverride) {
            auto *key = static_cast<QKeyEvent *>(event);
            if (key->key() == Qt::Key_Delete || key->key() == Qt::Key_Backspace
                    || key->key() == Qt::Key_Up || key->key() == Qt::Key_Down
                    || key->key() == Qt::Key_Home || key->key() == Qt::Key_End
                    || key->key() == Qt::Key_Space || key->key() == Qt::Key_F2) {
                event->accept();
                return true;
            }
        }
        return QListWidget::event(event);
    }
    void keyPressEvent(QKeyEvent *event) override
    {
        if (event->key() == Qt::Key_Delete || event->key() == Qt::Key_Backspace) {
            if (removed) removed();
            event->accept();
        } else if (event->key() == Qt::Key_Space && currentItem()) {
            currentItem()->setCheckState(currentItem()->checkState() == Qt::Checked
                                         ? Qt::Unchecked : Qt::Checked);
            event->accept();
        } else QListWidget::keyPressEvent(event);
    }
private:
    quint64 pressedIcon = 0;
    QRect decorationRect(QListWidgetItem *item) const
    {
        return static_cast<LayerDelegate *>(itemDelegate())->iconRect(indexFromItem(item), visualItemRect(item), this);
    }
};
}

QVOverlayPanel::QVOverlayPanel(QWidget *viewport, bool canResize, bool right)
    : QFrame(viewport->parentWidget()), resizable(canResize), defaultRight(right), viewport(viewport)
{
    setObjectName("layersOverlay");
    setMouseTracking(true);
    setAttribute(Qt::WA_StyledBackground);
    setStyleSheet(QStringLiteral(
        "QFrame#layersOverlay { background: rgba(32,35,41,246); border: 1px solid #454a54; border-radius: 9px; }"
        "QWidget { color: #e0e4ec; font-size: 12px; }"
        "QLabel, QScrollArea, QScrollArea > QWidget > QWidget { background: transparent; border: none; }"
        "QMenu { background: #202329; border: 1px solid #454a54; padding: 4px; }"
        "QMenu::item { padding: 5px 20px; } QMenu::item:selected { background: #3b4b65; }"
        "QMenu::item:disabled { color: #77808e; }"
        "QToolButton { border: none; border-radius: 5px; background: transparent; }"
        "QToolButton:hover, QPushButton:hover { background: #424a59; }"
        "QToolButton:pressed { background: #52617a; }"
        "QToolButton:focus { border: 1px solid #859cc1; }"
        "QListWidget { background: #191c22; border: 1px solid #343a44; border-radius: 5px; outline: none; }"
        "QListWidget::item { padding: 7px 4px; border-radius: 4px; }"
        "QListWidget::item:selected { background: #3b4b65; color: #ffffff; }"
        "QListWidget::item:hover:!selected { background: #2d333e; }"
        "QListWidget::indicator { width: 18px; height: 18px; }"
        "QListWidget::indicator:checked { image: url(:/layers/eye.svg); }"
        "QListWidget::indicator:unchecked { image: url(:/layers/eye-off.svg); }"
        "QLineEdit, QComboBox, QSpinBox { background: #191c22; border: 1px solid #414752; border-radius: 4px; padding: 4px; selection-background-color: #526b92; }"
        "QComboBox QAbstractItemView { background: #252a33; color: #e0e4ec; selection-background-color: #3b4b65; }"
        "QPushButton { background: #303641; border: 1px solid #454c58; border-radius: 4px; padding: 5px; }"
        "QGroupBox { border: 1px solid #414752; border-radius: 4px; margin-top: 10px; padding: 8px 4px 4px; }"
        "QGroupBox::title { subcontrol-origin: margin; left: 6px; }"
        "QSlider::groove:horizontal { height: 3px; background: #505966; border-radius: 1px; }"
        "QSlider::sub-page:horizontal { background: #a4b8d7; }"
        "QSlider::handle:horizontal { width: 10px; margin: -4px 0; border-radius: 5px; background: #d7e2f2; }"
        "QScrollBar:vertical { background: transparent; width: 7px; margin: 0; }"
        "QScrollBar::handle:vertical { background: #525b69; min-height: 20px; border-radius: 3px; }"
        "QScrollBar::add-line:vertical, QScrollBar::sub-line:vertical { height: 0; }"
        "QScrollBar::add-page:vertical, QScrollBar::sub-page:vertical { background: transparent; }"));
    viewport->installEventFilter(this);
    hide();
}

void QVOverlayPanel::setDragHandle(QWidget *handle)
{
    handle->setCursor(Qt::SizeAllCursor);
    handle->setToolTip(tr("Drag to move · Double-click to reset position"));
    handle->installEventFilter(this);
}

void QVOverlayPanel::restorePlacement(const QString &key, const QSize &size)
{
    defaultSize = size;
    const QSettings settings;
    preferredSize = (resizable ? settings.value(key + "/size", size).toSize() : size).expandedTo(QSize(40, 80));
    horizontalAnchor = qBound(0, settings.value(key + "/horizontal", defaultRight ? 2 : 1).toInt(), 2);
    verticalAnchor = qBound(0, settings.value(key + "/vertical", defaultRight ? 2 : 1).toInt(), 2);
    // Migrate the previous default top-right placement once. Free placements stay put.
    if (defaultRight && !settings.value(key + "/bottomDefault", false).toBool()
            && horizontalAnchor == 2 && verticalAnchor == 1) verticalAnchor = 2;
    freePosition = settings.value(key + "/position", QPoint(margin, margin)).toPoint();
    fitToViewport();
}

void QVOverlayPanel::savePlacement(const QString &key) const
{
    QSettings settings;
    settings.setValue(key + "/bottomDefault", true);
    settings.setValue(key + "/size", preferredSize);
    settings.setValue(key + "/horizontal", horizontalAnchor);
    settings.setValue(key + "/vertical", verticalAnchor);
    settings.setValue(key + "/position", freePosition);
}

void QVOverlayPanel::resetPlacement()
{
    preferredSize = defaultSize;
    horizontalAnchor = defaultRight ? 2 : 1;
    verticalAnchor = defaultRight ? 2 : 1;
    fitToViewport();
}

void QVOverlayPanel::fitToViewport()
{
    if (preferredSize.isEmpty()) return;
    const QSize bounds = viewport->size();
    const int inset = bounds.width() > 100 && bounds.height() > 100 ? margin : 0;
    const QSize size(qMin(preferredSize.width(), qMax(1, bounds.width() - 2 * inset)),
                     qMin(preferredSize.height(), qMax(1, bounds.height() - 2 * inset)));
    QPoint position = freePosition;
    if (horizontalAnchor) position.setX(horizontalAnchor == 1 ? inset : bounds.width() - size.width() - inset);
    if (verticalAnchor) position.setY(verticalAnchor == 1 ? inset : bounds.height() - size.height() - inset);
    position.setX(qBound(0, position.x(), qMax(0, bounds.width() - size.width())));
    position.setY(qBound(0, position.y(), qMax(0, bounds.height() - size.height())));
    setGeometry(QRect(position + viewport->pos(), size));
}

Qt::Edges QVOverlayPanel::edgesAt(const QPoint &point) const
{
    Qt::Edges edges;
    if (!resizable) return edges;
    if (point.x() < 7) edges |= Qt::LeftEdge;
    if (point.x() >= width() - 7) edges |= Qt::RightEdge;
    if (point.y() < 7) edges |= Qt::TopEdge;
    if (point.y() >= height() - 7) edges |= Qt::BottomEdge;
    return edges;
}

void QVOverlayPanel::beginDrag(QMouseEvent *event, bool handle)
{
    if (event->button() != Qt::LeftButton) return;
    resizing = handle ? Qt::Edges() : edgesAt(event->pos());
    if (!handle && !resizing) return;
    dragging = true;
    if (handle) setCursor(Qt::SizeAllCursor);
    dragOrigin = event->globalPos();
    dragGeometry = geometry().translated(-viewport->pos());
    grabMouse();
    raise();
    event->accept();
}

void QVOverlayPanel::drag(QMouseEvent *event)
{
    if (!dragging) return;
    const QPoint delta = event->globalPos() - dragOrigin;
    QRect next = dragGeometry;
    if (!resizing) {
        next.translate(delta);
    } else {
        const QSize bounds = viewport->size();
        const int minWidth = qMin(240, dragGeometry.width());
        const int minHeight = qMin(220, dragGeometry.height());
        if (resizing & Qt::LeftEdge) next.setLeft(qBound(0, dragGeometry.left() + delta.x(), next.right() - minWidth + 1));
        if (resizing & Qt::RightEdge) next.setRight(qBound(next.left() + minWidth - 1, dragGeometry.right() + delta.x(), bounds.width() - 1));
        if (resizing & Qt::TopEdge) next.setTop(qBound(0, dragGeometry.top() + delta.y(), next.bottom() - minHeight + 1));
        if (resizing & Qt::BottomEdge) next.setBottom(qBound(next.top() + minHeight - 1, dragGeometry.bottom() + delta.y(), bounds.height() - 1));
        preferredSize = next.size();
    }
    horizontalAnchor = verticalAnchor = 0;
    freePosition = next.topLeft();
    fitToViewport();
}

void QVOverlayPanel::finishDrag()
{
    if (!dragging) return;
    dragging = false;
    releaseMouse();
    resizing = {};
    setCursor(Qt::ArrowCursor);
    const QSize bounds = viewport->size();
    const QRect relative = geometry().translated(-viewport->pos());
    if (qAbs(relative.x() - margin) < snapDistance) horizontalAnchor = 1;
    else if (qAbs(bounds.width() - relative.right() - 1 - margin) < snapDistance) horizontalAnchor = 2;
    if (qAbs(relative.y() - margin) < snapDistance) verticalAnchor = 1;
    else if (qAbs(bounds.height() - relative.bottom() - 1 - margin) < snapDistance) verticalAnchor = 2;
    fitToViewport();
}

bool QVOverlayPanel::eventFilter(QObject *watched, QEvent *event)
{
    if (watched == viewport) {
        if (event->type() == QEvent::Resize || event->type() == QEvent::Move) fitToViewport();
    } else {
        if (event->type() == QEvent::MouseButtonPress) { beginDrag(static_cast<QMouseEvent *>(event), true); return true; }
        if (event->type() == QEvent::MouseMove && dragging) { drag(static_cast<QMouseEvent *>(event)); return true; }
        if (event->type() == QEvent::MouseButtonRelease && dragging) { finishDrag(); return true; }
        if (event->type() == QEvent::MouseButtonDblClick) { resetPlacement(); return true; }
    }
    return QFrame::eventFilter(watched, event);
}

bool QVOverlayPanel::event(QEvent *event)
{
    if (event->type() == QEvent::ChildPolished) {
        // Edge cursors belong to the frame, not its contents. Qt need not send
        // another frame mouseMove when entering a child, so inheritance can
        // otherwise leave the resize cursor over lists, sliders, and buttons.
        auto *child = qobject_cast<QWidget *>(static_cast<QChildEvent *>(event)->child());
        if (child && !child->testAttribute(Qt::WA_SetCursor))
            child->setCursor(Qt::ArrowCursor);
    }
    if ((event->type() == QEvent::Leave && !dragging) || event->type() == QEvent::Hide)
        setCursor(Qt::ArrowCursor);
    if (event->type() == QEvent::ShortcutOverride) {
        const int key = static_cast<QKeyEvent *>(event)->key();
        if (key == Qt::Key_Left || key == Qt::Key_Right || key == Qt::Key_Up
                || key == Qt::Key_Down || key == Qt::Key_Home || key == Qt::Key_End
                || key == Qt::Key_PageUp || key == Qt::Key_PageDown || key == Qt::Key_Space
                || key == Qt::Key_Delete || key == Qt::Key_Backspace || key == Qt::Key_F2) {
            event->accept();
            return true;
        }
    }
    if (event->type() == QEvent::Hide && dragging) finishDrag();
    // Unhandled overlay input must not bubble into canvas zoom, panning, file
    // drops, or the window's double-click fullscreen gesture.
    if (event->type() == QEvent::Wheel || event->type() == QEvent::MouseButtonDblClick
            || event->type() == QEvent::ContextMenu || event->type() == QEvent::KeyPress) {
        event->accept();
        return true;
    }
    return QFrame::event(event);
}

void QVOverlayPanel::mousePressEvent(QMouseEvent *event) { beginDrag(event, false); event->accept(); }
void QVOverlayPanel::mouseMoveEvent(QMouseEvent *event)
{
    if (dragging) drag(event);
    else {
        const auto edges = edgesAt(event->pos());
        const bool horizontal = edges & (Qt::LeftEdge | Qt::RightEdge);
        const bool vertical = edges & (Qt::TopEdge | Qt::BottomEdge);
        setCursor(horizontal && vertical ? ((edges == (Qt::LeftEdge | Qt::TopEdge)
                     || edges == (Qt::RightEdge | Qt::BottomEdge)) ? Qt::SizeFDiagCursor : Qt::SizeBDiagCursor)
                  : horizontal ? Qt::SizeHorCursor : vertical ? Qt::SizeVerCursor : Qt::ArrowCursor);
    }
    event->accept();
}
void QVOverlayPanel::mouseReleaseEvent(QMouseEvent *event) { finishDrag(); event->accept(); }

QVLayersHud::QVLayersHud(QVLayerModel *layerModel, QWidget *viewport)
    : QObject(viewport), model(layerModel)
{
    panel = new QVOverlayPanel(viewport, true, true);
    panel->setAccessibleName(tr("Layers"));
    auto *layout = new QVBoxLayout(panel);
    layout->setSizeConstraint(QLayout::SetNoConstraint);
    layout->setContentsMargins(9, 8, 9, 9);
    layout->setSpacing(7);
    auto *header = new QHBoxLayout;
    auto *title = new QLabel(tr("Layers"), panel);
    title->setStyleSheet("font-weight: 600; font-size: 13px;");
    title->setMinimumHeight(27);
    panel->setDragHandle(title);
    header->addWidget(title);
    sourceLabel = new ElidedLabel(panel);
    sourceLabel->setObjectName("layersSource");
    sourceLabel->setStyleSheet("color: #a3acba; font-size: 11px;");
    sourceLabel->setTextFormat(Qt::PlainText);
    sourceLabel->setMinimumWidth(0);
    sourceLabel->setSizePolicy(QSizePolicy::Ignored, QSizePolicy::Preferred);
    panel->setDragHandle(sourceLabel);
    header->addWidget(sourceLabel, 1);
    auto *close = button(panel, "close", tr("Hide Layers HUD"), "layersClose");
    header->addWidget(close);
    layout->addLayout(header);

    properties = new QWidget(panel);
    auto *propertyLayout = new QHBoxLayout(properties);
    propertyLayout->setContentsMargins(0, 0, 0, 0);
    propertyLayout->setSpacing(6);
    strength = new StrengthSlider(properties);
    strength->setObjectName("layerStrength");
    strength->setAccessibleName(tr("Layer strength"));
    strength->setToolTip(tr("Layer strength · Scroll to adjust by 1%"));
    strength->setRange(0, 100);
    strength->setMinimumWidth(35);
    strengthValue = new QLabel(properties);
    strengthValue->setObjectName("layerStrengthValue");
    strengthValue->setAccessibleName(tr("Layer strength percentage"));
    strengthValue->setAlignment(Qt::AlignRight | Qt::AlignVCenter);
    strengthValue->setFixedWidth(strengthValue->fontMetrics().horizontalAdvance("100%") + 4);
    strengthValue->setToolTip(strength->toolTip());
    strengthValue->installEventFilter(this);
    blend = new QComboBox(properties);
    blend->setObjectName("layerBlend");
    blend->setAccessibleName(tr("Blend mode"));
    blend->setToolTip(tr("Blend mode"));
    blend->addItems({ tr("Normal"), tr("Multiply"), tr("Screen"), tr("Overlay"), tr("Darken"), tr("Lighten") });
    propertyLayout->addWidget(strength, 1);
    propertyLayout->addWidget(strengthValue);
    propertyLayout->addWidget(blend);
    layout->addWidget(properties);

    auto *layerList = new LayerList(panel);
    list = layerList;
    list->setObjectName("layersList");
    list->setAccessibleName(tr("Layers, top to bottom"));
    list->setToolTip(tr("Drag to reorder · Double-click or F2 to rename · Space toggles visibility"));
    list->setDragDropMode(QAbstractItemView::InternalMove);
    list->setDefaultDropAction(Qt::MoveAction);
    list->setSelectionMode(QAbstractItemView::SingleSelection);
    list->setEditTriggers(QAbstractItemView::DoubleClicked | QAbstractItemView::EditKeyPressed);
    list->setMinimumSize(0, 48);
    list->setHorizontalScrollBarPolicy(Qt::ScrollBarAlwaysOff);
    layout->addWidget(list, 1);
    auto *actions = new QHBoxLayout;
    auto *add = button(panel, "filter", tr("Add filter layer"), "layersAddFilter");
    auto *duplicate = button(panel, "duplicate", tr("Duplicate layer"), "layersDuplicate");
    removeButton = button(panel, "delete", tr("Remove layer"), "layersRemove");
    upButton = button(panel, "up", tr("Move layer up"), "layersUp");
    downButton = button(panel, "down", tr("Move layer down"), "layersDown");
    actions->addWidget(add);
    actions->addWidget(duplicate);
    actions->addWidget(removeButton);
    actions->addStretch();
    actions->addWidget(upButton);
    actions->addWidget(downButton);
    layout->addLayout(actions);

    toolbar = new QVOverlayPanel(viewport, false, false);
    toolbar->setAccessibleName(tr("Editing tools"));
    auto *tools = new QVBoxLayout(toolbar);
    tools->setSizeConstraint(QLayout::SetNoConstraint);
    tools->setContentsMargins(7, 7, 7, 7);
    tools->setSpacing(3);
    auto *grip = new QLabel(toolbar);
    grip->setPixmap(icon("grip").pixmap(18, 12));
    grip->setAlignment(Qt::AlignCenter);
    grip->setFixedHeight(15);
    toolbar->setDragHandle(grip);
    tools->addWidget(grip);
    auto *hand = button(toolbar, "hand", tr("Pan canvas (drag the image)"), "layersPan");
    panButton = hand;
    panButton->setCheckable(true);
    panButton->setChecked(true);
    distortButton = button(toolbar, "distort", tr("Distort (D): drag to push pixels (Ctrl+Z to undo) · Alt+wheel changes brush size · Right-click for size"), "layersDistort");
    distortButton->setCheckable(true);
    distortButton->setContextMenuPolicy(Qt::CustomContextMenu);
    auto *brushMenu = new QMenu(distortButton);
    auto *brushOptions = new QWidget(brushMenu);
    auto *brushLayout = new QVBoxLayout(brushOptions);
    brushLayout->addWidget(new QLabel(tr("Brush radius"), brushOptions));
    auto *brushSize = new QSlider(Qt::Horizontal, brushOptions);
    brushSize->setObjectName("distortBrushSize");
    brushSize->setAccessibleName(tr("Distort brush radius"));
    brushSize->setRange(8, 200);
    brushSize->setValue(48);
    brushSize->setMinimumWidth(160);
    brushLayout->addWidget(brushSize);
    auto *brushAction = new QWidgetAction(brushMenu);
    brushAction->setDefaultWidget(brushOptions);
    brushMenu->addAction(brushAction);
    connect(brushSize, &QSlider::valueChanged, this, &QVLayersHud::distortSizeChanged);
    connect(distortButton, &QWidget::customContextMenuRequested, this,
            [brushMenu, this](const QPoint &pos) { brushMenu->popup(distortButton->mapToGlobal(pos)); });
    connect(distortButton, &QToolButton::clicked, this, [this](bool active) {
        setDistortActive(active);
        emit distortRequested(active);
    });
    cropButton = button(toolbar, "crop", tr("Crop / extend canvas: drag edges, Enter to apply, Esc to cancel. Right-click for options"), "layersCrop");
    cropButton->setCheckable(true);
    cropButton->setContextMenuPolicy(Qt::CustomContextMenu);
    connect(cropButton, &QToolButton::clicked, this, [this](bool active) {
        setCropActive(active);
        emit cropRequested(active);
    });
    connect(cropButton, &QWidget::customContextMenuRequested, this, [this](const QPoint &pos) {
        QMenu menu;
        auto *apply = menu.addAction(tr("Apply canvas bounds"));
        auto *cancel = menu.addAction(tr("Cancel"));
        apply->setEnabled(cropButton->isChecked());
        cancel->setEnabled(cropButton->isChecked());
        menu.addSeparator();
        auto *reset = menu.addAction(tr("Reset canvas to original"));
        auto *chosen = menu.exec(cropButton->mapToGlobal(pos));
        if (chosen == apply) emit cropApplyRequested();
        else if (chosen == cancel) { setCropActive(false); emit cropRequested(false); }
        else if (chosen == reset) emit cropResetRequested();
    });
    auto *filterTool = button(toolbar, "filter", tr("Filters (U)"), "layersFilterTool");
    auto *fit = button(toolbar, "fit", tr("Reset view"), "layersResetView");
    auto *save = button(toolbar, "export", tr("Export media"), "layersExport");
    for (auto *tool : { hand, distortButton, cropButton, filterTool, fit, save }) tools->addWidget(tool);
    tools->addStretch();

    connect(close, &QToolButton::clicked, this, [this] { setVisible(false); });
    connect(hand, &QToolButton::clicked, this, [this, viewport] {
        setCropActive(false);
        emit cropRequested(false);
        setDistortActive(false);
        emit distortRequested(false);
        viewport->setFocus();
    });
    connect(filterTool, &QToolButton::clicked, this, [this] { emit filtersRequested(selectedId()); });
    connect(fit, &QToolButton::clicked, this, &QVLayersHud::resetViewRequested);
    connect(save, &QToolButton::clicked, this, &QVLayersHud::exportRequested);
    connect(add, &QToolButton::clicked, this, &QVLayersHud::addFilter);
    connect(duplicate, &QToolButton::clicked, this, [this] { select(model->duplicate(selectedId())); });
    connect(removeButton, &QToolButton::clicked, this, [this] { model->remove(selectedId()); });
    list->setContextMenuPolicy(Qt::CustomContextMenu);
    connect(list, &QWidget::customContextMenuRequested, this, &QVLayersHud::showLayerMenu);
    layerList->iconClicked = [this](quint64 id) {
        const int index = model->indexOf(id);
        if (index < 0) return;
        if (model->stack().layers[index].kind == QVLayers::Kind::Distort) {
            if (!distortButton->isEnabled()) return;
            setDistortActive(true);
            emit distortRequested(true);
        } else emit filtersRequested(id);
    };
    layerList->removed = [this] { model->remove(selectedId()); };
    layerList->moved = [this](quint64 id, int index) { model->move(id, index); select(id); };
    connect(upButton, &QToolButton::clicked, this, [this] { moveSelection(-1); });
    connect(downButton, &QToolButton::clicked, this, [this] { moveSelection(1); });
    connect(list, &QListWidget::currentRowChanged, this, &QVLayersHud::loadSelection);
    connect(list, &QListWidget::itemChanged, this, [this](QListWidgetItem *item) {
        if (loading) return;
        const int index = model->indexOf(item->data(Qt::UserRole).toULongLong());
        if (index < 0) return;
        auto layer = model->stack().layers[index];
        layer.name = item->text();
        layer.visible = item->checkState() == Qt::Checked;
        model->update(layer);
    });
    connect(blend, QOverload<int>::of(&QComboBox::currentIndexChanged), this, &QVLayersHud::updateSelection);
    connect(strength, &QSlider::valueChanged, this, [this](int value) { strengthValue->setText(QString::number(value) + "%"); });
    connect(strength, &QSlider::valueChanged, this, &QVLayersHud::updateSelection);
    connect(model, &QVLayerModel::changed, this, &QVLayersHud::refresh);
    panel->restorePlacement("layersHud/panel", QSize(292, 490));
    toolbar->restorePlacement("layersHud/tools", QSize(46, 234));
    refresh();
}

QVLayersHud::~QVLayersHud()
{
    panel->savePlacement("layersHud/panel");
    toolbar->savePlacement("layersHud/tools");
    delete panel;
    delete toolbar;
}

bool QVLayersHud::isVisible() const { return !toolbar->isHidden(); }
void QVLayersHud::toggle() { setVisible(!isVisible()); }
void QVLayersHud::setVisible(bool visible)
{
    if (!visible) {
        setCropActive(false);
        emit cropRequested(false);
        setDistortActive(false);
        emit distortRequested(false);
        panel->savePlacement("layersHud/panel");
        toolbar->savePlacement("layersHud/tools");
    }
    panel->setVisible(visible && !cropButton->isChecked());
    toolbar->setVisible(visible);
    if (visible) { panel->fitToViewport(); toolbar->fitToViewport(); panel->raise(); toolbar->raise(); }
    else panel->parentWidget()->setFocus();
}

void QVLayersHud::setDistortAvailable(bool available)
{
    cropButton->setEnabled(available);
    if (!available) { setCropActive(false); emit cropRequested(false); }
    distortButton->setEnabled(available);
    if (!available) { setDistortActive(false); emit distortRequested(false); }
    distortButton->setToolTip(available
            ? tr("Distort (D): drag to push pixels (Ctrl+Z to undo) · Alt+wheel changes brush size · Right-click for size")
            : tr("Distort is available for still images"));
}

void QVLayersHud::setBrushRadius(int radius)
{
    auto *slider = distortButton->findChild<QSlider *>("distortBrushSize");
    if (slider) { QSignalBlocker blocker(slider); slider->setValue(radius); }
}

void QVLayersHud::setDistortActive(bool active)
{
    distortButton->setChecked(active);
    if (active) setCropActive(false);
    panButton->setChecked(!active && !cropButton->isChecked());
}

void QVLayersHud::setCropActive(bool active)
{
    cropButton->setChecked(active);
    if (active) distortButton->setChecked(false);
    panButton->setChecked(!active && !distortButton->isChecked());
    panel->setVisible(isVisible() && !active);
}

void QVLayersHud::setSource(const QString &source, bool available)
{
    sourceLabel->setText(available ? source : tr("Open media to start editing"));
    sourceLabel->setToolTip(sourceLabel->text());
    list->setEnabled(available);
    properties->setEnabled(available);
    for (auto *tool : panel->findChildren<QToolButton *>())
        if (tool->objectName() != "layersClose") tool->setEnabled(available);
    for (auto *tool : toolbar->findChildren<QToolButton *>()) tool->setEnabled(available);
    if (available) loadSelection();
}

quint64 QVLayersHud::selectedId() const
{
    return list->currentItem() ? list->currentItem()->data(Qt::UserRole).toULongLong() : 0;
}

void QVLayersHud::select(quint64 id)
{
    for (int i = 0; i < list->count(); ++i)
        if (list->item(i)->data(Qt::UserRole).toULongLong() == id) { list->setCurrentRow(i); break; }
}

void QVLayersHud::refresh()
{
    const quint64 id = selectedId();
    const int row = list->currentRow();
    loading = true;
    // Keep item objects and active editors alive during property changes.
    bool rebuild = list->count() != model->stack().layers.size();
    if (!rebuild)
        for (int i = 0; i < list->count(); ++i)
            rebuild |= list->item(i)->data(Qt::UserRole).toULongLong() != model->stack().layers[i].id;
    if (rebuild) list->clear();
    for (int i = 0; i < model->stack().layers.size(); ++i) {
        const auto &layer = model->stack().layers[i];
        auto *item = rebuild ? new QListWidgetItem(list) : list->item(i);
        item->setData(Qt::UserRole, QVariant::fromValue(layer.id));
        item->setData(Qt::UserRole + 1, layer.kind != QVLayers::Kind::Source);
        item->setText(layer.name);
        item->setIcon(icon(layer.kind == QVLayers::Kind::Source ? "source"
                : layer.kind == QVLayers::Kind::Distort ? "distort" : "filter"));
        item->setCheckState(layer.visible ? Qt::Checked : Qt::Unchecked);
        item->setFlags(item->flags() | Qt::ItemIsEditable | Qt::ItemIsUserCheckable | Qt::ItemIsDragEnabled);
        item->setToolTip(layer.kind == QVLayers::Kind::Source ? tr("Live source media")
                : layer.kind == QVLayers::Kind::Distort ? tr("Pushes pixels in the layers below · Right-click for stroke undo")
                : tr("Click the filter icon to edit · Adjusts the layers below"));
    }
    if (rebuild) { select(id); if (!list->currentItem()) list->setCurrentRow(qBound(0, row, list->count() - 1)); }
    loading = false;
    loadSelection();
}

void QVLayersHud::loadSelection()
{
    emit layerSelected(selectedId());
    if (loading) return;
    const int index = model->indexOf(selectedId());
    if (index < 0) return;
    loading = true;
    const auto &layer = model->stack().layers[index];
    blend->setCurrentIndex(int(layer.blend));
    strength->setValue(layer.strength);
    strengthValue->setText(QString::number(layer.strength) + "%");
    int sources = 0;
    for (const auto &entry : model->stack().layers) sources += entry.kind == QVLayers::Kind::Source;
    removeButton->setEnabled(list->isEnabled() && (layer.kind != QVLayers::Kind::Source || sources > 1));
    removeButton->setToolTip(layer.kind == QVLayers::Kind::Source && sources == 1
                            ? tr("Keep one source layer; use visibility to hide it") : tr("Remove layer"));
    upButton->setEnabled(list->isEnabled() && index > 0);
    downButton->setEnabled(list->isEnabled() && index < model->stack().layers.size() - 1);
    loading = false;
}

void QVLayersHud::updateSelection()
{
    if (loading) return;
    const int index = model->indexOf(selectedId());
    if (index < 0) return;
    auto layer = model->stack().layers[index];
    layer.blend = QVLayers::Blend(blend->currentIndex());
    layer.strength = strength->value();
    model->update(layer);
}

void QVLayersHud::addFilter() { select(model->addFilter(qMax(0, model->indexOf(selectedId())))); }
void QVLayersHud::moveSelection(int offset)
{
    const auto id = selectedId();
    model->move(id, model->indexOf(id) + offset);
    select(id);
}

bool QVLayersHud::eventFilter(QObject *watched, QEvent *event)
{
    if (watched == strengthValue && event->type() == QEvent::Wheel) {
        if (strength->isEnabled()) QApplication::sendEvent(strength, event);
        event->accept();
        return true;
    }
    return QObject::eventFilter(watched, event);
}

void QVLayersHud::showLayerMenu(const QPoint &position)
{
    auto *item = list->itemAt(position);
    if (!item || !list->isEnabled()) return;
    list->setCurrentItem(item);
    const quint64 id = selectedId();
    const int index = model->indexOf(id);
    if (index < 0) return;
    const auto layer = model->stack().layers[index];
    auto *menu = new QMenu(panel);
    menu->setObjectName("layerContextMenu");
    connect(menu, &QMenu::aboutToHide, menu, &QObject::deleteLater);
    auto addAction = [menu, this](const QString &text, const QString &name, const std::function<void()> &run) {
        auto *action = menu->addAction(text);
        action->setObjectName(name);
        connect(action, &QAction::triggered, this, run);
        return action;
    };
    if (layer.kind == QVLayers::Kind::Filter) {
        addAction(tr("Edit filter..."), "layerEditFilter", [this, id] {
            if (model->indexOf(id) >= 0) emit filtersRequested(id);
        });
        menu->addSeparator();
    }
    if (layer.kind == QVLayers::Kind::Distort) {
        addAction(tr("Edit distortion"), "layerEditDistort", [this] {
            if (!distortButton->isEnabled()) return;
            setDistortActive(true);
            emit distortRequested(true);
        });
        addAction(tr("Undo last stroke"), "layerUndoStroke", [this, id] { model->undoDistort(id); })
                ->setEnabled(!layer.strokes.isEmpty());
        addAction(tr("Redo stroke"), "layerRedoStroke", [this, id] { model->redoDistort(id); })
                ->setEnabled(!layer.redoStrokes.isEmpty());
        addAction(tr("Reset distortion"), "layerResetDistort", [this, id] {
            const int row = model->indexOf(id);
            if (row < 0) return;
            auto entry = model->stack().layers[row];
            entry.strokes.clear();
            model->update(entry);
        })->setEnabled(!layer.strokes.isEmpty());
        menu->addSeparator();
    }
    addAction(tr("Rename"), "layerRename", [this, id] {
        if (model->indexOf(id) < 0) return;
        select(id);
        list->editItem(list->currentItem());
    });
    addAction(tr("Duplicate"), "layerDuplicate", [this, id] { select(model->duplicate(id)); });
    addAction(layer.visible ? tr("Hide") : tr("Show"), "layerToggleVisibility", [this, id] {
        const int index = model->indexOf(id);
        if (index < 0) return;
        auto layer = model->stack().layers[index];
        layer.visible = !layer.visible;
        model->update(layer);
    });
    menu->addSeparator();
    addAction(tr("Move up"), "layerMoveUp", [this, id] { model->move(id, model->indexOf(id) - 1); })
            ->setEnabled(index > 0);
    addAction(tr("Move down"), "layerMoveDown", [this, id] { model->move(id, model->indexOf(id) + 1); })
            ->setEnabled(index < model->stack().layers.size() - 1);
    menu->addSeparator();
    addAction(tr("Remove"), "layerRemove", [this, id] { model->remove(id); })
            ->setEnabled(removeButton->isEnabled());
    menu->popup(list->viewport()->mapToGlobal(position));
}
