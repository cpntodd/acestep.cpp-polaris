// engine-process.h — manage the polaris-engine worker subprocess
//
// Spawns polaris-engine, monitors its health via a periodic timer, and
// auto-restarts it on unexpected exit. Exposes state to QML.

#pragma once

#include <QObject>
#include <QProcess>
#include <QString>
#include <QTimer>

class EngineProcess : public QObject {
    Q_OBJECT
    Q_PROPERTY(QString state READ state NOTIFY stateChanged)
    Q_PROPERTY(bool running READ isRunning NOTIFY stateChanged)
    Q_PROPERTY(QString modelsDir READ modelsDir WRITE setModelsDir NOTIFY modelsDirChanged)
    Q_PROPERTY(QString adaptersDir READ adaptersDir WRITE setAdaptersDir NOTIFY adaptersDirChanged)
    Q_PROPERTY(QString socketPath READ socketPath NOTIFY socketPathChanged)

public:
    explicit EngineProcess(QObject *parent = nullptr);
    ~EngineProcess() override;

    QString state() const { return m_state; }
    bool isRunning() const;
    QString modelsDir() const { return m_modelsDir; }
    QString adaptersDir() const { return m_adaptersDir; }
    QString socketPath() const;

    void setModelsDir(const QString &dir);
    void setAdaptersDir(const QString &dir);

    Q_INVOKABLE void chooseModelsDir();
    Q_INVOKABLE void refreshModels();

public slots:
    void start();
    void stop();

signals:
    void stateChanged();
    void modelsDirChanged();
    void adaptersDirChanged();
    void socketPathChanged();
    void engineOutput(const QString &line);

private slots:
    void onProcessStarted();
    void onProcessFinished(int exitCode, QProcess::ExitStatus status);
    void onProcessError(QProcess::ProcessError error);
    void onReadyRead();
    void onCheckHealth();

private:
    QString resolveEnginePath() const;

    QProcess *m_process = nullptr;
    QTimer   *m_healthTimer = nullptr;
    QString   m_state = "off";  // "off" | "starting" | "on"
    QString   m_modelsDir;
    QString   m_adaptersDir;
    bool      m_socketSeen = false;
    int       m_restartAttempts = 0;
    static const int MAX_RESTART_ATTEMPTS = 5;
};
