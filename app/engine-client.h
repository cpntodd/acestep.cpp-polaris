// engine-client.h — IPC client for polaris-engine Unix socket
//
// Talks the same length-prefixed frame protocol defined in src/engine-ipc.h.
// Synchronous request/response for Phase 1; timers in QML drive polling.

#pragma once

#include <QObject>
#include <QJsonObject>
#include <QString>

class QLocalSocket;

class EngineClient : public QObject {
    Q_OBJECT
    Q_PROPERTY(bool connected READ isConnected NOTIFY connectedChanged)

public:
    explicit EngineClient(QObject *parent = nullptr);
    ~EngineClient() override;

    bool isConnected() const;

    void setSocketPath(const QString &path);
    QString socketPath() const { return m_socketPath; }

public slots:
    bool connectToEngine();
    void disconnectFromEngine();

    // Synchronous JSON-RPC call (blocks until response received)
    QJsonObject call(const QString &method, const QJsonObject &params = {});

signals:
    void connectedChanged();

private:
    bool readFrame(QByteArray &payload);
    bool writeFrame(const QByteArray &payload);

    QLocalSocket *m_socket = nullptr;
    QString       m_socketPath;
};
