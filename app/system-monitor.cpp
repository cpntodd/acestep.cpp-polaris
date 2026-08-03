#include "system-monitor.h"

#include <QDir>
#include <QDebug>
#include <QFile>
#include <QFileInfo>
#include <QStandardPaths>
#include <QTextStream>
#include <QThread>

#include <fstream>
#include <string>

namespace {

double percent(double value) {
    return qBound(0.0, value, 100.0);
}

double filteredGauge(double current, double target, bool initialized, double response = 0.38) {
    return initialized ? current + (target - current) * response : target;
}

bool readNumber(const QString &path, qulonglong &value) {
    QFile file(path);
    if (!file.open(QIODevice::ReadOnly | QIODevice::Text)) return false;
    bool ok = false;
    const qulonglong parsed = QString::fromLatin1(file.readLine()).trimmed().toULongLong(&ok);
    if (ok) value = parsed;
    return ok;
}

QString driverName(const QString &devicePath) {
    const QString target = QFileInfo(QDir(devicePath).filePath(QStringLiteral("driver"))).symLinkTarget();
    return target.isEmpty() ? QString() : QFileInfo(target).fileName();
}

} // namespace

SystemMonitor::SystemMonitor(QObject *parent)
    : QObject(parent), m_cpuCores(qMax(1, QThread::idealThreadCount())) {
    discoverGpu();
    m_nvidiaSmi = QStandardPaths::findExecutable(QStringLiteral("nvidia-smi"));
    connect(&m_nvidiaProcess, QOverload<int, QProcess::ExitStatus>::of(&QProcess::finished),
            this, &SystemMonitor::readNvidiaResult);
    connect(&m_timer, &QTimer::timeout, this, &SystemMonitor::sample);
    m_timer.setTimerType(Qt::PreciseTimer);
    m_timer.setInterval(refreshInterval());
    sample();
    m_timer.start();
}

void SystemMonitor::discoverGpu() {
#ifdef Q_OS_LINUX
    const QDir drm(QStringLiteral("/sys/class/drm"));
    const QStringList cards = drm.entryList({QStringLiteral("card[0-9]*")},
                                             QDir::Dirs | QDir::System | QDir::NoDotAndDotDot,
                                             QDir::Name);
    for (const QString &card : cards) {
        const QString device = drm.filePath(card + QStringLiteral("/device"));
        if (QFileInfo::exists(QDir(device).filePath(QStringLiteral("gpu_busy_percent"))) ||
            QFileInfo::exists(QDir(device).filePath(QStringLiteral("mem_info_vram_total")))) {
            m_gpuDevicePath = device;
            const QString driver = driverName(device);
            m_gpuName = driver.isEmpty() ? card.toUpper() : card.toUpper() + QStringLiteral(" / ") + driver.toUpper();
            return;
        }
    }
#endif
}

void SystemMonitor::sample() {
    sampleCpu();
    sampleRam();
    sampleDrm();
    if (m_gpuDevicePath.isEmpty() && !m_nvidiaSmi.isEmpty() && (++m_nvidiaTick % 4) == 0)
        requestNvidiaSample();
    if (++m_sampleCount == 3) {
        qInfo().nospace() << "Polaris telemetry: CPU=" << m_cpuUsage << "% RAM="
                          << m_ramUsage << "% (" << m_ramUsed << "/" << m_ramTotal
                          << ") GPU=" << m_gpuUsage << "% VRAM=" << m_vramUsage
                          << "% (" << m_vramUsed << "/" << m_vramTotal << ") device="
                          << m_gpuName;
    }
    emit telemetryChanged();
}

void SystemMonitor::sampleCpu() {
#ifdef Q_OS_LINUX
    QFile file(QStringLiteral("/proc/stat"));
    if (!file.open(QIODevice::ReadOnly | QIODevice::Text)) {
        m_cpuAvailable = false;
        return;
    }
    const QList<QByteArray> fields = file.readLine().simplified().split(' ');
    if (fields.size() < 9 || fields.first() != "cpu") {
        m_cpuAvailable = false;
        return;
    }
    quint64 values[8] = {};
    bool ok = true;
    for (int i = 0; i < 8; ++i) {
        bool fieldOk = false;
        values[i] = fields.at(i + 1).toULongLong(&fieldOk);
        ok = ok && fieldOk;
    }
    if (!ok) {
        m_cpuAvailable = false;
        return;
    }
    const quint64 idle = values[3] + values[4];
    quint64 total = 0;
    for (quint64 value : values) total += value;
    if (m_previousCpuTotal > 0 && total > m_previousCpuTotal) {
        const quint64 totalDelta = total - m_previousCpuTotal;
        const quint64 idleDelta = idle >= m_previousCpuIdle ? idle - m_previousCpuIdle : 0;
        const double rawUsage = percent(
            100.0 * static_cast<double>(totalDelta - qMin(totalDelta, idleDelta)) /
            static_cast<double>(totalDelta));
        m_cpuUsage = filteredGauge(m_cpuUsage, rawUsage, m_cpuAvailable);
    }
    m_previousCpuTotal = total;
    m_previousCpuIdle = idle;
    m_cpuAvailable = true;
#else
    m_cpuAvailable = false;
#endif
}

