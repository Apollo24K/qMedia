#include "qvoptionsdialog.h"
#include "ui_qvoptionsdialog.h"
#include "qvapplication.h"
#include <QColorDialog>
#include <QPalette>
#include <QScreen>
#include <QMessageBox>
#include <QSettings>
#include <QKeySequence>
#include <QShortcut>

#include <QDebug>

QVOptionsDialog::QVOptionsDialog(QWidget *parent) : QDialog(parent), ui(new Ui::QVOptionsDialog)
{
    ui->setupUi(this);
    auto *toggleShortcut = new QShortcut(Qt::Key_S, this);
    toggleShortcut->setObjectName("settingsToggleShortcut");
    connect(toggleShortcut, &QShortcut::activated, this, &QDialog::close);

    // Set platform-specific modifier text for Ctrl drag checkbox
    QString ctrlString = QKeySequence(Qt::ControlModifier).toString().remove('+');
    ui->ctrlDragCheckbox->setText(ui->ctrlDragCheckbox->text().arg(ctrlString));
    ui->ctrlDragCheckbox->setToolTip(ui->ctrlDragCheckbox->toolTip().arg(ctrlString));

    languageRestartMessageShown = false;
    mediaBackendRestartMessageShown = false;

    ui->mediaBackendComboBox->addItem(tr("FFmpeg (broad format support)"),
                                      QStringLiteral("ffmpeg"));
    ui->mediaBackendComboBox->addItem(tr("Windows Media Foundation (audio compatibility)"),
                                      QStringLiteral("windows"));

    setAttribute(Qt::WA_DeleteOnClose);
    setWindowFlags(windowFlags() & (~Qt::WindowContextHelpButtonHint | Qt::CustomizeWindowHint));

    resize(640, 530);

    qvApp->ensureFontLoaded(":/fonts/MaterialIconsOutlined-Regular.otf");

    connect(ui->categoryList, &QListWidget::currentRowChanged, this, [this](int currentRow) { ui->stackedWidget->setCurrentIndex(currentRow); });
    connect(ui->buttonBox, &QDialogButtonBox::clicked, this, &QVOptionsDialog::buttonBoxClicked);
    connect(ui->shortcutsTable, &QTableWidget::cellDoubleClicked, this,
            &QVOptionsDialog::shortcutCellDoubleClicked);
    connect(ui->shortcutsSearch, &QLineEdit::textChanged, this,
            &QVOptionsDialog::filterShortcuts);
    connect(ui->shortcutsSearchMode, QOverload<int>::of(&QComboBox::currentIndexChanged),
            this, [this](int mode) {
                ui->shortcutsSearch->setPlaceholderText(mode == 1 ? tr("Search action names...")
                        : mode == 2 ? tr("Key or combination, e.g. C or Ctrl+C")
                                    : tr("Search actions or shortcuts..."));
                filterShortcuts();
            });
    connect(ui->bgColorCheckbox, &QCheckBox::stateChanged, this,
            &QVOptionsDialog::bgColorCheckboxStateChanged);
    connect(ui->scalingCheckbox, &QCheckBox::stateChanged, this,
            &QVOptionsDialog::scalingCheckboxStateChanged);

    QSettings settings;

    populateCategories(settings.value("optionstab", 1).toInt());
    populateLanguages();

    // On macOS, the dialog should not be dependent on any window
#ifndef Q_OS_MACOS
    setWindowModality(Qt::WindowModal);
#else
    // Load window geometry
    restoreGeometry(settings.value("optionsgeometry").toByteArray());
#endif

#if QT_VERSION < QT_VERSION_CHECK(6, 0, 0)
    // Hide scroll zoom auto-detect option if unsupported
    // TODO: This causes an issue with saving/loading settings
    // between different Qt versions.
    ui->scrollZoomsComboBox->removeItem(1);
#endif

    if (QOperatingSystemVersion::current()
        < QOperatingSystemVersion(QOperatingSystemVersion::MacOS, 13)) {
        setWindowTitle("Preferences");
    }

#ifdef QV_DISABLE_ONLINE_VERSION_CHECK
    ui->updateCheckbox->hide();
#endif // QV_DISABLE_ONLINE_VERSION_CHECK

// Platform specific settings
#ifdef Q_OS_MACOS
    ui->menubarCheckbox->hide();
#else
    ui->forceDarkModeCheckbox->hide();
    ui->hideTitlebarCheckbox->hide();
    ui->quitOnLastWindowCheckbox->hide();
#endif

#if !defined Q_OS_WIN || QT_VERSION < QT_VERSION_CHECK(6, 0, 0)
    ui->mediaBackendLabel->hide();
    ui->mediaBackendComboBox->hide();
    ui->mediaBackendRestartLabel->hide();
#endif

// Hide language selection below 5.12, as 5.12 does not support embedding the translations :(
#if (QT_VERSION < QT_VERSION_CHECK(5, 12, 0))
    ui->langComboBox->hide();
    ui->langComboLabel->hide();
#endif

// Hide color space conversion below 5.14, which is when color space support was introduced
#if (QT_VERSION < QT_VERSION_CHECK(5, 14, 0))
    ui->colorSpaceConversionComboBox->hide();
    ui->colorSpaceConversionLabel->hide();
#endif

    syncSettings(false, true);
    connect(ui->windowResizeComboBox, QOverload<int>::of(&QComboBox::currentIndexChanged), this,
            &QVOptionsDialog::windowResizeComboBoxCurrentIndexChanged);
    connect(ui->langComboBox, QOverload<int>::of(&QComboBox::currentIndexChanged), this,
            &QVOptionsDialog::languageComboBoxCurrentIndexChanged);
    connect(ui->mediaBackendComboBox,
            QOverload<int>::of(&QComboBox::currentIndexChanged), this,
            &QVOptionsDialog::mediaBackendComboBoxCurrentIndexChanged);
    connect(ui->scrollZoomsComboBox, QOverload<int>::of(&QComboBox::currentIndexChanged), this,
            &QVOptionsDialog::scrollZoomsComboBoxCurrentIndexChanged);
    syncShortcuts();
    updateButtonBox();
}

