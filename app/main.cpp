// main.cpp — Polaris Studio desktop application (Phase 1)
//
// Wires up the engine subprocess lifecycle and IPC client, then launches
// the QML UI with props/metrics gauges and a power button.

#include "engine-process.h"
#include "engine-client.h"
#include "database.h"
#include "library.h"
#include "model-downloader.h"
#include "image-engine-process.h"
#include "app-settings.h"
#include "system-monitor.h"
#include <QApplication>
#include <QMenu>
#include <QQmlApplicationEngine>
#include <QQmlContext>
#include <QSettings>
#include <QStandardPaths>
#include <QDir>
#include <QTimer>
#include <QQuickWindow>
#include <QQuickStyle>
#include <QSystemTrayIcon>
#include <QStyle>
#include <QDebug>

int main(int argc, char *argv[]) {
    QApplication app(argc, argv);
    qInfo() << "Polaris Studio initializing";
    QQuickStyle::setStyle(QStringLiteral("Basic"));
    app.setOrganizationName("PolarisStudio");
    app.setApplicationName("polaris-studio");
    app.setApplicationDisplayName("Polaris Studio");

    // Default directories (Debian-friendly, zero-config)
    QString defaultModelsDir = QStandardPaths::writableLocation(QStandardPaths::AppDataLocation) + "/models";
    QString defaultAdaptersDir = QStandardPaths::writableLocation(QStandardPaths::AppLocalDataLocation) + "/adapters";
    QDir().mkpath(defaultModelsDir);
    QDir().mkpath(defaultAdaptersDir);

    QSettings settings;
    QString modelsDir = settings.value("models/dir", defaultModelsDir).toString();
    QString adaptersDir = settings.value("adapters/dir", defaultAdaptersDir).toString();
    qInfo() << "Polaris model directory:" << modelsDir;

    // Engine process manager
    EngineProcess engineProcess;
    engineProcess.setModelsDir(modelsDir);
    engineProcess.setAdaptersDir(adaptersDir);

    // IPC client
    EngineClient engineClient;
    engineClient.setSocketPath(engineProcess.socketPath());

    // Establish IPC independently from QML RPC calls. The worker process can
    // exist briefly before its Unix socket is listening, so retry until the
    // socket is ready and disconnect promptly when the worker stops.
    QTimer ipcConnectTimer;
    ipcConnectTimer.setInterval(500);
    QObject::connect(&ipcConnectTimer, &QTimer::timeout, [&]() {
        if (!engineProcess.isRunning()) {
            ipcConnectTimer.stop();
            return;
        }
        if (engineClient.isConnected()) {
            ipcConnectTimer.stop();
            return;
        }
        if (engineClient.connectToEngine()) {
            qDebug() << "Polaris IPC connected:" << engineClient.socketPath();
            ipcConnectTimer.stop();
        }
    });
    QObject::connect(&engineProcess, &EngineProcess::stateChanged, [&]() {
        if (engineProcess.isRunning()) {
            if (!engineClient.isConnected()) ipcConnectTimer.start();
        } else {
            ipcConnectTimer.stop();
            engineClient.disconnectFromEngine();
        }
    });

    // Database + library
    Database db;
    Library library(&db);
    ModelDownloader downloader;
    ImageEngineProcess imageEngine;
    imageEngine.setAssetsDir(QStandardPaths::writableLocation(QStandardPaths::AppDataLocation) + "/assets");
    AppSettings appSettings;
    SystemMonitor systemMonitor;
    qInfo() << "Polaris services initialized";

    // Restore library path from settings or use default
    QString libPath = db.getSetting("library_path");
    if (!libPath.isEmpty()) library.setPath(libPath);

    // Starting QProcess is asynchronous, so it is safe to launch before QML
    // construction. This also prevents slow multimedia/UI initialization from
    // delaying the local server indefinitely.
    qInfo() << "Polaris engine auto-start requested";
    engineProcess.start();

    // QML engine
    QQmlApplicationEngine qmlEngine;
    qmlEngine.rootContext()->setContextProperty("engineProcess", &engineProcess);
    qmlEngine.rootContext()->setContextProperty("engineClient", &engineClient);
    qmlEngine.rootContext()->setContextProperty("library", &library);
    qmlEngine.rootContext()->setContextProperty("downloader", &downloader);
    qmlEngine.rootContext()->setContextProperty("imageEngine", &imageEngine);
    qmlEngine.rootContext()->setContextProperty("themeSettings", &appSettings);
    qmlEngine.rootContext()->setContextProperty("systemMonitor", &systemMonitor);

    qInfo() << "Loading Polaris UI";
    qmlEngine.load(QUrl("qrc:/main.qml"));
    if (qmlEngine.rootObjects().isEmpty())
        return -1;
    qInfo() << "Polaris UI loaded";

    // System tray icon
    QSystemTrayIcon *tray = new QSystemTrayIcon(&app);
    tray->setIcon(QIcon(":/polaris-studio.svg"));
    // Fallback: use a built-in icon if our SVG isn't found
    if (tray->icon().isNull())
        tray->setIcon(QApplication::style()->standardIcon(QStyle::SP_MediaPlay));

    QMenu *trayMenu = new QMenu();
    QAction *showAction = trayMenu->addAction("Show Polaris Studio");
    QObject::connect(showAction, &QAction::triggered, [&qmlEngine]() {
        if (!qmlEngine.rootObjects().isEmpty()) {
            QWindow *win = qobject_cast<QWindow*>(qmlEngine.rootObjects().first());
            if (win) { win->show(); win->raise(); win->requestActivate(); }
        }
    });
    trayMenu->addSeparator();
    QAction *quitAction = trayMenu->addAction("Quit");
    QObject::connect(quitAction, &QAction::triggered, &app, &QApplication::quit);
    tray->setContextMenu(trayMenu);

    // Double-click shows the window
    QObject::connect(tray, &QSystemTrayIcon::activated, [&qmlEngine](QSystemTrayIcon::ActivationReason reason) {
        if (reason == QSystemTrayIcon::DoubleClick || reason == QSystemTrayIcon::Trigger) {
            if (!qmlEngine.rootObjects().isEmpty()) {
                QWindow *win = qobject_cast<QWindow*>(qmlEngine.rootObjects().first());
                if (win) { win->show(); win->raise(); win->requestActivate(); }
            }
        }
    });
    tray->show();
    qInfo() << "Polaris Studio event loop starting";

    return app.exec();
}
