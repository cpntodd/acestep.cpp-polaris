// image-engine-process.h — optional stable-diffusion.cpp CLI adapter

#pragma once

#include <QObject>
#include <QProcess>
#include <QString>

class ImageEngineProcess final : public QObject {
    Q_OBJECT
    Q_PROPERTY(bool available READ available NOTIFY availabilityChanged)
    Q_PROPERTY(bool busy READ busy NOTIFY busyChanged)
    Q_PROPERTY(QString state READ state NOTIFY stateChanged)
    Q_PROPERTY(QString executable READ executable NOTIFY availabilityChanged)
    Q_PROPERTY(QString outputPath READ outputPath NOTIFY outputChanged)
    Q_PROPERTY(int outputRevision READ outputRevision NOTIFY outputChanged)
    Q_PROPERTY(QString status READ status NOTIFY statusChanged)
    Q_PROPERTY(QString log READ log NOTIFY logChanged)

public:
    explicit ImageEngineProcess(QObject *parent = nullptr);
    ~ImageEngineProcess() override;

    bool available() const { return m_available; }
    bool busy() const { return m_process && m_process->state() != QProcess::NotRunning; }
    QString state() const { return m_state; }
    QString executable() const { return m_executable; }
    QString outputPath() const { return m_outputPath; }
    int outputRevision() const { return m_outputRevision; }
    QString status() const { return m_status; }
    QString log() const { return m_log; }

    void setAssetsDir(const QString &dir);

public slots:
    void refresh();
    void cancel();

    Q_INVOKABLE bool generate(const QString &profileId,
                              const QString &modelsDir,
                              const QString &prompt,
                              const QString &negativePrompt,
                              const QString &assetType,
                              int width,
                              int height,
                              int steps,
                              int seed);

signals:
    void availabilityChanged();
    void busyChanged();
    void stateChanged();
    void outputChanged();
    void statusChanged();
    void logChanged();

private slots:
    void onStarted();
    void onFinished(int exitCode, QProcess::ExitStatus exitStatus);
    void onError(QProcess::ProcessError error);
    void onReadyRead();

private:
    QString resolveExecutable() const;
    bool resolveProfile(const QString &profileId,
                        const QString &modelsDir,
                        QString &modelPath,
                        QString &adapterName,
                        QString &promptPrefix) const;
    void appendLog(const QString &line);
    void setStatus(const QString &status);

    QProcess *m_process = nullptr;
    QString m_assetsDir;
    QString m_executable;
    QString m_outputPath;
    QString m_log;
    QString m_state = QStringLiteral("unavailable");
    QString m_status = QStringLiteral("stable-diffusion.cpp runtime not found");
    int m_outputRevision = 0;
    bool m_available = false;
};