QVOptionsDialog::~QVOptionsDialog()
{
    delete ui;
}

void QVOptionsDialog::changeEvent(QEvent *event)
{
    if (event->type() == QEvent::PaletteChange)
    {
        populateCategories(ui->categoryList->currentRow());
    }
    QDialog::changeEvent(event);
}

void QVOptionsDialog::done(int r)
{
    // Save window geometry
    QSettings settings;
    settings.setValue("optionsgeometry", saveGeometry());
    settings.setValue("optionstab", ui->categoryList->currentRow());

    QDialog::done(r);
}

void QVOptionsDialog::modifySetting(QString key, QVariant value)
{
    transientSettings.insert(key, value);
    updateButtonBox();
}

void QVOptionsDialog::saveSettings()
{
    QSettings settings;
    settings.beginGroup("options");

    const auto keys = transientSettings.keys();
    for (const auto &key : keys) {
        const auto &value = transientSettings[key];
        settings.setValue(key, value);
    }

    settings.endGroup();
    settings.beginGroup("shortcuts");

    const auto &shortcutsList = qvApp->getShortcutManager().getShortcutsList();
    for (int i = 0; i < transientShortcuts.length(); i++) {
        settings.setValue(shortcutsList.value(i).name, transientShortcuts.value(i));
    }

    qvApp->getShortcutManager().updateShortcuts();
    qvApp->getSettingsManager().loadSettings();
}

