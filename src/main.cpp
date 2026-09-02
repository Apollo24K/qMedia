#include "mainwindow.h"
#include "qvapplication.h"
#include "qvvideoview.h"
#include "qvwin32functions.h"

#include <QCommandLineParser>
#include <QIcon>
#include <QSettings>
#include <QTimer>

namespace {
void migrateLegacySettings()
{
    QSettings currentSettings;
    const QString migrationKey = QStringLiteral("migration/qViewSettingsImported");
    if (currentSettings.value(migrationKey, false).toBool())
        return;

    if (currentSettings.allKeys().isEmpty()) {
        QSettings legacySettings(QStringLiteral("qView"), QStringLiteral("qView"));
        for (const QString &key : legacySettings.allKeys())
            currentSettings.setValue(key, legacySettings.value(key));
    }
    currentSettings.setValue(migrationKey, true);
    currentSettings.sync();
}
}

int main(int argc, char *argv[])
{
#if QT_VERSION < QT_VERSION_CHECK(6, 0, 0)
    QCoreApplication::setAttribute(Qt::AA_EnableHighDpiScaling);
#endif
    QCoreApplication::setOrganizationName("qMedia");
    QCoreApplication::setApplicationName("qMedia");
    QCoreApplication::setApplicationVersion(QString::number(VERSION));
    migrateLegacySettings();
#if defined Q_OS_WIN && QT_VERSION >= QT_VERSION_CHECK(6, 0, 0)
    // The native backend provides working audio on Windows installations where
    // Qt's FFmpeg backend decodes video but produces no audible output. Preserve
    // an explicit override for troubleshooting and format-compatibility testing.
    if (qEnvironmentVariableIsEmpty("QT_MEDIA_BACKEND"))
        qputenv("QT_MEDIA_BACKEND", QByteArrayLiteral("windows"));
#endif
    QVApplication app(argc, argv);
    app.setWindowIcon(QIcon(QStringLiteral(":/images/qMedia-icon.png")));

    // QAudioOutput performs expensive once-per-process device initialization on
    // some systems. Start it off the GUI thread after the event loop begins so
    // image startup and the first video frame are not blocked by audio discovery.
    QTimer::singleShot(50, &app, []() { QVVideoView::startAudioBackendWarmup(); });

    QCommandLineParser parser;
    parser.addHelpOption();
    parser.addVersionOption();
    parser.addPositionalArgument(QObject::tr("file"), QObject::tr("The file to open."));
#if defined Q_OS_WIN && WIN32_LOADED && QT_VERSION < QT_VERSION_CHECK(6, 7, 2)
    // Workaround for unicode characters getting mangled in certain cases. To support unicode
    // arguments on Windows, QCoreApplication normally ignores argv and gets them from the Windows
    // API instead. But this only happens if it thinks argv hasn't been modified prior to being
    // passed into QCoreApplication's constructor. Certain characters like U+2033 (double prime) get
    // converted differently in argv versus the value Qt is comparing with (__argv). This makes Qt
    // incorrectly think the data was changed, and it skips fetching unicode arguments from the API.
    // https://bugreports.qt.io/browse/QTBUG-125380
    parser.process(QVWin32Functions::getCommandLineArgs());
#else
    parser.process(app);
#endif

    auto *window = QVApplication::newWindow();
    if (!parser.positionalArguments().isEmpty())
        QVApplication::openFile(window, parser.positionalArguments().constFirst(), true);

    return QApplication::exec();
}
