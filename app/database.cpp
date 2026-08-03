// database.cpp — SQLite persistence implementation

#include "database.h"
#include <QCoreApplication>
#include <QDir>
#include <QSqlError>
#include <QSqlQuery>
#include <QStandardPaths>
#include <QDebug>

Database::Database(QObject *parent) : QObject(parent) {
    QString dbPath = QStandardPaths::writableLocation(QStandardPaths::AppDataLocation)
                     + "/polaris.db";
    QDir().mkpath(QStandardPaths::writableLocation(QStandardPaths::AppDataLocation));

    m_db = QSqlDatabase::addDatabase("QSQLITE", "polaris_main");
    m_db.setDatabaseName(dbPath);

    if (!m_db.open()) {
        qWarning() << "Database open failed:" << m_db.lastError().text();
        return;
    }
    createTables();
}

Database::~Database() {
    if (m_db.isOpen()) m_db.close();
}

void Database::setLibraryPath(const QString &path) {
    if (m_libraryPath != path) {
        m_libraryPath = path;
        setSetting("library_path", path);
        emit libraryPathChanged();
    }
}

void Database::createTables() {
    QSqlQuery q(m_db);
    q.exec("PRAGMA journal_mode=WAL");
    q.exec("PRAGMA foreign_keys=ON");

    q.exec(R"(
        CREATE TABLE IF NOT EXISTS songs (
            id INTEGER PRIMARY KEY AUTOINCREMENT,
            kind TEXT NOT NULL,
            relative_path TEXT NOT NULL,
            hash TEXT,
            size INTEGER DEFAULT 0,
            duration REAL DEFAULT 0.0,
            language TEXT DEFAULT '',
            analysis_state TEXT DEFAULT 'pending',
            created_at TEXT NOT NULL DEFAULT (datetime('now'))
        )
    )");

    q.exec(R"(
        CREATE TABLE IF NOT EXISTS settings (
            key TEXT PRIMARY KEY,
            value TEXT
        )
    )");

    // Load library path from settings
    m_libraryPath = getSetting("library_path");
}

int Database::addSong(const SongRecord &rec) {
    QSqlQuery q(m_db);
    q.prepare(R"(
        INSERT INTO songs (kind, relative_path, hash, size, duration, language, analysis_state)
        VALUES (:kind, :path, :hash, :size, :dur, :lang, :state)
    )");
    q.bindValue(":kind",  rec.kind);
    q.bindValue(":path",  rec.relativePath);
    q.bindValue(":hash",  rec.hash);
    q.bindValue(":size",  rec.size);
    q.bindValue(":dur",   rec.duration);
    q.bindValue(":lang",  rec.language);
    q.bindValue(":state", rec.analysisState);

    if (!q.exec()) {
        qWarning() << "addSong failed:" << q.lastError().text();
        return -1;
    }
    return q.lastInsertId().toInt();
}

bool Database::updateSong(const SongRecord &rec) {
    QSqlQuery q(m_db);
    q.prepare(R"(
        UPDATE songs SET kind=:kind, relative_path=:path, hash=:hash, size=:size,
            duration=:dur, language=:lang, analysis_state=:state
        WHERE id=:id
    )");
    q.bindValue(":kind",  rec.kind);
    q.bindValue(":path",  rec.relativePath);
    q.bindValue(":hash",  rec.hash);
    q.bindValue(":size",  rec.size);
    q.bindValue(":dur",   rec.duration);
    q.bindValue(":lang",  rec.language);
    q.bindValue(":state", rec.analysisState);
    q.bindValue(":id",    rec.id);

    if (!q.exec()) {
        qWarning() << "updateSong failed:" << q.lastError().text();
        return false;
    }
    return true;
}

bool Database::removeSong(int id) {
    QSqlQuery q(m_db);
    q.prepare("DELETE FROM songs WHERE id = :id");
    q.bindValue(":id", id);
    return q.exec();
}

SongRecord Database::getSong(int id) {
    SongRecord rec;
    QSqlQuery q(m_db);
    q.prepare("SELECT * FROM songs WHERE id = :id");
    q.bindValue(":id", id);
    if (!q.exec() || !q.next()) return rec;

    rec.id            = q.value("id").toInt();
    rec.kind          = q.value("kind").toString();
    rec.relativePath  = q.value("relative_path").toString();
    rec.hash          = q.value("hash").toString();
    rec.size          = q.value("size").toLongLong();
    rec.duration      = q.value("duration").toDouble();
    rec.language      = q.value("language").toString();
    rec.analysisState = q.value("analysis_state").toString();
    rec.createdAt     = q.value("created_at").toDateTime();
    return rec;
}

QList<SongRecord> Database::allSongs() {
    QList<SongRecord> results;
    QSqlQuery q(m_db);
    q.exec("SELECT * FROM songs ORDER BY created_at DESC");
    while (q.next()) {
        SongRecord rec;
        rec.id            = q.value("id").toInt();
        rec.kind          = q.value("kind").toString();
        rec.relativePath  = q.value("relative_path").toString();
        rec.hash          = q.value("hash").toString();
        rec.size          = q.value("size").toLongLong();
        rec.duration      = q.value("duration").toDouble();
        rec.language      = q.value("language").toString();
        rec.analysisState = q.value("analysis_state").toString();
        rec.createdAt     = q.value("created_at").toDateTime();
        results.append(rec);
    }
    return results;
}

void Database::setSetting(const QString &key, const QString &value) {
    QSqlQuery q(m_db);
    q.prepare("INSERT OR REPLACE INTO settings (key, value) VALUES (:key, :value)");
    q.bindValue(":key", key);
    q.bindValue(":value", value);
    q.exec();
}

QString Database::getSetting(const QString &key, const QString &defaultValue) {
    QSqlQuery q(m_db);
    q.prepare("SELECT value FROM settings WHERE key = :key");
    q.bindValue(":key", key);
    if (q.exec() && q.next()) return q.value("value").toString();
    return defaultValue;
}