void QVOptionsDialog::syncSettings(bool defaults, bool makeConnections)
{
    auto &settingsManager = qvApp->getSettingsManager();
    settingsManager.loadSettings();

    // bgcolorenabled
    syncCheckbox(ui->bgColorCheckbox, "bgcolorenabled", defaults, makeConnections);
    if (ui->bgColorCheckbox->isChecked())
        ui->bgColorButton->setEnabled(true);
    else
        ui->bgColorButton->setEnabled(false);
    // bgcolor
    ui->bgColorButton->setText(settingsManager.getString("bgcolor", defaults));
    transientSettings.insert("bgcolor", ui->bgColorButton->text());
    updateBgColorButton(ui->bgColorButton);
    ui->alternateBgColorButton->setText(settingsManager.getString("alternatebgcolor", defaults));
    transientSettings.insert("alternatebgcolor", ui->alternateBgColorButton->text());
    updateBgColorButton(ui->alternateBgColorButton);
    if (makeConnections) {
        connect(ui->bgColorButton, &QPushButton::clicked, this,
                [this] { bgColorButtonClicked(ui->bgColorButton, "bgcolor"); });
        connect(ui->alternateBgColorButton, &QPushButton::clicked, this,
                [this] { bgColorButtonClicked(ui->alternateBgColorButton, "alternatebgcolor"); });
    }
    // titlebarmode
    syncRadioButtons({ ui->titlebarRadioButton0, ui->titlebarRadioButton1, ui->titlebarRadioButton2,
                       ui->titlebarRadioButton3 },
                     "titlebarmode", defaults, makeConnections);
    // windowresizemode
    syncComboBox(ui->windowResizeComboBox, "windowresizemode", defaults, makeConnections);
    windowResizeComboBoxCurrentIndexChanged(ui->windowResizeComboBox->currentIndex());
    // minwindowresizedpercentage
    syncSpinBox(ui->minWindowResizeSpinBox, "minwindowresizedpercentage", defaults,
                makeConnections);
    // maxwindowresizedperecentage
    syncSpinBox(ui->maxWindowResizeSpinBox, "maxwindowresizedpercentage", defaults,
                makeConnections);
    // hidetitlebar
    syncCheckbox(ui->hideTitlebarCheckbox, "hidetitlebar", defaults, makeConnections);
    // forcedarkmode
    syncCheckbox(ui->forceDarkModeCheckbox, "forcedarkmode", defaults, makeConnections);
    // quitonlastwindow
    syncCheckbox(ui->quitOnLastWindowCheckbox, "quitonlastwindow", defaults, makeConnections);
    // ctrldragwindow
    syncCheckbox(ui->ctrlDragCheckbox, "ctrldragwindow", defaults, makeConnections);
    // menubarenabled
    syncCheckbox(ui->menubarCheckbox, "menubarenabled", defaults, makeConnections);
    // fullscreendetails
    syncCheckbox(ui->detailsInFullscreen, "fullscreendetails", defaults, makeConnections);
    // filteringenabled
    syncCheckbox(ui->filteringCheckbox, "filteringenabled", defaults, makeConnections);
    // scalingenabled
    syncCheckbox(ui->scalingCheckbox, "scalingenabled", defaults, makeConnections);
    ui->scalingTwoCheckbox->setEnabled(ui->scalingCheckbox->isChecked());
    // scalingtwoenabled
    syncCheckbox(ui->scalingTwoCheckbox, "scalingtwoenabled", defaults, makeConnections);
    // scalefactor
    syncSpinBox(ui->scaleFactorSpinBox, "scalefactor", defaults, makeConnections);
    // scrollzoom
    syncComboBox(ui->scrollZoomsComboBox, "scrollzoom", defaults, makeConnections);
    // fractionalzoom
    syncCheckbox(ui->fractionalZoomCheckbox, "fractionalzoom", defaults, makeConnections);
    // cursorzoom
    syncCheckbox(ui->cursorZoomCheckbox, "cursorzoom", defaults, makeConnections);
    // cropmode
    syncComboBox(ui->cropModeComboBox, "cropmode", defaults, makeConnections);
    // pastactualsizeenabled
    syncCheckbox(ui->pastActualSizeCheckbox, "pastactualsizeenabled", defaults, makeConnections);
    // colorspaceconversion
    syncComboBox(ui->colorSpaceConversionComboBox, "colorspaceconversion", defaults,
                 makeConnections);
    // language
    syncComboBoxData(ui->langComboBox, "language", defaults, makeConnections);
    // media backend
    syncComboBoxData(ui->mediaBackendComboBox, "mediabackend", defaults, makeConnections);
    // sortmode
    syncComboBox(ui->sortComboBox, "sortmode", defaults, makeConnections);
    // sortdescending
    syncRadioButtons({ ui->descendingRadioButton0, ui->descendingRadioButton1 }, "sortdescending",
                     defaults, makeConnections);
    // preloadingmode
    syncComboBox(ui->preloadingComboBox, "preloadingmode", defaults, makeConnections);
    // loopfolders
    syncCheckbox(ui->loopFoldersCheckbox, "loopfoldersenabled", defaults, makeConnections);
    // slideshowreversed
    syncComboBox(ui->slideshowDirectionComboBox, "slideshowreversed", defaults, makeConnections);
    // slideshowtimer
    syncDoubleSpinBox(ui->slideshowTimerSpinBox, "slideshowtimer", defaults, makeConnections);
    // afterdelete
    syncComboBox(ui->afterDeletionComboBox, "afterdelete", defaults, makeConnections);
    // askdelete
    syncCheckbox(ui->askDeleteCheckbox, "askdelete", defaults, makeConnections);
    // allowmimecontentdetection
    syncCheckbox(ui->mimeContentDetectionCheckbox, "allowmimecontentdetection", defaults,
                 makeConnections);
    // saverecents
    syncCheckbox(ui->saveRecentsCheckbox, "saverecents", defaults, makeConnections);
    // updatenotifications
    syncCheckbox(ui->updateCheckbox, "updatenotifications", defaults, makeConnections);
    // skiphidden
    syncCheckbox(ui->skipHiddenCheckbox, "skiphidden", defaults, makeConnections);
}

