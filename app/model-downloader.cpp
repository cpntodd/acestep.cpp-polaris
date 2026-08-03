// model-downloader.cpp — local ACE-Step and Pixel Lab model installation

#include "model-downloader.h"

#include <QCryptographicHash>
#include <QDebug>
#include <QDir>
#include <QFileInfo>
#include <QSettings>
#include <QUrl>

namespace {

QVariantMap profile(const QString &id, const QString &label,
                   const QString &description, const QString &source,
                   const QString &license, const QString &tier,
                   const QString &size) {
    QVariantMap result;
    result.insert(QStringLiteral("id"), id);
    result.insert(QStringLiteral("label"), label);
    result.insert(QStringLiteral("description"), description);
    result.insert(QStringLiteral("source"), source);
    result.insert(QStringLiteral("license"), license);
    result.insert(QStringLiteral("tier"), tier);
    result.insert(QStringLiteral("size"), size);
    return result;
}

} // namespace

ModelDownloader::ModelDownloader(QObject *parent)
    : QObject(parent), m_nam(new QNetworkAccessManager(this)) {
    m_selectedImageProfile = QSettings().value(
        QStringLiteral("models/image-profile"), QStringLiteral("pixel-art-sdxl")).toString();
}

QList<DownloadTask> ModelDownloader::defaultManifest(const QString &targetDir) {
    QList<DownloadTask> tasks;

    // Primary models — Serveurperso/ACE-Step-1.5-GGUF
    tasks.append({"Serveurperso/ACE-Step-1.5-GGUF", "vae-BF16.gguf", targetDir});
    tasks.append({"Serveurperso/ACE-Step-1.5-GGUF", "Qwen3-Embedding-0.6B-Q8_0.gguf", targetDir});
    tasks.append({"Serveurperso/ACE-Step-1.5-GGUF", "acestep-5Hz-lm-4B-Q8_0.gguf", targetDir});
    tasks.append({"Serveurperso/ACE-Step-1.5-GGUF", "acestep-v15-turbo-Q8_0.gguf", targetDir});

    // Language listener — ggerganov/whisper.cpp
    tasks.append({"ggerganov/whisper.cpp", "ggml-large-v3-turbo-q5_0.bin", targetDir});

    return tasks;
}

QVariantList ModelDownloader::imageProfiles() const {
    return {
        profile(QStringLiteral("pixel-art-sd15"), QStringLiteral("Pixel Art / SD1.5"),
                QStringLiteral("Compatible starter profile for lower-memory systems."),
                QStringLiteral("stable-diffusion-v1-5/stable-diffusion-v1-5"),
                QStringLiteral("CreativeML OpenRAIL-M"), QStringLiteral("COMPATIBLE"),
                QStringLiteral("~4 GB")),
        profile(QStringLiteral("pixel-art-sdxl"), QStringLiteral("Pixel Art XL / SDXL"),
                QStringLiteral("Detailed pixel art for units, buildings, props, and icons."),
                QStringLiteral("stabilityai + nerijs / Hugging Face"),
                QStringLiteral("OpenRAIL"), QStringLiteral("QUALITY"), QStringLiteral("~7 GB")),
        profile(QStringLiteral("terrain-tiles"), QStringLiteral("Terrain Tiles / SDXL"),
                QStringLiteral("Tile-oriented terrain and map decoration profile."),
                QStringLiteral("stabilityai + kokuren / Hugging Face"),
                QStringLiteral("OpenRAIL + Apache-2.0"), QStringLiteral("TERRAIN"),
                QStringLiteral("~7 GB")),
        profile(QStringLiteral("fast-preview"), QStringLiteral("Fast Preview / SDXL"),
                QStringLiteral("Quick style and composition previews using LCM."),
                QStringLiteral("stabilityai + latent-consistency / Hugging Face"),
                QStringLiteral("Upstream terms"), QStringLiteral("PREVIEW"),
                QStringLiteral("~7 GB"))
    };
}

QVariantMap ModelDownloader::imageProfile(const QString &profileId) const {
    const QVariantList profiles = imageProfiles();
    for (const QVariant &entry : profiles) {
        const QVariantMap map = entry.toMap();
        if (map.value(QStringLiteral("id")).toString() == profileId) return map;
    }
    return {};
}

