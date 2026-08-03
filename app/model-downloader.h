// model-downloader.h — Hugging Face model download manager
//
// Downloads the default GGUF model set from Hugging Face into a target
// directory. Uses QNetworkAccessManager for HTTP with progress signals.
// Designed to be driven from QML (context property).

#pragma once

#include <QObject>
#include <QNetworkAccessManager>
#include <QNetworkReply>
#include <QQueue>
#include <QString>
#include <QStringList>
#include <QVariantList>
#include <QVariantMap>
#include <QFile>

struct DownloadTask {
    QString repo;       // e.g. "Serveurperso/ACE-Step-1.5-GGUF"
    QString file;       // e.g. "vae-BF16.gguf"
    QString targetDir;  // where to save the file
    QString sha256;     // optional content hash; empty means source revision is trusted
    QString revision;   // optional Hugging Face commit; empty means main
};

class ModelDownloader : public QObject {
    Q_OBJECT
    Q_PROPERTY(bool busy READ isBusy NOTIFY busyChanged)
    Q_PROPERTY(int totalFiles READ totalFiles NOTIFY totalFilesChanged)
    Q_PROPERTY(int completedFiles READ completedFiles NOTIFY progressChanged)
    Q_PROPERTY(QString currentFile READ currentFile NOTIFY progressChanged)
    Q_PROPERTY(qint64 currentReceived READ currentReceived NOTIFY progressChanged)
    Q_PROPERTY(qint64 currentTotal READ currentTotal NOTIFY progressChanged)
    Q_PROPERTY(QStringList localModelFiles READ localModelFiles NOTIFY localModelsChanged)
    Q_PROPERTY(QVariantList imageProfiles READ imageProfiles CONSTANT)
    Q_PROPERTY(QString selectedImageProfile READ selectedImageProfile NOTIFY selectedImageProfileChanged)
    Q_PROPERTY(QString imageProfileStatus READ imageProfileStatus NOTIFY imageProfileChanged)
    Q_PROPERTY(QString imageProfileMessage READ imageProfileMessage NOTIFY imageProfileChanged)

public:
    explicit ModelDownloader(QObject *parent = nullptr);

    bool isBusy() const { return m_busy; }
    int  totalFiles() const { return m_totalFiles; }
    int  completedFiles() const { return m_completedFiles; }
    QString currentFile() const { return m_currentFile; }
    qint64 currentReceived() const { return m_currentReceived; }
    qint64 currentTotal() const { return m_currentTotal; }
    QStringList localModelFiles() const { return m_localModelFiles; }
    QVariantList imageProfiles() const;
    QString selectedImageProfile() const { return m_selectedImageProfile; }
    QString imageProfileStatus() const { return m_imageProfileStatus; }
    QString imageProfileMessage() const { return m_imageProfileMessage; }

    // Returns the default model manifest as (repo, file, description)
    static QList<DownloadTask> defaultManifest(const QString &targetDir);

public slots:
    // Start downloading the default manifest into targetDir
    void downloadDefaults(const QString &targetDir);

    // Download a specific set of tasks
    void downloadTasks(const QList<DownloadTask> &tasks);

    // Selecting an image profile installs its curated model bundle on demand.
    Q_INVOKABLE void selectImageProfile(const QString &profileId, const QString &modelsDir);
    Q_INVOKABLE void inspectImageProfile(const QString &profileId, const QString &modelsDir);
    Q_INVOKABLE bool imageProfileInstalled(const QString &profileId, const QString &modelsDir) const;
    Q_INVOKABLE QString imageProfileStatusFor(const QString &profileId, const QString &modelsDir) const;
    Q_INVOKABLE QString imageProfileInstallPath(const QString &profileId, const QString &modelsDir) const;
    Q_INVOKABLE void scanLocalModels(const QString &modelsDir);

    // Cancel current downloads
    void cancel();

signals:
    void busyChanged();
    void totalFilesChanged();
    void progressChanged();
    void downloadFinished(bool allSucceeded);
    void errorOccurred(const QString &message);
    void selectedImageProfileChanged();
    void imageProfileChanged();
    void localModelsChanged();

private slots:
    void onFinished();
    void onReadyRead();

private:
    void startNext();
    void failAll(const QString &msg);
    QList<DownloadTask> imageProfileTasks(const QString &profileId, const QString &modelsDir) const;
    QVariantMap imageProfile(const QString &profileId) const;
    void setImageProfileState(const QString &status, const QString &message);
    static bool verifyFile(const QString &path, const QString &sha256);

    QNetworkAccessManager *m_nam;
    QQueue<DownloadTask>    m_queue;
    QNetworkReply          *m_currentReply = nullptr;
    QFile                  *m_currentOutput = nullptr;
    bool                    m_busy = false;
    int                     m_totalFiles = 0;
    int                     m_completedFiles = 0;
    int                     m_failedFiles = 0;
    QString                 m_currentFile;
    qint64                  m_currentReceived = 0;
    qint64                  m_currentTotal = 0;
    QString                 m_currentDestination;
    QString                 m_currentPartPath;
    QString                 m_activeImageProfile;
    QString                 m_selectedImageProfile;
    QStringList             m_localModelFiles;
    QString                 m_imageProfileStatus = "not-installed";
    QString                 m_imageProfileMessage = "Select a profile to install its local model bundle.";
};