void QVOptionsDialog::syncCheckbox(QCheckBox *checkbox, const QString &key, bool defaults,
                                   bool makeConnection)
{
    auto val = qvApp->getSettingsManager().getBool(key, defaults);
    checkbox->setChecked(val);
    transientSettings.insert(key, val);

    if (makeConnection) {
        connect(checkbox, &QCheckBox::stateChanged, this,
                [this, key](int arg1) { modifySetting(key, static_cast<bool>(arg1)); });
    }
}

void QVOptionsDialog::syncRadioButtons(QList<QRadioButton *> buttons, const QString &key,
                                       bool defaults, bool makeConnection)
{
    auto val = qvApp->getSettingsManager().getInt(key, defaults);
    buttons.value(val)->setChecked(true);
    transientSettings.insert(key, val);

    if (makeConnection) {
        for (int i = 0; i < buttons.length(); i++) {
            connect(buttons.value(i), &QRadioButton::clicked, this,
                    [this, key, i] { modifySetting(key, i); });
        }
    }
}

void QVOptionsDialog::syncComboBox(QComboBox *comboBox, const QString &key, bool defaults,
                                   bool makeConnection)
{
    auto val = qvApp->getSettingsManager().getInt(key, defaults);
    comboBox->setCurrentIndex(val);
    transientSettings.insert(key, val);

    if (makeConnection) {
        connect(comboBox, QOverload<int>::of(&QComboBox::currentIndexChanged), this,
                [this, key](int index) { modifySetting(key, index); });
    }
}

void QVOptionsDialog::syncComboBoxData(QComboBox *comboBox, const QString &key, bool defaults,
                                       bool makeConnection)
{
    auto val = qvApp->getSettingsManager().getString(key, defaults);
    comboBox->setCurrentIndex(comboBox->findData(val));
    transientSettings.insert(key, val);

    if (makeConnection) {
        connect(comboBox, QOverload<int>::of(&QComboBox::currentIndexChanged), this,
                [this, key, comboBox](int index) {
                    Q_UNUSED(index)
                    modifySetting(key, comboBox->currentData());
                });
    }
}

