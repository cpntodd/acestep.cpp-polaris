// Lightweight host telemetry for UI gauges.
//
// This deliberately runs in the desktop process and reads operating-system
// counters. It does not call the inference worker or submit GPU work, so gauge
// updates continue while the engine is stopped, loading, or rendering.

#pragma once

#include <QObject>
#include <QProcess>
#include <QString>
#include <QTimer>

class SystemMonitor final : public QObject {
    Q_OBJECT
    Q_PROPERTY(bool cpuAvailable READ cpuAvailable NOTIFY telemetryChanged)
    Q_PROPERTY(double cpuUsage READ cpuUsage NOTIFY telemetryChanged)
    Q_PROPERTY(int cpuCores READ cpuCores CONSTANT)
    Q_PROPERTY(bool ramAvailable READ ramAvailable NOTIFY telemetryChanged)
    Q_PROPERTY(double ramUsage READ ramUsage NOTIFY telemetryChanged)
    Q_PROPERTY(qulonglong ramUsed READ ramUsed NOTIFY telemetryChanged)
    Q_PROPERTY(qulonglong ramTotal READ ramTotal NOTIFY telemetryChanged)
    Q_PROPERTY(bool gpuAvailable READ gpuAvailable NOTIFY telemetryChanged)
    Q_PROPERTY(double gpuUsage READ gpuUsage NOTIFY telemetryChanged)
    Q_PROPERTY(QString gpuName READ gpuName NOTIFY telemetryChanged)
    Q_PROPERTY(bool vramAvailable READ vramAvailable NOTIFY telemetryChanged)
    Q_PROPERTY(double vramUsage READ vramUsage NOTIFY telemetryChanged)
    Q_PROPERTY(qulonglong vramUsed READ vramUsed NOTIFY telemetryChanged)
    Q_PROPERTY(qulonglong vramTotal READ vramTotal NOTIFY telemetryChanged)
    Q_PROPERTY(int refreshInterval READ refreshInterval CONSTANT)

public:
    explicit SystemMonitor(QObject *parent = nullptr);

    bool cpuAvailable() const { return m_cpuAvailable; }
    double cpuUsage() const { return m_cpuUsage; }
    int cpuCores() const { return m_cpuCores; }
    bool ramAvailable() const { return m_ramAvailable; }
    double ramUsage() const { return m_ramUsage; }
    qulonglong ramUsed() const { return m_ramUsed; }
    qulonglong ramTotal() const { return m_ramTotal; }
    bool gpuAvailable() const { return m_gpuAvailable; }
    double gpuUsage() const { return m_gpuUsage; }
    QString gpuName() const { return m_gpuName; }
    bool vramAvailable() const { return m_vramAvailable; }
    double vramUsage() const { return m_vramUsage; }
    qulonglong vramUsed() const { return m_vramUsed; }
    qulonglong vramTotal() const { return m_vramTotal; }
    int refreshInterval() const { return 250; }

signals:
    void telemetryChanged();

private slots:
    void sample();
    void readNvidiaResult(int exitCode, QProcess::ExitStatus status);

private:
    void discoverGpu();
    void sampleCpu();
    void sampleRam();
    void sampleDrm();
    void requestNvidiaSample();

    QTimer m_timer;
    QProcess m_nvidiaProcess;
    QString m_gpuDevicePath;
    QString m_nvidiaSmi;
    quint64 m_previousCpuTotal = 0;
    quint64 m_previousCpuIdle = 0;
    int m_nvidiaTick = 0;
    int m_sampleCount = 0;
    int m_cpuCores = 0;
    bool m_cpuAvailable = false;
    bool m_ramAvailable = false;
    bool m_gpuAvailable = false;
    bool m_vramAvailable = false;
    double m_cpuUsage = 0.0;
    double m_ramUsage = 0.0;
    double m_gpuUsage = 0.0;
    double m_vramUsage = 0.0;
    qulonglong m_ramUsed = 0;
    qulonglong m_ramTotal = 0;
    qulonglong m_vramUsed = 0;
    qulonglong m_vramTotal = 0;
    QString m_gpuName = QStringLiteral("GPU telemetry unavailable");
};
