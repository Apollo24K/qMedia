#ifndef QVFILTERSDIALOG_H
#define QVFILTERSDIALOG_H

#include "qvfilters.h"

#include <QDialog>

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
    void addFilterControl(const QString &label, const QString &objectName, int value,
                          QSlider **slider, QSpinBox **spinBox);
    void emitCurrentFilters();
    void resetFilters();

    class QFormLayout *form;
    QSlider *brightnessSlider;
    QSlider *contrastSlider;
    QSlider *saturationSlider;
    QSpinBox *brightnessSpinBox;
    QSpinBox *contrastSpinBox;
    QSpinBox *saturationSpinBox;
};

#endif // QVFILTERSDIALOG_H