void QVOptionsDialog::syncSpinBox(QSpinBox *spinBox, const QString &key, bool defaults,
                                  bool makeConnection)
{
    auto val = qvApp->getSettingsManager().getInt(key, defaults);
    spinBox->setValue(val);
    transientSettings.insert(key, val);

    if (makeConnection) {
        connect(spinBox, QOverload<int>::of(&QSpinBox::valueChanged), this,
                [this, key](int arg1) { modifySetting(key, arg1); });
    }
}

void QVOptionsDialog::syncDoubleSpinBox(QDoubleSpinBox *doubleSpinBox, const QString &key,
                                        bool defaults, bool makeConnection)
{
    auto val = qvApp->getSettingsManager().getDouble(key, defaults);
    doubleSpinBox->setValue(val);
    transientSettings.insert(key, val);

    if (makeConnection) {
        connect(doubleSpinBox, QOverload<double>::of(&QDoubleSpinBox::valueChanged), this,
                [this, key](double arg1) { modifySetting(key, arg1); });
    }
}

void QVOptionsDialog::syncShortcuts(bool defaults)
{
    qvApp->getShortcutManager().updateShortcuts();

    transientShortcuts.clear();
    const auto &shortcutsList = qvApp->getShortcutManager().getShortcutsList();
    ui->shortcutsTable->setRowCount(shortcutsList.length());

    for (int i = 0; i < shortcutsList.length(); i++) {
        const ShortcutManager::SShortcut &shortcut = shortcutsList.value(i);

        // Add shortcut to transient shortcut list
        if (defaults)
            transientShortcuts.append(shortcut.defaultShortcuts);
        else
            transientShortcuts.append(shortcut.shortcuts);

        // Add shortcut to table widget
        auto *nameItem = new QTableWidgetItem();
        nameItem->setText(shortcut.readableName);
        ui->shortcutsTable->setItem(i, 0, nameItem);

        auto *shortcutsItem = new QTableWidgetItem();
        shortcutsItem->setText(
                ShortcutManager::stringListToReadableString(transientShortcuts.value(i)));
        ui->shortcutsTable->setItem(i, 1, shortcutsItem);
    }
    updateShortcutsTable();
}

void QVOptionsDialog::updateShortcutsTable()
{
    for (int i = 0; i < transientShortcuts.length(); i++) {
        const QStringList &shortcuts = transientShortcuts.value(i);
        ui->shortcutsTable->item(i, 1)->setText(
                ShortcutManager::stringListToReadableString(shortcuts));
    }
    filterShortcuts();
    updateButtonBox();
}

void QVOptionsDialog::filterShortcuts()
{
    const QString query = ui->shortcutsSearch->text().trimmed();
    const int mode = ui->shortcutsSearchMode->currentIndex();
    const auto combinedKey = [](const QKeySequence &sequence) {
#if QT_VERSION >= QT_VERSION_CHECK(6, 0, 0)
        return sequence[0].toCombined();
#else
        return sequence[0];
#endif
    };
    QKeySequence searchedKey = QKeySequence::fromString(query, QKeySequence::NativeText);
    if (searchedKey.isEmpty() || combinedKey(searchedKey) == Qt::Key_unknown)
        searchedKey = QKeySequence::fromString(query, QKeySequence::PortableText);
    const int searchedCode = combinedKey(searchedKey);
    const bool validKey = searchedKey.count() == 1 && searchedCode != Qt::Key_unknown;
    const int modifiers = int(Qt::KeyboardModifierMask);
    for (int row = 0; row < ui->shortcutsTable->rowCount(); ++row) {
        bool matches = query.isEmpty();
        if (mode != 2)
            matches |= ui->shortcutsTable->item(row, 0)->text().contains(query, Qt::CaseInsensitive);
        if (mode == 0)
            matches |= ui->shortcutsTable->item(row, 1)->text().contains(query, Qt::CaseInsensitive);
        if (mode == 2 && validKey) {
            const auto bindings = ShortcutManager::stringListToKeySequenceList(transientShortcuts.value(row));
            for (const auto &binding : bindings) {
                const int code = combinedKey(binding);
                // A bare key includes modified bindings; a chord matches exactly.
                matches |= (searchedCode & modifiers) ? code == searchedCode
                        : (code & ~modifiers) == searchedCode;
            }
        }
        ui->shortcutsTable->setRowHidden(row, !matches);
    }
}

