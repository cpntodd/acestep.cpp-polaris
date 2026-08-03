// image-engine-process.cpp — optional stable-diffusion.cpp CLI adapter

#include "image-engine-process.h"

#include <QCoreApplication>
#include <QDateTime>
#include <QDir>
#include <QFileInfo>
#include <QProcessEnvironment>
#include <QStandardPaths>

namespace {

QString imageRoot(const QString &modelsDir) {
    return QDir(modelsDir).filePath(QStringLiteral("image"));
}

QString adapterRoot(const QString &modelsDir) {
    return QDir(imageRoot(modelsDir)).filePath(QStringLiteral("adapters"));
}

QString baseRoot(const QString &modelsDir, const QString &family) {
    return QDir(imageRoot(modelsDir)).filePath(QStringLiteral("base/") + family);
}

} // namespace

ImageEngineProcess::ImageEngineProcess(QObject *parent)
    : QObject(parent) {
    m_process = new QProcess(this);
    connect(m_process, &QProcess::started, this, &ImageEngineProcess::onStarted);
    connect(m_process, QOverload<int, QProcess::ExitStatus>::of(&QProcess::finished),
            this, &ImageEngineProcess::onFinished);
    connect(m_process, &QProcess::errorOccurred, this, &ImageEngineProcess::onError);
    connect(m_process, &QProcess::readyReadStandardOutput, this, &ImageEngineProcess::onReadyRead);
    connect(m_process, &QProcess::readyReadStandardError, this, &ImageEngineProcess::onReadyRead);
    refresh();
}

ImageEngineProcess::~ImageEngineProcess() {
    cancel();
}

void ImageEngineProcess::setAssetsDir(const QString &dir) {
    m_assetsDir = dir;
    QDir().mkpath(m_assetsDir);
}

QString ImageEngineProcess::resolveExecutable() const {
    const QString envPath = QProcessEnvironment::systemEnvironment().value(QStringLiteral("POLARIS_SD_CLI"));
    if (!envPath.isEmpty() && QFileInfo(envPath).isExecutable()) return envPath;

    const QString appDir = QCoreApplication::applicationDirPath();
    const QStringList localCandidates = {
        QDir(appDir).filePath(QStringLiteral("sd-cli")),
        QDir(appDir).filePath(QStringLiteral("stable-diffusion.cpp/bin/sd-cli")),
        QDir(appDir).filePath(QStringLiteral("../lib/polaris-studio/sd-cli")),
        QDir(appDir).filePath(QStringLiteral("stable-diffusion-cli"))
    };
    for (const QString &candidate : localCandidates) {
        if (QFileInfo(candidate).isExecutable()) return candidate;
    }

    for (const QString &name : {QStringLiteral("sd-cli"),
                                QStringLiteral("stable-diffusion-cli"),
                                QStringLiteral("stable-diffusion")}) {
        const QString found = QStandardPaths::findExecutable(name);
        if (!found.isEmpty()) return found;
    }
    return {};
}

void ImageEngineProcess::refresh() {
    const QString resolved = resolveExecutable();
    const bool wasAvailable = m_available;
    const QString oldExecutable = m_executable;
    m_executable = resolved;
    m_available = !resolved.isEmpty();
    m_state = m_available ? QStringLiteral("ready") : QStringLiteral("unavailable");
    if (!m_available) {
        setStatus(QStringLiteral("stable-diffusion.cpp runtime not found; install sd-cli or set POLARIS_SD_CLI"));
    } else if (!busy()) {
        setStatus(QStringLiteral("sd-cli ready: ") + m_executable);
    }
    if (wasAvailable != m_available || oldExecutable != m_executable) emit availabilityChanged();
    emit stateChanged();
}

bool ImageEngineProcess::resolveProfile(const QString &profileId,
                                        const QString &modelsDir,
                                        QString &modelPath,
                                        QString &adapterName,
                                        QString &promptPrefix) const {
    adapterName.clear();
    if (profileId == QStringLiteral("pixel-art-sd15")) {
        modelPath = QDir(baseRoot(modelsDir, QStringLiteral("sd15")))
                        .filePath(QStringLiteral("v1-5-pruned-emaonly.safetensors"));
        promptPrefix = QStringLiteral("pixel art, game asset, crisp limited palette, ");
    } else if (profileId == QStringLiteral("pixel-art-sdxl")) {
        modelPath = QDir(baseRoot(modelsDir, QStringLiteral("sdxl")))
                        .filePath(QStringLiteral("sd_xl_base_1.0.safetensors"));
        adapterName = QStringLiteral("pixel-art-xl");
        promptPrefix = QStringLiteral("pixel art, detailed game asset, crisp limited palette, ");
    } else if (profileId == QStringLiteral("terrain-tiles")) {
        modelPath = QDir(baseRoot(modelsDir, QStringLiteral("sdxl")))
                        .filePath(QStringLiteral("sd_xl_base_1.0.safetensors"));
        adapterName = QStringLiteral("mapchipLora");
        promptPrefix = QStringLiteral("pixel art, isometric terrain tile, orthographic game map asset, ");
    } else if (profileId == QStringLiteral("fast-preview")) {
        modelPath = QDir(baseRoot(modelsDir, QStringLiteral("sdxl")))
                        .filePath(QStringLiteral("sd_xl_base_1.0.safetensors"));
        adapterName = QStringLiteral("pytorch_lora_weights");
        promptPrefix = QStringLiteral("pixel art concept preview, ");
    } else {
        return false;
    }

    return QFileInfo(modelPath).isFile();
}