void SystemMonitor::sampleRam() {
#ifdef Q_OS_LINUX
    std::ifstream meminfo("/proc/meminfo");
    if (!meminfo) {
        m_ramAvailable = false;
        return;
    }
    qulonglong totalKiB = 0;
    qulonglong availableKiB = 0;
    std::string label;
    std::string unit;
    qulonglong valueKiB = 0;
    while (meminfo >> label >> valueKiB >> unit) {
        if (label == "MemTotal:") totalKiB = valueKiB;
        else if (label == "MemAvailable:") availableKiB = valueKiB;
    }
    const bool wasAvailable = m_ramAvailable;
    m_ramAvailable = totalKiB > 0 && availableKiB <= totalKiB;
    if (!m_ramAvailable) return;
    m_ramTotal = totalKiB * 1024ULL;
    m_ramUsed = (totalKiB - availableKiB) * 1024ULL;
    const double rawUsage = percent(100.0 * static_cast<double>(m_ramUsed) /
                                    static_cast<double>(m_ramTotal));
    m_ramUsage = filteredGauge(m_ramUsage, rawUsage, wasAvailable);
#else
    m_ramAvailable = false;
#endif
}

void SystemMonitor::sampleDrm() {
    if (m_gpuDevicePath.isEmpty()) return;
    qulonglong usage = 0;
    if (readNumber(QDir(m_gpuDevicePath).filePath(QStringLiteral("gpu_busy_percent")), usage)) {
        const bool wasAvailable = m_gpuAvailable;
        m_gpuAvailable = true;
        m_gpuUsage = filteredGauge(m_gpuUsage, percent(static_cast<double>(usage)), wasAvailable, 0.32);
    }
    qulonglong used = 0;
    qulonglong total = 0;
    if (readNumber(QDir(m_gpuDevicePath).filePath(QStringLiteral("mem_info_vram_used")), used) &&
        readNumber(QDir(m_gpuDevicePath).filePath(QStringLiteral("mem_info_vram_total")), total) && total > 0) {
        const bool wasAvailable = m_vramAvailable;
        m_gpuAvailable = true;
        m_vramAvailable = true;
        m_vramUsed = qMin(used, total);
        m_vramTotal = total;
        const double rawUsage = percent(100.0 * static_cast<double>(m_vramUsed) /
                                        static_cast<double>(m_vramTotal));
        m_vramUsage = filteredGauge(m_vramUsage, rawUsage, wasAvailable);
    }
}

void SystemMonitor::requestNvidiaSample() {
    if (m_nvidiaProcess.state() != QProcess::NotRunning) return;
    m_nvidiaProcess.start(m_nvidiaSmi,
        {QStringLiteral("--query-gpu=name,utilization.gpu,memory.used,memory.total"),
         QStringLiteral("--format=csv,noheader,nounits")});
}

void SystemMonitor::readNvidiaResult(int exitCode, QProcess::ExitStatus status) {
    if (exitCode != 0 || status != QProcess::NormalExit) return;
    const QString line = QString::fromUtf8(m_nvidiaProcess.readAllStandardOutput()).split('\n').value(0);
    const QStringList fields = line.split(',');
    if (fields.size() < 4) return;
    bool usageOk = false;
    bool usedOk = false;
    bool totalOk = false;
    const double usage = fields.at(1).trimmed().toDouble(&usageOk);
    const double usedMiB = fields.at(2).trimmed().toDouble(&usedOk);
    const double totalMiB = fields.at(3).trimmed().toDouble(&totalOk);
    if (!usageOk) return;
    const bool gpuWasAvailable = m_gpuAvailable;
    const bool vramWasAvailable = m_vramAvailable;
    m_gpuName = fields.at(0).trimmed();
    m_gpuAvailable = true;
    m_gpuUsage = filteredGauge(m_gpuUsage, percent(usage), gpuWasAvailable, 0.32);
    if (usedOk && totalOk && totalMiB > 0.0) {
        m_vramAvailable = true;
        m_vramUsed = static_cast<qulonglong>(qMax(0.0, usedMiB) * 1024.0 * 1024.0);
        m_vramTotal = static_cast<qulonglong>(totalMiB * 1024.0 * 1024.0);
        const double rawUsage = percent(100.0 * static_cast<double>(m_vramUsed) /
                                        static_cast<double>(m_vramTotal));
        m_vramUsage = filteredGauge(m_vramUsage, rawUsage, vramWasAvailable);
    }
    emit telemetryChanged();
}
