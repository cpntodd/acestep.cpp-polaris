// database.h — SQLite persistence for Polaris Studio
//
// Stores songs, settings, analysis state. All DB operations are synchronous
// (called from QML timers/buttons, not from the engine worker thread).

#pragma once

#include <QObject>
#include <QSqlDatabase>
#include <QString>
#include <QDateTime>
#include <QVariantMap>

struct SongRecord {
    int         id = 0;
    QString     kind;            // "reference" | "generated"
    QString     relativePath;    // relative to the library root
    QString     hash;            // SHA-256
    qint64      size = 0;        // bytes
    double      duration = 0.0;  // seconds
    QString     language;        // "en" | "mk" | "" (unknown)
    QString     analysisState;   // "pending" | "ready" | "failed"
    QDateTime   createdAt;
};

class Database : public QObject {
    Q_OBJECT
    Q_PROPERTY(QString libraryPath READ libraryPath WRITE setLibraryPath NOTIFY libraryPathChanged)

public:
    explicit Database(QObject *parent = nullptr);
    ~Database() override;

    QString libraryPath() const { return m_libraryPath; }
    void setLibraryPath(const QString &path);

    // Song CRUD
    int  addSong(const SongRecord &rec);
    bool updateSong(const SongRecord &rec);
    bool removeSong(int id);
    SongRecord getSong(int id);
    QList<SongRecord> allSongs();

    // Settings
    void    setSetting(const QString &key, const QString &value);
    QString getSetting(const QString &key, const QString &defaultValue = {});

signals:
    void libraryPathChanged();

private:
    void createTables();

    QSqlDatabase m_db;
    QString      m_libraryPath;
};
