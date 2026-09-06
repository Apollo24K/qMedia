#include "qvfiltersdialog.h"

#include <QComboBox>
#include <QDialogButtonBox>
#include <QFormLayout>
#include <QGroupBox>
#include <QHBoxLayout>
#include <QLabel>
#include <QPushButton>
#include <QSignalBlocker>
#include <QSlider>
#include <QSpinBox>
#include <QShortcut>
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

QString sliderStyle(const QString &gradient)
{
    return QStringLiteral(
            "QSlider::groove:horizontal { height: 10px; border: 1px solid palette(mid); "
            "border-radius: 5px; background: %1; }"
            "QSlider::handle:horizontal { width: 14px; margin: -4px 0; border-radius: 7px; "
            "border: 2px solid palette(highlight); background: palette(base); }")
            .arg(gradient);
}
}

QVFiltersDialog::QVFiltersDialog(QVLayerModel *layerModel, QWidget *parent)
    : QDialog(parent), model(layerModel)
{
    setWindowTitle(tr("Filters"));
    setWindowModality(Qt::NonModal);
    setMinimumWidth(500);
    auto *toggleShortcut = new QShortcut(Qt::Key_U, this);
    toggleShortcut->setObjectName("filtersToggleShortcut");
    connect(toggleShortcut, &QShortcut::activated, this, &QDialog::close);

    auto *layout = new QVBoxLayout(this);
    auto *layerRow = new QHBoxLayout;
    layerRow->addWidget(new QLabel(tr("Layer"), this));
    layerSelector = new QComboBox(this);
    layerSelector->setObjectName("filterLayerSelector");
    layerRow->addWidget(layerSelector, 1);
    auto *addLayerButton = new QPushButton(tr("Add"), this);
    addLayerButton->setObjectName("filterAddLayer");
    removeLayerButton = new QPushButton(tr("Remove"), this);
    removeLayerButton->setObjectName("filterRemoveLayer");
    layerRow->addWidget(addLayerButton);
    layerRow->addWidget(removeLayerButton);
    layout->addLayout(layerRow);

    basicForm = new QFormLayout;
    addFilterControl(tr("Brightness"), QStringLiteral("brightness"), -100, 100,
                     &brightnessSlider, &brightnessSpinBox,
                     "qlineargradient(x1:0, y1:0, x2:1, y2:0, stop:0 black, stop:1 white)");
    addFilterControl(tr("Contrast"), QStringLiteral("contrast"), -100, 100,
                     &contrastSlider, &contrastSpinBox,
                     "qlineargradient(x1:0, y1:0, x2:1, y2:0, stop:0 #808080, stop:0.5 #555555, stop:1 white)");
    addFilterControl(tr("Saturation"), QStringLiteral("saturation"), -100, 100,
                     &saturationSlider, &saturationSpinBox,
                     "qlineargradient(x1:0, y1:0, x2:1, y2:0, stop:0 #808080, stop:1 #ff1744)");
    addFilterControl(tr("Hue"), QStringLiteral("hue"), -180, 180,
                     &hueSlider, &hueSpinBox,
                     "qlineargradient(x1:0, y1:0, x2:1, y2:0, stop:0 #ff0000, stop:0.17 #ffff00, "
                     "stop:0.33 #00ff00, stop:0.5 #00ffff, stop:0.67 #0000ff, "
                     "stop:0.83 #ff00ff, stop:1 #ff0000)");
    addFilterControl(tr("Transparency"), QStringLiteral("transparency"), 0, 100,
                     &transparencySlider, &transparencySpinBox,
                     "qlineargradient(x1:0, y1:0, x2:1, y2:0, stop:0 palette(text), stop:1 transparent)");
    layout->addLayout(basicForm);

    gradientGroup = new QGroupBox(tr("Gradient mask (advanced)"), this);
    gradientGroup->setObjectName("filterGradientGroup");
    gradientGroup->setCheckable(true);
    auto *advanced = new QFormLayout(gradientGroup);
    centerX = new QSpinBox(this);
    centerY = new QSpinBox(this);
    direction = new QSpinBox(this);
    softness = new QSpinBox(this);
    centerX->setObjectName("filterCenterX");
    centerY->setObjectName("filterCenterY");
    direction->setObjectName("filterDirection");
    softness->setObjectName("filterSoftness");
    for (auto *position : { centerX, centerY }) {
        position->setRange(0, 100);
        position->setSuffix(QStringLiteral(" %"));
    }
    direction->setRange(0, 359);
    direction->setSuffix(QStringLiteral("°"));
    softness->setRange(1, 100);
    softness->setSuffix(QStringLiteral(" %"));
    advanced->addRow(tr("Center X"), centerX);
    advanced->addRow(tr("Center Y"), centerY);
    advanced->addRow(tr("Direction (0° right, 90° down)"), direction);
    advanced->addRow(tr("Transition softness"), softness);
    layout->addWidget(gradientGroup);

    auto *buttons = new QDialogButtonBox(this);
    auto *reset = buttons->addButton(tr("Reset layer"), QDialogButtonBox::ResetRole);
    reset->setObjectName("filterResetLayer");
    buttons->addButton(QDialogButtonBox::Close);
    layout->addWidget(buttons);

    connect(addLayerButton, &QPushButton::clicked, this, &QVFiltersDialog::addLayer);
    connect(removeLayerButton, &QPushButton::clicked, this, &QVFiltersDialog::removeLayer);
    connect(layerSelector, QOverload<int>::of(&QComboBox::currentIndexChanged),
            this, &QVFiltersDialog::loadLayer);
    connect(reset, &QPushButton::clicked, this, &QVFiltersDialog::resetCurrentLayer);
    connect(buttons, &QDialogButtonBox::rejected, this, &QDialog::close);
    for (auto *control : { centerX, centerY, direction, softness }) {
        connect(control, QOverload<int>::of(&QSpinBox::valueChanged),
                this, &QVFiltersDialog::updateCurrentLayer);
    }
    connect(gradientGroup, &QGroupBox::toggled, this, &QVFiltersDialog::updateCurrentLayer);

    connect(model, &QVLayerModel::changed, this, &QVFiltersDialog::updateLayerNames);
    updateLayerNames();
}