void ImageEngineProcess::appendLog(const QString &line) {
    if (line.isEmpty()) return;
    m_log += line + QLatin1Char('\n');
    const QStringList lines = m_log.split(QLatin1Char('\n'));
    if (lines.size() > 240) m_log = lines.mid(lines.size() - 240).join(QLatin1Char('\n'));
    emit logChanged();
}

void ImageEngineProcess::setStatus(const QString &status) {
    if (m_status == status) return;
    m_status = status;
    emit statusChanged();
}

bool ImageEngineProcess::generate(const QString &profileId,
                                  const QString &modelsDir,
                                  const QString &prompt,
                                  const QString &negativePrompt,
                                  const QString &assetType,
                                  int width,
                                  int height,
                                  int steps,
                                  int seed) {
    if (busy()) return false;
    refresh();
    if (!m_available) return false;

    QString modelPath;
    QString adapterName;
    QString promptPrefix;
    if (!resolveProfile(profileId, modelsDir, modelPath, adapterName, promptPrefix)) {
        setStatus(QStringLiteral("Selected model profile is not installed."));
        return false;
    }

    if (prompt.trimmed().isEmpty()) {
        setStatus(QStringLiteral("Enter a prompt before generating."));
        return false;
    }

    if (m_assetsDir.isEmpty()) {
        setStatus(QStringLiteral("Image asset directory is not configured."));
        return false;
    }
    QDir().mkpath(m_assetsDir);
    const QString stamp = QString::number(QDateTime::currentMSecsSinceEpoch());
    m_outputPath = QDir(m_assetsDir).filePath(QStringLiteral("pixel-asset-") + stamp + QStringLiteral(".png"));
    emit outputChanged();

    const QString basePrompt = promptPrefix + assetType.toLower() + QStringLiteral(", ") + prompt;
    const QString generationPrompt = adapterName.isEmpty()
                                         ? basePrompt
                                         : QStringLiteral("<lora:") + adapterName + QStringLiteral(":1.0> ") + basePrompt;

    QStringList args;
    args << QStringLiteral("-m") << modelPath
         << QStringLiteral("-p") << generationPrompt
         << QStringLiteral("-n") << negativePrompt
         << QStringLiteral("-o") << m_outputPath
         << QStringLiteral("--width") << QString::number(qBound(256, width, 1536))
         << QStringLiteral("--height") << QString::number(qBound(256, height, 1536))
         << QStringLiteral("--steps") << QString::number(qBound(1, steps, 100))
         << QStringLiteral("--cfg-scale") << QString::number(profileId == QStringLiteral("fast-preview") ? 1.0 : 7.0)
         << QStringLiteral("--seed") << QString::number(seed);

    const QString backend = QProcessEnvironment::systemEnvironment().value(
        QStringLiteral("POLARIS_SD_BACKEND"), QStringLiteral("vulkan"));
    if (!backend.isEmpty()) args << QStringLiteral("--backend") << backend;

    if (profileId == QStringLiteral("fast-preview")) {
        args << QStringLiteral("--sampling-method") << QStringLiteral("lcm");
    } else {
        args << QStringLiteral("--sampling-method") << QStringLiteral("euler_a");
    }
    args << QStringLiteral("--offload-to-cpu") << QStringLiteral("--vae-tiling");

    if (!adapterName.isEmpty()) args << QStringLiteral("--lora-model-dir") << adapterRoot(modelsDir);

    m_log.clear();
    emit logChanged();
    m_state = QStringLiteral("starting");
    emit stateChanged();
    setStatus(QStringLiteral("Starting local image render…"));
    m_process->setProgram(m_executable);
    m_process->setArguments(args);
    m_process->start();
    emit busyChanged();
    return true;
}

void ImageEngineProcess::cancel() {
    if (!m_process || m_process->state() == QProcess::NotRunning) return;
    m_process->terminate();
    if (!m_process->waitForFinished(1500)) m_process->kill();
    m_state = QStringLiteral("ready");
    setStatus(QStringLiteral("Image render cancelled."));
    emit stateChanged();
    emit busyChanged();
}

void ImageEngineProcess::onStarted() {
    m_state = QStringLiteral("rendering");
    setStatus(QStringLiteral("Rendering locally with stable-diffusion.cpp…"));
    emit stateChanged();
    emit busyChanged();
}

void ImageEngineProcess::onFinished(int exitCode, QProcess::ExitStatus exitStatus) {
    onReadyRead();
    const bool ok = exitStatus == QProcess::NormalExit && exitCode == 0 && QFileInfo(m_outputPath).isFile();
    m_state = QStringLiteral("ready");
    if (ok) {
        ++m_outputRevision;
        setStatus(QStringLiteral("Render complete: ") + m_outputPath);
        emit outputChanged();
    } else if (exitStatus == QProcess::CrashExit) {
        setStatus(QStringLiteral("Image worker crashed; inspect the Pixel Lab log."));
    } else {
        setStatus(QStringLiteral("Image render failed with exit code ") + QString::number(exitCode));
    }
    emit stateChanged();
    emit busyChanged();
}

void ImageEngineProcess::onError(QProcess::ProcessError error) {
    if (error == QProcess::FailedToStart) {
        m_state = QStringLiteral("unavailable");
        setStatus(QStringLiteral("Could not start sd-cli. Check POLARIS_SD_CLI or PATH."));
        emit stateChanged();
        emit busyChanged();
    }
}

void ImageEngineProcess::onReadyRead() {
    if (!m_process) return;
    appendLog(QString::fromUtf8(m_process->readAllStandardOutput()).trimmed());
    appendLog(QString::fromUtf8(m_process->readAllStandardError()).trimmed());
}
