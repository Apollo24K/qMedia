#ifndef QVFILTERSDIALOG_H
#define QVFILTERSDIALOG_H

#include "qvlayers.h"

#include <QDialog>

class QComboBox;
class QFormLayout;
class QGroupBox;
class QPushButton;
class QSlider;
class QSpinBox;

class QVFiltersDialog : public QDialog
{
    Q_OBJECT
public:
    explicit QVFiltersDialog(QVLayerModel *model, QWidget *parent = nullptr);

    void selectLayer(quint64 id);

private:
    void addFilterControl(const QString &label, const QString &objectName, int minimum,
                          int maximum, QSlider **slider, QSpinBox **spinBox,
                          const QString &gradient);
    void addLayer();
    void removeLayer();
    void loadLayer(int index);
    void updateCurrentLayer();
    void resetCurrentLayer();
    void updateLayerNames();

    QVLayerModel *model;
    quint64 selectedId() const;
    QFormLayout *basicForm;
    QComboBox *layerSelector;
    QPushButton *removeLayerButton;
    QSlider *brightnessSlider;
    QSlider *contrastSlider;
    QSlider *saturationSlider;
    QSlider *hueSlider;
    QSlider *transparencySlider;
    QSpinBox *brightnessSpinBox;
    QSpinBox *contrastSpinBox;
    QSpinBox *saturationSpinBox;
    QSpinBox *hueSpinBox;
    QSpinBox *transparencySpinBox;
    QGroupBox *gradientGroup;
    QSpinBox *centerX;
    QSpinBox *centerY;
    QSpinBox *direction;
    QSpinBox *softness;
    bool loadingLayer = false;
};

#endif // QVFILTERSDIALOG_H