void QVFiltersDialog::addFilterControl(const QString &label, const QString &objectName,
                                       int minimum, int maximum, QSlider **slider,
                                       QSpinBox **spinBox, const QString &gradient)
{
    *slider = new QVPercentSlider(this);
    (*slider)->setObjectName(objectName + QStringLiteral("Slider"));
    (*slider)->setRange(minimum, maximum);
    (*slider)->setSingleStep(1);
    (*slider)->setStyleSheet(sliderStyle(gradient));
    (*spinBox) = new QSpinBox(this);
    (*spinBox)->setObjectName(objectName + QStringLiteral("SpinBox"));
    (*spinBox)->setRange(minimum, maximum);
    (*spinBox)->setSuffix(objectName == QStringLiteral("hue") ? QStringLiteral("°")
                                                               : QStringLiteral(" %"));
    auto *row = new QHBoxLayout;
    row->addWidget(*slider, 1);
    row->addWidget(*spinBox);
    basicForm->addRow(label, row);
    connect(*slider, &QSlider::valueChanged, *spinBox, &QSpinBox::setValue);
    connect(*spinBox, QOverload<int>::of(&QSpinBox::valueChanged), *slider,
            &QSlider::setValue);
    connect(*slider, &QSlider::valueChanged, this, &QVFiltersDialog::updateCurrentLayer);
}

quint64 QVFiltersDialog::selectedId() const
{
    return layerSelector->currentData().toULongLong();
}

void QVFiltersDialog::selectLayer(quint64 id)
{
    const int index = layerSelector->findData(QVariant::fromValue(id));
    if (index >= 0) layerSelector->setCurrentIndex(index);
}

void QVFiltersDialog::updateLayerNames()
{
    const quint64 selected = selectedId();
    const int previousIndex = qMax(0, layerSelector->currentIndex());
    {
        const QSignalBlocker blocker(layerSelector);
        layerSelector->clear();
        for (const auto &layer : model->stack().layers) {
            if (layer.kind == QVLayers::Kind::Filter)
                layerSelector->addItem(layer.name, QVariant::fromValue(layer.id));
        }
        const int index = layerSelector->findData(QVariant::fromValue(selected));
        layerSelector->setCurrentIndex(index >= 0 ? index : qMin(previousIndex, layerSelector->count() - 1));
    }
    const bool available = layerSelector->count() > 0;
    removeLayerButton->setEnabled(available);
    for (auto *slider : { brightnessSlider, contrastSlider, saturationSlider, hueSlider, transparencySlider })
        slider->setEnabled(available);
    for (auto *spin : { brightnessSpinBox, contrastSpinBox, saturationSpinBox, hueSpinBox, transparencySpinBox })
        spin->setEnabled(available);
    gradientGroup->setEnabled(available);
    findChild<QPushButton *>("filterResetLayer")->setEnabled(available);
    loadLayer(layerSelector->currentIndex());
}

void QVFiltersDialog::addLayer()
{
    selectLayer(model->addFilter());
}

void QVFiltersDialog::removeLayer()
{
    model->remove(selectedId());
}

void QVFiltersDialog::loadLayer(int index)
{
    const int modelIndex = model->indexOf(layerSelector->itemData(index).toULongLong());
    if (modelIndex < 0) return;
    loadingLayer = true;
    const auto &layer = model->stack().layers[modelIndex].filter;
    brightnessSlider->setValue(layer.brightness);
    contrastSlider->setValue(layer.contrast);
    saturationSlider->setValue(layer.saturation);
    hueSlider->setValue(layer.hue);
    transparencySlider->setValue(layer.transparency);
    gradientGroup->setChecked(layer.gradient);
    centerX->setValue(layer.centerX);
    centerY->setValue(layer.centerY);
    direction->setValue(layer.direction);
    softness->setValue(layer.softness);
    loadingLayer = false;
}

void QVFiltersDialog::updateCurrentLayer()
{
    const int index = model->indexOf(selectedId());
    if (loadingLayer || index < 0) return;
    auto entry = model->stack().layers[index];
    auto &layer = entry.filter;
    layer.brightness = brightnessSlider->value();
    layer.contrast = contrastSlider->value();
    layer.saturation = saturationSlider->value();
    layer.hue = hueSlider->value();
    layer.transparency = transparencySlider->value();
    layer.gradient = gradientGroup->isChecked();
    layer.centerX = centerX->value();
    layer.centerY = centerY->value();
    layer.direction = direction->value();
    layer.softness = softness->value();
    model->update(entry);
}

void QVFiltersDialog::resetCurrentLayer()
{
    const int index = model->indexOf(selectedId());
    if (index < 0) return;
    auto entry = model->stack().layers[index];
    entry.filter = QVFilters::Layer();
    model->update(entry);
}
