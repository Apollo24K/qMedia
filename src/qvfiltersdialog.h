#ifndef QVFILTERSDIALOG_H
#define QVFILTERSDIALOG_H

#include "qvfilters.h"

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
    explicit QVFiltersDialog(const QVFilters::Settings &settings, QWidget *parent = nullptr);

signals:
    void filtersChanged(const QVFilters::Settings &settings);

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

    QVFilters::Settings settings;
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