void QVOptionsDialog::shortcutCellDoubleClicked(int row, int column)
{
    Q_UNUSED(column)
    auto getTransientShortcutCallback = [this](int index) {
        return transientShortcuts.value(index);
    };
    auto *shortcutDialog = new QVShortcutDialog(row, getTransientShortcutCallback, this);
    connect(shortcutDialog, &QVShortcutDialog::shortcutsListChanged, this,
            [this](int index, const QStringList &stringListShortcuts) {
                transientShortcuts.replace(index, stringListShortcuts);
                updateShortcutsTable();
            });
    shortcutDialog->open();
}

void QVOptionsDialog::buttonBoxClicked(QAbstractButton *button)
{
    auto role = ui->buttonBox->buttonRole(button);
    if (role == QDialogButtonBox::AcceptRole || role == QDialogButtonBox::ApplyRole) {
        saveSettings();
        if (role == QDialogButtonBox::ApplyRole)
            button->setEnabled(false);
    } else if (role == QDialogButtonBox::ResetRole) {
        syncSettings(true);
        syncShortcuts(true);
    }
}

void QVOptionsDialog::updateButtonBox()
{
    QPushButton *defaultsButton = ui->buttonBox->button(QDialogButtonBox::RestoreDefaults);
    QPushButton *applyButton = ui->buttonBox->button(QDialogButtonBox::Apply);
    defaultsButton->setEnabled(false);
    applyButton->setEnabled(false);

    // settings
    const QList<QString> settingKeys = transientSettings.keys();
    for (const auto &key : settingKeys) {
        const auto &transientValue = transientSettings.value(key);
        const auto &savedValue = qvApp->getSettingsManager().getSetting(key);
        const auto &defaultValue = qvApp->getSettingsManager().getSetting(key, true);

        if (transientValue != savedValue)
            applyButton->setEnabled(true);
        if (transientValue != defaultValue)
            defaultsButton->setEnabled(true);
    }

    // shortcuts
    const QList<ShortcutManager::SShortcut> &shortcutsList =
            qvApp->getShortcutManager().getShortcutsList();
    for (int i = 0; i < transientShortcuts.length(); i++) {
        const auto &transientValue = transientShortcuts.value(i);
        QStringList savedValue = shortcutsList.value(i).shortcuts;
        QStringList defaultValue = shortcutsList.value(i).defaultShortcuts;

        if (transientValue != savedValue)
            applyButton->setEnabled(true);
        if (transientValue != defaultValue)
            defaultsButton->setEnabled(true);
    }
}

void QVOptionsDialog::bgColorButtonClicked(QPushButton *button, const QString &key)
{
    auto *colorDialog = new QColorDialog(button->text(), this);
    colorDialog->setAttribute(Qt::WA_DeleteOnClose);
    colorDialog->setWindowModality(Qt::WindowModal);
    connect(colorDialog, &QDialog::accepted, colorDialog, [this, colorDialog, button, key] {
        auto selectedColor = colorDialog->currentColor();

        if (!selectedColor.isValid())
            return;

        modifySetting(key, selectedColor.name());
        button->setText(selectedColor.name());
        updateBgColorButton(button);
    });
    colorDialog->open();
}

void QVOptionsDialog::updateBgColorButton(QPushButton *button)
{
    QPixmap newPixmap = QPixmap(32, 32);
    newPixmap.fill(button->text());
    button->setIcon(QIcon(newPixmap));
}

void QVOptionsDialog::bgColorCheckboxStateChanged(int arg1)
{
    if (arg1 > 0)
        ui->bgColorButton->setEnabled(true);
    else
        ui->bgColorButton->setEnabled(false);

    updateBgColorButton(ui->bgColorButton);
}

