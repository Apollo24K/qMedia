#include "qvfiltersdialog.h"

#include <QDialogButtonBox>
#include <QFormLayout>
#include <QHBoxLayout>
#include <QPushButton>
#include <QSlider>
#include <QSpinBox>
#include <QVBoxLayout>
#include <QWheelEvent>

namespace {
class QVPercentSlider : public QSlider
{
public:
    explicit QVPercentSlider(QWidget *parent = nullptr) : QSlider(Qt::Horizontal, parent) { }

protected:
    void wheelEvent(QWheelEvent *event) override
    {
        int delta = event->angleDelta().y();
        if (delta == 0)
            delta = event->angleDelta().x();
        wheelRemainder += delta;
        const int steps = wheelRemainder / 120;
        wheelRemainder %= 120;
        if (steps != 0)
            setValue(value() + (invertedControls() ? -steps : steps));
        event->accept();
    }

private:
    int wheelRemainder = 0;
};
}

QVFiltersDialog::QVFiltersDialog(const QVFilters::Settings &settings, QWidget *parent)
    : QDialog(parent)
{
    setWindowTitle(tr("Filters"));
    setWindowModality(Qt::NonModal);
    setMinimumWidth(420);

    auto *layout = new QVBoxLayout(this);
    form = new QFormLayout;
    addFilterControl(tr("Brightness"), QStringLiteral("brightness"), settings.brightness,
                     &brightnessSlider, &brightnessSpinBox);
    addFilterControl(tr("Contrast"), QStringLiteral("contrast"), settings.contrast,
                     &contrastSlider, &contrastSpinBox);
    addFilterControl(tr("Saturation"), QStringLiteral("saturation"), settings.saturation,
                     &saturationSlider, &saturationSpinBox);
    layout->addLayout(form);

    auto *buttons = new QDialogButtonBox(this);
    auto *reset = buttons->addButton(tr("Reset"), QDialogButtonBox::ResetRole);
    buttons->addButton(QDialogButtonBox::Close);
    layout->addWidget(buttons);
    connect(reset, &QPushButton::clicked, this, &QVFiltersDialog::resetFilters);
    connect(buttons, &QDialogButtonBox::rejected, this, &QDialog::close);
}

void QVFiltersDialog::addFilterControl(const QString &label, const QString &objectName,
                                       int value, QSlider **slider, QSpinBox **spinBox)
{
    *slider = new QVPercentSlider(this);
    (*slider)->setObjectName(objectName + QStringLiteral("Slider"));
    (*slider)->setRange(-100, 100);
    (*slider)->setSingleStep(1);
    (*slider)->setValue(value);
    (*spinBox) = new QSpinBox(this);
    (*spinBox)->setObjectName(objectName + QStringLiteral("SpinBox"));
    (*spinBox)->setRange(-100, 100);
    (*spinBox)->setSuffix(QStringLiteral(" %"));
    (*spinBox)->setValue(value);

    auto *row = new QHBoxLayout;
    row->addWidget(*slider, 1);
    row->addWidget(*spinBox);
    form->addRow(label, row);
    connect(*slider, &QSlider::valueChanged, *spinBox, &QSpinBox::setValue);
    connect(*spinBox, QOverload<int>::of(&QSpinBox::valueChanged), *slider,
            &QSlider::setValue);
    connect(*slider, &QSlider::valueChanged, this, [this] { emitCurrentFilters(); });
}

void QVFiltersDialog::emitCurrentFilters()
{
    QVFilters::Settings settings;
    settings.brightness = brightnessSlider->value();
    settings.contrast = contrastSlider->value();
    settings.saturation = saturationSlider->value();
    emit filtersChanged(settings);
}

void QVFiltersDialog::resetFilters()
{
    brightnessSlider->setValue(0);
    contrastSlider->setValue(0);
    saturationSlider->setValue(0);
}
