// engine-client.cpp — IPC client for polaris-engine Unix socket

#include "engine-client.h"
#include <QLocalSocket>
#include <QJsonDocument>
#include <QJsonObject>
#include <QDebug>

static const quint8  IPC_FRAME_JSON  = 0x01;
static const int     IPC_HEADER_SIZE = 5;  // 1 byte type + 4 byte length

EngineClient::EngineClient(QObject *parent) : QObject(parent) {}

EngineClient::~EngineClient() {
    disconnectFromEngine();
}

bool EngineClient::isConnected() const {
    return m_socket && m_socket->state() == QLocalSocket::ConnectedState;
}

void EngineClient::setSocketPath(const QString &path) {
    m_socketPath = path;
}

bool EngineClient::connectToEngine() {
    if (isConnected()) return true;
    if (m_socketPath.isEmpty()) {
        qWarning() << "EngineClient: no socket path set";
        return false;
    }

    if (m_socket) {
        m_socket->deleteLater();
    }
    m_socket = new QLocalSocket(this);
    m_socket->connectToServer(m_socketPath);

    if (!m_socket->waitForConnected(750)) {
        qWarning() << "EngineClient: connect failed:" << m_socket->errorString();
        m_socket->deleteLater();
        m_socket = nullptr;
        return false;
    }

    emit connectedChanged();
    return true;
}

void EngineClient::disconnectFromEngine() {
    if (m_socket) {
        m_socket->disconnectFromServer();
        m_socket->deleteLater();
        m_socket = nullptr;
        emit connectedChanged();
    }
}

bool EngineClient::readFrame(QByteArray &payload) {
    if (!m_socket) return false;

    auto readExact = [this](char *target, int length) {
        int offset = 0;
        while (offset < length) {
            // QLocalSocket may already have buffered both the frame header and
            // payload. Waiting before consuming buffered bytes can time out
            // even though the complete reply is available.
            if (m_socket->bytesAvailable() <= 0 && !m_socket->waitForReadyRead(5000))
                return false;
            const qint64 n = m_socket->read(target + offset, length - offset);
            if (n <= 0) return false;
            offset += static_cast<int>(n);
        }
        return true;
    };

    // Read header: 5 bytes (type + 4-byte big-endian length)
    char header[IPC_HEADER_SIZE];
    if (!readExact(header, IPC_HEADER_SIZE)) return false;

    // We only care about type for skipping — all frames are read
    quint32 len = ((quint8)header[1] << 24) | ((quint8)header[2] << 16) |
                  ((quint8)header[3] << 8)  |  (quint8)header[4];
    if (len > 128U * 1024U * 1024U) {
        qWarning() << "EngineClient: rejecting oversized IPC frame:" << len;
        return false;
    }

    // Read payload
    payload.resize(len);
    return len == 0 || readExact(payload.data(), static_cast<int>(len));
}

bool EngineClient::writeFrame(const QByteArray &payload) {
    if (!m_socket) return false;

    // Build header: type(1) + length(4, big-endian)
    QByteArray frame;
    quint32 len = (quint32)payload.size();
    frame.append((char)IPC_FRAME_JSON);
    frame.append((char)((len >> 24) & 0xFF));
    frame.append((char)((len >> 16) & 0xFF));
    frame.append((char)((len >> 8)  & 0xFF));
    frame.append((char)(len & 0xFF));
    frame.append(payload);

    qint64 written = m_socket->write(frame);
    if (written != frame.size()) return false;
    return m_socket->bytesToWrite() == 0 || m_socket->waitForBytesWritten(5000);
}

QJsonObject EngineClient::call(const QString &method, const QJsonObject &params) {
    if (!connectToEngine()) return {};

    // Build JSON-RPC request
    static int g_id = 0;
    QJsonObject req;
    req["jsonrpc"] = "2.0";
    req["method"]  = method;
    req["params"]  = params;
    req["id"]      = ++g_id;

    QByteArray reqBytes = QJsonDocument(req).toJson(QJsonDocument::Compact);
    if (!writeFrame(reqBytes)) {
        qWarning() << "EngineClient: write failed for" << method;
        disconnectFromEngine();
        return {};
    }

    // Read response frame
    QByteArray respBytes;
    if (!readFrame(respBytes)) {
        qWarning() << "EngineClient: read failed for" << method;
        disconnectFromEngine();
        return {};
    }

    QJsonParseError err;
    QJsonDocument doc = QJsonDocument::fromJson(respBytes, &err);
    if (err.error != QJsonParseError::NoError) {
        qWarning() << "EngineClient: JSON parse error:" << err.errorString();
        return {};
    }

    QJsonObject resp = doc.object();
    if (resp.contains("error")) {
        qWarning() << "EngineClient: RPC error for" << method << ":" << resp["error"];
        return {};
    }

    return resp["result"].toObject();
}