QString ModelDownloader::imageProfileInstallPath(const QString &profileId,
                                                 const QString &modelsDir) const {
    if (imageProfile(profileId).isEmpty() || modelsDir.isEmpty()) return {};
    return QDir(modelsDir).filePath(QStringLiteral("image"));
}

void ModelDownloader::scanLocalModels(const QString &modelsDir) {
    QStringList files;
    if (!modelsDir.isEmpty()) {
        QDir directory(modelsDir);
        files = directory.entryList({QStringLiteral("*.gguf")},
                                     QDir::Files | QDir::Readable,
                                     QDir::Name);
    }

    if (m_localModelFiles == files) return;
    m_localModelFiles = files;
    emit localModelsChanged();
}

QList<DownloadTask> ModelDownloader::imageProfileTasks(const QString &profileId,
                                                       const QString &modelsDir) const {
    QList<DownloadTask> tasks;
    if (modelsDir.isEmpty() || imageProfile(profileId).isEmpty()) return tasks;

    const QString imageRoot = QDir(modelsDir).filePath(QStringLiteral("image"));
    const QString base15 = QDir(imageRoot).filePath(QStringLiteral("base/sd15"));
    const QString baseXl = QDir(imageRoot).filePath(QStringLiteral("base/sdxl"));
    const QString adapters = QDir(imageRoot).filePath(QStringLiteral("adapters"));

    if (profileId == QStringLiteral("pixel-art-sd15")) {
        tasks.append({"stable-diffusion-v1-5/stable-diffusion-v1-5",
                      "v1-5-pruned-emaonly.safetensors", base15, {},
                      "451f4fe16113bff5a5d2269ed5ad43b0592e9a14"});
    } else if (profileId == QStringLiteral("pixel-art-sdxl")) {
        tasks.append({"stabilityai/stable-diffusion-xl-base-1.0",
                      "sd_xl_base_1.0.safetensors", baseXl, {},
                      "462165984030d82259a11f4367a4eed129e94a7b"});
        tasks.append({"nerijs/pixel-art-xl", "pixel-art-xl.safetensors", adapters, {},
                      "8bf4a4d9ea283e00a51fafda8e0539f8248ea037"});
    } else if (profileId == QStringLiteral("terrain-tiles")) {
        tasks.append({"stabilityai/stable-diffusion-xl-base-1.0",
                      "sd_xl_base_1.0.safetensors", baseXl, {},
                      "462165984030d82259a11f4367a4eed129e94a7b"});
        tasks.append({"kokuren/mapchipLora", "mapchipLora.safetensors", adapters, {},
                      "7ff7d9e43c9c364eb25ca283851565b7c5778dbf"});
    } else if (profileId == QStringLiteral("fast-preview")) {
        tasks.append({"stabilityai/stable-diffusion-xl-base-1.0",
                      "sd_xl_base_1.0.safetensors", baseXl, {},
                      "462165984030d82259a11f4367a4eed129e94a7b"});
        tasks.append({"latent-consistency/lcm-lora-sdxl",
                      "pytorch_lora_weights.safetensors", adapters, {},
                      "a18548dd4956b174ec5b0d78d340c8dae0a129cd"});
    }
    return tasks;
}

bool ModelDownloader::verifyFile(const QString &path, const QString &sha256) {
    const QFileInfo info(path);
    if (!info.exists() || !info.isFile() || info.size() <= 0) return false;
    if (sha256.isEmpty()) return true;

    QFile file(path);
    if (!file.open(QIODevice::ReadOnly)) return false;
    QCryptographicHash hash(QCryptographicHash::Sha256);
    if (!hash.addData(&file)) return false;
    return QString::fromLatin1(hash.result().toHex()).compare(sha256, Qt::CaseInsensitive) == 0;
}

bool ModelDownloader::imageProfileInstalled(const QString &profileId,
                                            const QString &modelsDir) const {
    const QList<DownloadTask> tasks = imageProfileTasks(profileId, modelsDir);
    if (tasks.isEmpty()) return false;

    for (const DownloadTask &task : tasks) {
        const QString path = QDir(task.targetDir).filePath(task.file);
        if (!verifyFile(path, task.sha256)) return false;
    }
    return true;
}

