#ifndef QVLAYERSHUD_H
#define QVLAYERSHUD_H

#include "qvlayers.h"
#include <QFrame>

class QLabel;
class QListWidget;
class QComboBox;
class QSlider;
class QToolButton;

// Floating viewport siblings: QGraphicsView scrolls viewport children with the
// image. Siblings stay anchored without participating in the canvas layout.
class QVOverlayPanel : public QFrame
{
    Q_OBJECT
public:
    QVOverlayPanel(QWidget *viewport, bool resizable, bool right);
    void setDragHandle(QWidget *handle);
    void restorePlacement(const QString &key, const QSize &defaultSize);
    void savePlacement(const QString &key) const;
    void fitToViewport();
    void resetPlacement();
protected:
    bool eventFilter(QObject *watched, QEvent *event) override;
    bool event(QEvent *event) override;
    void mousePressEvent(QMouseEvent *event) override;
    void mouseMoveEvent(QMouseEvent *event) override;
    void mouseReleaseEvent(QMouseEvent *event) override;
private:
    Qt::Edges edgesAt(const QPoint &point) const;
    void beginDrag(QMouseEvent *event, bool handle);
    void drag(QMouseEvent *event);
    void finishDrag();
    bool resizable;
    bool defaultRight;
    QWidget *viewport;
    bool dragging = false;
    Qt::Edges resizing;
    QPoint dragOrigin;
    QRect dragGeometry;
    QSize preferredSize;
    QSize defaultSize;
    QPoint freePosition;
    int horizontalAnchor = 0;
    int verticalAnchor = 1;
};

class QVLayersHud : public QObject
{
    Q_OBJECT
public:
    QVLayersHud(QVLayerModel *model, QWidget *viewport);
    ~QVLayersHud() override;
    bool isVisible() const;
    void setVisible(bool visible);
    void toggle();
    quint64 selectedLayerId() const { return selectedId(); }
    void setSource(const QString &name, bool available);
    void setDistortAvailable(bool available);
    void setDistortActive(bool active);
    void setCropActive(bool active);
    void setBrushRadius(int radius);
    void selectLayer(quint64 id) { select(id); }
signals:
    void layerSelected(quint64 id);
    void distortRequested(bool active);
    void cropRequested(bool active);
    void cropApplyRequested();
    void cropResetRequested();
    void distortSizeChanged(int radius);
    void filtersRequested(quint64 id);
    void exportRequested();
    void resetViewRequested();
protected:
    bool eventFilter(QObject *watched, QEvent *event) override;
private:
    void showLayerMenu(const QPoint &position);
    void refresh();
    void select(quint64 id);
    void loadSelection();
    void updateSelection();
    void addFilter();
    void moveSelection(int offset);
    quint64 selectedId() const;
    QVLayerModel *model;
    QVOverlayPanel *panel;
    QVOverlayPanel *toolbar;
    QListWidget *list;
    QLabel *sourceLabel;
    QComboBox *blend;
    QSlider *strength;
    QLabel *strengthValue;
    QToolButton *distortButton;
    QToolButton *cropButton;
    QToolButton *panButton;
    QToolButton *removeButton;
    QToolButton *upButton;
    QToolButton *downButton;
    QWidget *properties;
    bool loading = false;
};

#endif
