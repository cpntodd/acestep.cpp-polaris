// library.h — file import and management for the user's music library
//
// Copies uploaded reference audio into <library>/reference/<sha256>.<ext>,
// detects duplicates by content hash, and returns SongRecords for the
// database.

#pragma once

#include <QObject>
#include <QString>
#include <QUrl>
#include "database.h"

class QMediaPlayer;

class Library : public QObject {
    Q_OBJECT
    Q_PROPERTY(QString path READ path WRITE setPath NOTIFY pathChanged)
    Q_PROPERTY(int songCount READ songCount NOTIFY songsChanged)

public:
    explicit Library(Database *db, QObject *parent = nullptr);

    QString path() const { return m_path; }
    void setPath(const QString &path);
    Q_INVOKABLE int  songCount() const;
    Q_INVOKABLE QVariantList songList() const;  // QML-friendly song list
    QList<SongRecord> songs() const;             // C++ use

    // Returns the full on-disk path for a song
    Q_INVOKABLE QString absolutePath(const SongRecord &rec) const;
    Q_INVOKABLE QString fullPath(const QString &relativePath) const {
        return m_path + "/" + relativePath;
    }

public slots:
    // Import a file from a local path or URL. Copies into library/reference/
    // with hash-based dedup. Returns the song ID, or -1 on failure.
    int importFile(const QUrl &url);

    // Remove a song and its file from the library
    bool removeSong(int id);

    // Rescan the library for new files
    void refresh();

signals:
    void pathChanged();
    void songsChanged();
    void importProgress(const QString &filePath, int percent);
    void importFinished(int songId);

private:
    QString computeSha256(const QString &filePath);
    QString extensionFromPath(const QString &path);

    Database *m_db;
    QString   m_path;
};
