// library.cpp — file import and management implementation

#include "library.h"
#include <QCryptographicHash>
#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QDebug>
#include <QUrl>

Library::Library(Database *db, QObject *parent)
    : QObject(parent), m_db(db) {
    // Default library path: ~/Music/PolarisStudio
    m_path = m_db->getSetting("library_path");
    if (m_path.isEmpty()) {
        m_path = QDir::homePath() + "/Music/PolarisStudio";
        m_db->setSetting("library_path", m_path);
    }
    QDir().mkpath(m_path + "/reference");
    QDir().mkpath(m_path + "/generated");
}

void Library::setPath(const QString &path) {
    if (m_path != path) {
        m_path = path;
        QDir().mkpath(m_path + "/reference");
        QDir().mkpath(m_path + "/generated");
        m_db->setSetting("library_path", m_path);
        emit pathChanged();
    }
}

int Library::songCount() const {
    return m_db->allSongs().size();
}

QList<SongRecord> Library::songs() const {
    return m_db->allSongs();
}

QVariantList Library::songList() const {
    QVariantList list;
    for (const auto &s : m_db->allSongs()) {
        QVariantMap m;
        m["id"]            = s.id;
        m["kind"]          = s.kind;
        m["relativePath"]  = s.relativePath;
        m["hash"]          = s.hash;
        m["size"]          = s.size;
        m["duration"]      = s.duration;
        m["language"]      = s.language;
        m["analysisState"] = s.analysisState;
        m["createdAt"]     = s.createdAt;
        list.append(m);
    }
    return list;
}

QString Library::absolutePath(const SongRecord &rec) const {
    return m_path + "/" + rec.relativePath;
}

QString Library::computeSha256(const QString &filePath) {
    QFile file(filePath);
    if (!file.open(QIODevice::ReadOnly)) return {};

    QCryptographicHash hasher(QCryptographicHash::Sha256);
    const qint64 chunkSize = 1024 * 1024;  // 1 MB chunks
    while (!file.atEnd()) {
        hasher.addData(file.read(chunkSize));
    }
    return hasher.result().toHex();
}

QString Library::extensionFromPath(const QString &path) {
    QFileInfo fi(path);
    QString ext = fi.suffix().toLower();
    if (ext.isEmpty()) ext = "wav";
    return ext;
}

int Library::importFile(const QUrl &url) {
    QString sourcePath = url.isLocalFile() ? url.toLocalFile() : url.path();
    QFileInfo fi(sourcePath);

    if (!fi.exists()) {
        qWarning() << "Library::importFile: file not found:" << sourcePath;
        return -1;
    }

    emit importProgress(fi.fileName(), 10);

    // Compute hash
    QString hash = computeSha256(sourcePath);
    if (hash.isEmpty()) {
        qWarning() << "Library::importFile: hash failed for" << sourcePath;
        return -1;
    }

    emit importProgress(fi.fileName(), 40);

    // Check for duplicate by hash
    for (const auto &s : m_db->allSongs()) {
        if (s.hash == hash) {
            qDebug() << "Library::importFile: duplicate detected, returning existing song" << s.id;
            emit importFinished(s.id);
            return s.id;
        }
    }

    // Copy into library
    QString ext = extensionFromPath(sourcePath);
    QString relPath = "reference/" + hash + "." + ext;
    QString destPath = m_path + "/" + relPath;

    emit importProgress(fi.fileName(), 60);

    if (!QFile::copy(sourcePath, destPath)) {
        qWarning() << "Library::importFile: copy failed to" << destPath;
        return -1;
    }

    emit importProgress(fi.fileName(), 90);

    // Create database record
    SongRecord rec;
    rec.kind         = "reference";
    rec.relativePath = relPath;
    rec.hash         = hash;
    rec.size         = fi.size();
    rec.duration     = 0;  // set later by analysis
    rec.language     = "";
    rec.analysisState = "pending";

    int songId = m_db->addSong(rec);
    if (songId < 0) {
        QFile::remove(destPath);
        return -1;
    }

    emit importProgress(fi.fileName(), 100);
    emit songsChanged();
    emit importFinished(songId);
    return songId;
}

bool Library::removeSong(int id) {
    SongRecord rec = m_db->getSong(id);
    if (rec.id == 0) return false;

    // Remove file from disk
    QString fullPath = absolutePath(rec);
    QFile::remove(fullPath);

    // Remove from database
    bool ok = m_db->removeSong(id);
    if (ok) emit songsChanged();
    return ok;
}

void Library::refresh() {
    emit songsChanged();
}
