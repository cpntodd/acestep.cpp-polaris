// engine-ipc.h — IPC frame protocol for polaris-engine
//
// polais-engine communicates with the Qt desktop app over a Unix domain
// socket using length-prefixed frames:
//
//   [1 byte frame type][4 bytes payload length (big-endian)][payload...]
//
// Frame types:
//   0x01  JSON-RPC request/response
//   0x02  binary audio chunk (PCM, WAV, or raw float — mime in metadata)
//   0x03  binary progress/telemetry chunk
//
// The Unix socket is created at $XDG_RUNTIME_DIR/polaris/engine.sock.
// The directory is created if needed; permissions are 0700.
//
// JSON-RPC 2.0 envelope:
//   Request:  {"jsonrpc":"2.0","method":"<name>","params":{...},"id":<int>}
//   Response: {"jsonrpc":"2.0","result":{...},"id":<int>}
//   Error:    {"jsonrpc":"2.0","error":{"code":<int>,"message":"..."},"id":<int>}

#pragma once

#include <cstdint>
#include <string>

#if defined(_WIN32)
#    error "IPC transport is Unix-only (polaris-engine is a Linux desktop component)"
#endif

#include <sys/socket.h>
#include <sys/stat.h>
#include <sys/un.h>
#include <unistd.h>

// socket path components
#define POLARIS_IPC_DIR     "polaris"
#define POLARIS_IPC_SOCK    "engine.sock"
#define POLARIS_IPC_XDG_VAR "XDG_RUNTIME_DIR"

// frame types
enum IpcFrameType : uint8_t {
    IPC_FRAME_JSON    = 0x01,  // JSON-RPC payload
    IPC_FRAME_AUDIO   = 0x02,  // binary audio bytes
    IPC_FRAME_PROGRESS = 0x03, // binary/JSON progress data
};

// fixed sizes
#define IPC_FRAME_TYPE_SIZE  1
#define IPC_FRAME_LEN_SIZE   4
#define IPC_FRAME_HEADER_SIZE 5  // type(1) + len(4)

// Build the runtime socket path: $XDG_RUNTIME_DIR/polaris/engine.sock.
// Returns empty string if XDG_RUNTIME_DIR is not set.
static inline std::string ipc_socket_path() {
    const char * runtime = getenv(POLARIS_IPC_XDG_VAR);
    if (!runtime || !*runtime) return "";
    return std::string(runtime) + "/" POLARIS_IPC_DIR "/" POLARIS_IPC_SOCK;
}

// Build just the directory path: $XDG_RUNTIME_DIR/polaris
static inline std::string ipc_socket_dir() {
    const char * runtime = getenv(POLARIS_IPC_XDG_VAR);
    if (!runtime || !*runtime) return "";
    return std::string(runtime) + "/" POLARIS_IPC_DIR;
}

// Pack a 32-bit unsigned integer into big-endian bytes.
static inline void ipc_pack_u32(uint8_t out[IPC_FRAME_LEN_SIZE], uint32_t len) {
    out[0] = (len >> 24) & 0xFF;
    out[1] = (len >> 16) & 0xFF;
    out[2] = (len >> 8)  & 0xFF;
    out[3] =  len        & 0xFF;
}

// Unpack a big-endian 32-bit unsigned integer from 4 bytes.
static inline uint32_t ipc_unpack_u32(const uint8_t in[IPC_FRAME_LEN_SIZE]) {
    return ((uint32_t)in[0] << 24) | ((uint32_t)in[1] << 16) |
           ((uint32_t)in[2] << 8)  |  (uint32_t)in[3];
}

// Write a full frame to a socket fd. Returns true on success.
static inline bool ipc_write_frame(int fd, IpcFrameType type, const void * payload, uint32_t len) {
    uint8_t header[IPC_FRAME_HEADER_SIZE];
    header[0] = static_cast<uint8_t>(type);
    ipc_pack_u32(header + IPC_FRAME_TYPE_SIZE, len);

    ssize_t n;
    n = write(fd, header, IPC_FRAME_HEADER_SIZE);
    if (n != IPC_FRAME_HEADER_SIZE) return false;
    if (len == 0) return true;
    n = write(fd, payload, len);
    return n == (ssize_t)len;
}

// Write a JSON string as an IPC frame. Convenience wrapper.
static inline bool ipc_write_json_frame(int fd, const std::string & json) {
    return ipc_write_frame(fd, IPC_FRAME_JSON, json.data(), (uint32_t)json.size());
}

// Write a binary audio frame.
static inline bool ipc_write_audio_frame(int fd, const void * data, uint32_t len) {
    return ipc_write_frame(fd, IPC_FRAME_AUDIO, data, len);
}

// Read one full frame header (5 bytes) from a socket fd.
// Returns true if a full header was read; sets type and len.
// Returns false on EOF or error — the caller should close the socket.
static inline bool ipc_read_frame_header(int fd, IpcFrameType & type, uint32_t & len) {
    uint8_t buf[IPC_FRAME_HEADER_SIZE];
    size_t  offset = 0;
    while (offset < IPC_FRAME_HEADER_SIZE) {
        ssize_t n = read(fd, buf + offset, IPC_FRAME_HEADER_SIZE - offset);
        if (n <= 0) return false;
        offset += (size_t)n;
    }
    type = static_cast<IpcFrameType>(buf[0]);
    len  = ipc_unpack_u32(buf + IPC_FRAME_TYPE_SIZE);
    return true;
}

// Create a listening Unix domain socket at ipc_socket_path().
// Returns fd on success, -1 on failure.
static inline int ipc_create_server_socket() {
    std::string dir = ipc_socket_dir();
    if (dir.empty()) return -1;

    // ensure the directory exists with 0700 permissions
    mkdir(dir.c_str(), 0700);

    std::string path = ipc_socket_path();
    if (path.empty()) return -1;

    // remove any stale socket file from a previous crashed instance
    unlink(path.c_str());

    int fd = socket(AF_UNIX, SOCK_STREAM, 0);
    if (fd < 0) return -1;

    struct sockaddr_un addr;
    memset(&addr, 0, sizeof(addr));
    addr.sun_family = AF_UNIX;
    snprintf(addr.sun_path, sizeof(addr.sun_path), "%s", path.c_str());

    if (bind(fd, (struct sockaddr *)&addr, sizeof(addr)) < 0) {
        close(fd);
        return -1;
    }

    if (listen(fd, 5) < 0) {
        close(fd);
        return -1;
    }

    return fd;
}

// Check a string for valid JSON-RPC framing: starts with { and has a
// recognizable structure. This is a lightweight check, not a full parser.
static inline bool ipc_is_json_rpc(const std::string & body) {
    return body.size() > 2 && body[0] == '{' &&
           body.find("\"method\"") != std::string::npos;
}