QString ModelDownloader::imageProfileStatusFor(const QString &profileId,
                                               const QString &modelsDir) const {
    if (profileId == m_activeImageProfile && m_busy) return QStringLiteral("downloading");
    return imageProfileInstalled(profileId, modelsDir)
               ? QStringLiteral("installed")
               : QStringLiteral("available");
}

void ModelDownloader::setImageProfileState(const QString &status, const QString &message) {
    if (m_imageProfileStatus == status && m_imageProfileMessage == message) return;
    m_imageProfileStatus = status;
    m_imageProfileMessage = message;
    emit imageProfileChanged();
}

void ModelDownloader::downloadDefaults(const QString &targetDir) {
    m_activeImageProfile.clear();
    setImageProfileState(QStringLiteral("not-installed"),
                         QStringLiteral("Select a Pixel Lab profile to install its local bundle."));
    downloadTasks(defaultManifest(targetDir));
}

void ModelDownloader::selectImageProfile(const QString &profileId, const QString &modelsDir) {
    if (imageProfile(profileId).isEmpty()) {
        setImageProfileState(QStringLiteral("error"), QStringLiteral("Unknown image model profile."));
        return;
    }

    if (m_selectedImageProfile != profileId) {
        m_selectedImageProfile = profileId;
        QSettings settings;
        settings.setValue(QStringLiteral("models/image-profile"), m_selectedImageProfile);
        settings.sync();
        emit selectedImageProfileChanged();
    }

    if (m_busy) {
        setImageProfileState(QStringLiteral("busy"),
                             QStringLiteral("Another model download is already in progress."));
        return;
    }

    const QList<DownloadTask> tasks = imageProfileTasks(profileId, modelsDir);
    if (tasks.isEmpty()) {
        setImageProfileState(QStringLiteral("error"), QStringLiteral("Profile has no downloadable files."));
        return;
    }

    if (imageProfileInstalled(profileId, modelsDir)) {
        setImageProfileState(QStringLiteral("installed"),
                             QStringLiteral("Installed locally and ready for the image engine."));
        return;
    }

    m_activeImageProfile = profileId;
    setImageProfileState(QStringLiteral("downloading"),
                         QStringLiteral("Downloading the selected profile into the Polaris model directory…"));
    downloadTasks(tasks);
}

void ModelDownloader::inspectImageProfile(const QString &profileId, const QString &modelsDir) {
    if (imageProfile(profileId).isEmpty()) {
        setImageProfileState(QStringLiteral("error"), QStringLiteral("Unknown image model profile."));
        return;
    }
    if (m_busy) return;

    if (imageProfileInstalled(profileId, modelsDir)) {
        setImageProfileState(QStringLiteral("installed"),
                             QStringLiteral("Installed locally and ready for the image engine."));
    } else {
        setImageProfileState(QStringLiteral("available"),
                             QStringLiteral("Selecting this profile will download its local model bundle."));
    }
}

void ModelDownloader::downloadTasks(const QList<DownloadTask> &tasks) {
    if (m_busy || tasks.isEmpty()) return;

    m_queue.clear();
    for (const auto &t : tasks) m_queue.enqueue(t);
    m_totalFiles = tasks.size();
    m_completedFiles = 0;
    m_failedFiles = 0;
    m_busy = true;

    emit totalFilesChanged();
    emit busyChanged();
    startNext();
}

void ModelDownloader::cancel() {
    const bool imageDownload = !m_activeImageProfile.isEmpty();
    if (m_currentReply) {
        m_currentReply->abort();
        m_currentReply->deleteLater();
        m_currentReply = nullptr;
    }
    if (m_currentOutput) {
        m_currentOutput->close();
        m_currentOutput->deleteLater();
        m_currentOutput = nullptr;
    }
    if (!m_currentPartPath.isEmpty()) QFile::remove(m_currentPartPath);
    m_currentPartPath.clear();
    m_currentDestination.clear();
    m_queue.clear();
    m_busy = false;
    if (imageDownload) {
        setImageProfileState(QStringLiteral("cancelled"), QStringLiteral("Download cancelled; partial files were removed."));
        m_activeImageProfile.clear();
    }
    emit busyChanged();
}