void QVOptionsDialog::scalingCheckboxStateChanged(int arg1)
{
    if (arg1 > 0)
        ui->scalingTwoCheckbox->setEnabled(true);
    else
        ui->scalingTwoCheckbox->setEnabled(false);
}

void QVOptionsDialog::windowResizeComboBoxCurrentIndexChanged(int index)
{
    bool enableRelatedControls = index != 0;
    ui->minWindowResizeLabel->setEnabled(enableRelatedControls);
    ui->minWindowResizeSpinBox->setEnabled(enableRelatedControls);
    ui->maxWindowResizeLabel->setEnabled(enableRelatedControls);
    ui->maxWindowResizeSpinBox->setEnabled(enableRelatedControls);
}

void QVOptionsDialog::populateCategories(int selectedRow)
{
    const int iconSize = 24;
    const int listRightPadding = 3;
    auto addItem = [&](const QChar &iconChar, const QString &text) {
        ui->categoryList->addItem(new QListWidgetItem(qvApp->iconFromFont("Material Icons Outlined", iconChar, iconSize, devicePixelRatioF()), text));
    };
    ui->categoryList->setIconSize(QSize(iconSize, iconSize));
    ui->categoryList->setFont(QApplication::font());
    const QString currentStyle = qApp->style()->objectName();
    if (currentStyle.compare("fusion", Qt::CaseInsensitive) == 0 ||
        currentStyle.compare("macos", Qt::CaseInsensitive) == 0)
    {
        const QColor textColor = QApplication::palette().color(QPalette::WindowText);
        QPalette palette = ui->categoryList->palette();
        palette.setColor(QPalette::HighlightedText, textColor);
        palette.setColor(QPalette::Highlight, qvApp->getPerceivedBrightness(textColor) > 0.5 ? QColor(0, 65, 127) : QColor(75, 166, 255));
        ui->categoryList->setPalette(palette);
    }
    ui->categoryList->clear();
    addItem(u'\ue069', tr("Window"));
    addItem(u'\ue3f4', tr("Image"));
    addItem(u'\ue429', tr("Miscellaneous"));
    addItem(u'\ue312', tr("Shortcuts"));
    ui->categoryList->setCurrentRow(selectedRow);
    ui->categoryList->setFixedWidth(ui->categoryList->sizeHintForColumn(0) + ui->categoryList->frameWidth() + listRightPadding);
}

void QVOptionsDialog::populateLanguages()
{
    ui->langComboBox->clear();

    ui->langComboBox->addItem(tr("System Language"), "system");

    // Put english at the top seperately because it has no file
    QLocale eng("en");
    ui->langComboBox->addItem("English (en)", "en");

    const auto entries = QDir(":/i18n/").entryList();
    for (auto entry : entries) {
        entry.remove(0, QStringLiteral("qmedia_").length());
        entry.remove(entry.length() - 3, 3);
        QLocale locale(entry);

        const QString langString = locale.nativeLanguageName() + " (" + entry + ")";

        ui->langComboBox->addItem(langString, entry);
    }
}

void QVOptionsDialog::languageComboBoxCurrentIndexChanged(int index)
{
    Q_UNUSED(index)
    if (!languageRestartMessageShown) {
        QMessageBox::information(this, tr("Restart Required"),
                                 tr("You must restart qMedia to change the language."));
        languageRestartMessageShown = true;
    }
}

void QVOptionsDialog::mediaBackendComboBoxCurrentIndexChanged(int index)
{
    Q_UNUSED(index)
    if (!mediaBackendRestartMessageShown) {
        QMessageBox::information(this, tr("Restart Required"),
                                 tr("You must restart qMedia to change the video backend."));
        mediaBackendRestartMessageShown = true;
    }
}

void QVOptionsDialog::scrollZoomsComboBoxCurrentIndexChanged(int index)
{
    const bool zoomScrollEnabled = index != 2;
    ui->fractionalZoomCheckbox->setEnabled(zoomScrollEnabled);
}
