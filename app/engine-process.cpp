// engine-process.cpp — polaris-engine worker subprocess lifecycle

#include "engine-process.h"
#include <QCoreApplication>
#include <QDir>
#include <QFileInfo>
#include <QStandardPaths>
#include <QDebug>
#include <QFileDialog>
#include <QSettings>
#include <unistd.h>

EngineProcess::EngineProcess(QObject *parent)
    : QObject(parent) {
    m_healthTimer = new QTimer(this);
    m_healthTimer->setInterval(3000);
    connect(m_healthTimer, &QTimer::timeout, this, &EngineProcess::onCheckHealth);
}

EngineProcess::~EngineProcess() {
    stop();
}

bool EngineProcess::isRunning() const {
    return m_process && m_process->state() != QProcess::NotRunning;
}

void EngineProcess::setModelsDir(const QString &dir) {
    if (m_modelsDir != dir) {
        m_modelsDir = dir;
        emit modelsDirChanged();
    }
}

void EngineProcess::setAdaptersDir(const QString &dir) {
    if (m_adaptersDir != dir) {
        m_adaptersDir = dir;
        emit adaptersDirChanged();
    }
}

void EngineProcess::chooseModelsDir() {
    const QString chosen = QFileDialog::getExistingDirectory(
        nullptr, QStringLiteral("Choose Models Directory"), m_modelsDir);
    if (!chosen.isEmpty()) {
        setModelsDir(chosen);
        QSettings settings;
        settings.setValue(QStringLiteral("models/dir"), m_modelsDir);
        settings.sync();
    }
}

void EngineProcess::refreshModels() {
    // The registry is built during engine_init(), so a refresh must create a
    // fresh worker with the current paths. This also handles a path change
    // while the worker is booting or already online.
    stop();
    QTimer::singleShot(0, this, &EngineProcess::start);
}

QString EngineProcess::socketPath() const {
    const char *runtime = getenv("XDG_RUNTIME_DIR");
    QString runtimeDir;
    if (runtime && *runtime) {
        runtimeDir = QString::fromUtf8(runtime);
    } else {
        // Some desktop launchers and minimal environments do not export
        // XDG_RUNTIME_DIR. Keep IPC usable with a private per-user fallback.
        runtimeDir = QDir::temp().filePath(
            QStringLiteral("polaris-runtime-") + QString::number(geteuid()));
        QDir().mkpath(runtimeDir);
        QFile::setPermissions(runtimeDir, QFileDevice::ReadOwner | QFileDevice::WriteOwner |
                                           QFileDevice::ExeOwner);
    }
    return QDir(runtimeDir).filePath(QStringLiteral("polaris/engine.sock"));
}

QString EngineProcess::resolveEnginePath() const {
    // Look next to the app binary first (packaged layout), then in PATH
    QString appDir = QCoreApplication::applicationDirPath();
    QStringList candidates = {
        appDir + "/polaris-engine",
        appDir + "/../lib/polaris-studio/polaris-engine",
        QStringLiteral("polaris-engine"),  // PATH
    };
    for (const auto &c : candidates) {
        if (QFileInfo::exists(c)) return c;
    }
    return candidates.last();  // fallback to PATH
}

void EngineProcess::start() {
    if (m_state == "on" || m_state == "starting") return;

    m_restartAttempts = 0;
    QString enginePath = resolveEnginePath();

    if (!QFileInfo::exists(enginePath)) {
        qWarning() << "polaris-engine not found at" << enginePath;
        return;
    }
    if (m_modelsDir.isEmpty() || m_adaptersDir.isEmpty()) {
        qWarning() << "Models or adapters dir not set";
        return;
    }

    m_state = "starting";
    m_socketSeen = false;
    emit stateChanged();

    // Remove stale socket from a previous crashed instance
    QFile::remove(socketPath());

    m_process = new QProcess(this);
    connect(m_process, &QProcess::started, this, &EngineProcess::onProcessStarted);
    connect(m_process, QOverload<int, QProcess::ExitStatus>::of(&QProcess::finished),
            this, &EngineProcess::onProcessFinished);
    connect(m_process, &QProcess::errorOccurred, this, &EngineProcess::onProcessError);
    connect(m_process, &QProcess::readyReadStandardError, this, &EngineProcess::onReadyRead);

    QStringList args;
    args << "--models" << m_modelsDir
         << "--adapters" << m_adaptersDir
         << "--socket" << socketPath();

    qDebug() << "Starting polaris-engine:" << enginePath << args;
    m_process->start(enginePath, args);
    if (!m_process->waitForStarted(5000)) {
        qWarning() << "polaris-engine failed to start:" << m_process->errorString();
        m_state = "off";
        emit stateChanged();
    }
}

void EngineProcess::stop() {
    m_healthTimer->stop();
    if (m_process) {
        m_process->disconnect();
        if (m_process->state() != QProcess::NotRunning) {
            m_process->terminate();
            if (!m_process->waitForFinished(5000)) {
                m_process->kill();
                m_process->waitForFinished(2000);
            }
        }
        m_process->deleteLater();
        m_process = nullptr;
    }
    m_restartAttempts = 0;
    m_socketSeen = false;
    m_state = "off";
    emit stateChanged();
}

void EngineProcess::onProcessStarted() {
    m_state = "on";
    m_healthTimer->start();
    emit stateChanged();
}

void EngineProcess::onProcessFinished(int exitCode, QProcess::ExitStatus status) {
    m_healthTimer->stop();
    qDebug() << "polaris-engine exited, code:" << exitCode << "status:" << status;

    if (m_restartAttempts < MAX_RESTART_ATTEMPTS && m_state != "off") {
        m_restartAttempts++;
        qDebug() << "Auto-restarting polaris-engine (attempt" << m_restartAttempts << ")";
        m_state = "starting";
        emit stateChanged();
        // Re-spawn after a brief delay
        QTimer::singleShot(1000, this, [this]() {
            if (m_state == "starting") {
                delete m_process;
                m_process = nullptr;
                // start() deliberately ignores calls while marked starting.
                // Move to off for the relaunch while preserving the retry
                // counter that start() resets for user-initiated starts.
                const int attempts = m_restartAttempts;
                m_state = "off";
                start();
                m_restartAttempts = attempts;
            }
        });
    } else {
        m_state = "off";
        emit stateChanged();
    }
}

void EngineProcess::onProcessError(QProcess::ProcessError error) {
    qWarning() << "polaris-engine process error:" << error;
    if (!isRunning()) {
        m_state = "off";
        emit stateChanged();
    }
}

void EngineProcess::onReadyRead() {
    while (m_process && m_process->canReadLine()) {
        QString line = QString::fromUtf8(m_process->readLine()).trimmed();
        if (!line.isEmpty()) emit engineOutput(line);
    }
}

void EngineProcess::onCheckHealth() {
    if (!isRunning()) return;
    // Model and Vulkan initialization may take minutes. Do not kill a live
    // worker merely because it has not created its socket yet. Once the socket
    // has existed, its disappearance is a useful crash/health signal.
    QFileInfo info(socketPath());
    if (info.exists()) {
        m_socketSeen = true;
        return;
    }
    if (m_socketSeen) {
        qWarning() << "Engine socket not found, treating as crash";
        m_healthTimer->stop();
        if (m_process) {
            m_process->terminate();
            m_process->waitForFinished(3000);
            m_process->kill();
        }
    }
}