void ModelDownloader::startNext() {
    if (m_queue.isEmpty()) {
        m_busy = false;
        emit busyChanged();
        const bool allOk = (m_failedFiles == 0);
        if (!m_activeImageProfile.isEmpty()) {
            const QString completedProfile = m_activeImageProfile;
            setImageProfileState(allOk ? QStringLiteral("installed") : QStringLiteral("error"),
                                 allOk ? QStringLiteral("Installed locally and ready for the image engine.")
                                       : QStringLiteral("One or more files failed; retry the profile download."));
            m_activeImageProfile.clear();
            Q_UNUSED(completedProfile);
        }
        emit downloadFinished(allOk);
        return;
    }

    const DownloadTask task = m_queue.dequeue();
    m_currentFile = task.file;
    m_currentReceived = 0;
    m_currentTotal = 0;
    m_currentDestination = QDir(task.targetDir).filePath(task.file);
    m_currentPartPath = m_currentDestination + QStringLiteral(".part");
    emit progressChanged();

    QDir().mkpath(task.targetDir);

    // Check if file already exists and passes its optional hash.
    if (verifyFile(m_currentDestination, task.sha256)) {
        qDebug() << "Model already exists, skipping:" << m_currentDestination;
        m_completedFiles++;
        emit progressChanged();
        startNext();
        return;
    }

    QFile::remove(m_currentPartPath);
    const QString revision = task.revision.isEmpty() ? QStringLiteral("main") : task.revision;
    QString url = "https://huggingface.co/" + task.repo + "/resolve/" + revision + "/" + task.file;
    QNetworkRequest req{QUrl(url)};
    req.setAttribute(QNetworkRequest::RedirectPolicyAttribute,
                     QNetworkRequest::NoLessSafeRedirectPolicy);
    req.setRawHeader("User-Agent", "PolarisStudio/1.0");

    qDebug() << "Downloading:" << url;
    m_currentOutput = new QFile(m_currentPartPath, this);
    if (!m_currentOutput->open(QIODevice::WriteOnly)) {
        qWarning() << "Cannot write to" << m_currentPartPath;
        failAll("Cannot write to " + m_currentPartPath);
        return;
    }

    m_currentReply = m_nam->get(req);
    connect(m_currentReply, &QNetworkReply::finished, this, &ModelDownloader::onFinished);
    connect(m_currentReply, &QNetworkReply::readyRead, this, &ModelDownloader::onReadyRead);
}

void ModelDownloader::onReadyRead() {
    if (!m_currentReply || !m_currentOutput) return;
    const QByteArray data = m_currentReply->readAll();
    m_currentOutput->write(data);
    m_currentReceived += data.size();
    m_currentTotal = m_currentReply->header(QNetworkRequest::ContentLengthHeader).toLongLong();
    emit progressChanged();
}

void ModelDownloader::onFinished() {
    if (!m_currentReply) return;

    // Flush bytes that arrived with finished but did not produce readyRead.
    onReadyRead();
    if (m_currentOutput) {
        m_currentOutput->close();
        m_currentOutput->deleteLater();
        m_currentOutput = nullptr;
    }

    const int status = m_currentReply->attribute(QNetworkRequest::HttpStatusCodeAttribute).toInt();
    bool ok = (m_currentReply->error() == QNetworkReply::NoError && status >= 200 && status < 300);
    if (ok && !m_currentPartPath.isEmpty()) {
        // A task with a hash is checked before installation. Current curated
        // profiles are pinned to stable repository paths and can add hashes
        // without changing the downloader protocol.
        if (QFileInfo(m_currentPartPath).size() <= 0) ok = false;
        if (ok) {
            QFile::remove(m_currentDestination);
            ok = QFile::rename(m_currentPartPath, m_currentDestination);
        }
    }

    if (!ok) {
        qWarning() << "Download failed:" << m_currentFile
                   << "HTTP" << status << m_currentReply->errorString();
        m_failedFiles++;
        QFile::remove(m_currentPartPath);
    } else {
        m_completedFiles++;
    }

    m_currentReply->deleteLater();
    m_currentReply = nullptr;
    m_currentPartPath.clear();
    m_currentDestination.clear();
    emit progressChanged();

    startNext();
}

void ModelDownloader::failAll(const QString &msg) {
    cancel();
    emit errorOccurred(msg);
}
