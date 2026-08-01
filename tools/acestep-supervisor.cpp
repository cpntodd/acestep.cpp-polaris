// acestep-supervisor.cpp: small localhost process supervisor for the desktop app.
//
// The WebUI is served by ace-server on the application port. This process keeps
// a second localhost control port alive so the UI can stop and restart the
// backend even while the backend is offline.

#include "httplib.h"

#include <atomic>
#include <cerrno>
#include <csignal>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <mutex>
#include <string>
#include <thread>

#include <sys/types.h>
#include <sys/wait.h>
#include <unistd.h>

static httplib::Server * g_server = nullptr;
static pid_t             g_child  = -1;
static std::mutex        g_child_mutex;

static std::string g_server_path;
static std::string g_models_dir;
static std::string g_adapters_dir;
static int         g_server_port  = 8080;
static int         g_control_port = 8081;

static void on_signal(int) {
    if (g_server) {
        g_server->stop();
    }
}

static void cors(httplib::Response & res) {
    // The control daemon binds only to loopback. A wildcard keeps the control
    // port usable when the WebUI is opened from 127.0.0.1 or localhost.
    res.set_header("Access-Control-Allow-Origin", "*");
    res.set_header("Access-Control-Allow-Methods", "GET, POST, OPTIONS");
    res.set_header("Access-Control-Allow-Headers", "Content-Type");
}

static bool child_alive_locked() {
    if (g_child <= 0) {
        return false;
    }

    int   status = 0;
    pid_t result = waitpid(g_child, &status, WNOHANG);
    if (result == 0) {
        return true;
    }
    if (result == g_child || (result < 0 && errno == ECHILD)) {
        g_child = -1;
        return false;
    }
    return result < 0 ? false : true;
}

static bool start_child_locked() {
    if (child_alive_locked()) {
        return true;
    }

    const std::string port = std::to_string(g_server_port);
    const pid_t        pid  = fork();
    if (pid < 0) {
        return false;
    }
    if (pid == 0) {
        execl(g_server_path.c_str(), g_server_path.c_str(), "--host", "127.0.0.1", "--port", port.c_str(),
              "--models", g_models_dir.c_str(), "--adapters", g_adapters_dir.c_str(), (char *) nullptr);
        _exit(127);
    }

    g_child = pid;
    return true;
}

static void stop_child_locked() {
    if (!child_alive_locked()) {
        return;
    }

    const pid_t pid = g_child;
    kill(pid, SIGTERM);
    for (int i = 0; i < 50; ++i) {
        int   status = 0;
        pid_t result = waitpid(pid, &status, WNOHANG);
        if (result == pid || (result < 0 && errno == ECHILD)) {
            g_child = -1;
            return;
        }
        if (result < 0 && errno != EINTR) {
            break;
        }
        usleep(100000);
    }

    // A crashed or wedged model process must still be recoverable from the
    // desktop switch. Give graceful shutdown five seconds, then force it.
    kill(pid, SIGKILL);
    waitpid(pid, nullptr, 0);
    g_child = -1;
}

static void status_response(httplib::Response & res) {
    cors(res);
    std::lock_guard<std::mutex> lock(g_child_mutex);
    const bool alive = child_alive_locked();
    std::string body = "{\"state\":\"";
    body += alive ? "on" : "off";
    body += "\",\"pid\":";
    body += std::to_string(alive ? static_cast<long long>(g_child) : 0);
    body += ",\"server_port\":";
    body += std::to_string(g_server_port);
    body += "}";
    res.set_content(body, "application/json");
}

static void usage(const char * prog) {
    fprintf(stderr,
            "Usage: %s --server PATH --models DIR --adapters DIR [--server-port N] [--control-port N]\n",
            prog);
}

int main(int argc, char ** argv) {
    for (int i = 1; i < argc; ++i) {
        if (!strcmp(argv[i], "--server") && i + 1 < argc) {
            g_server_path = argv[++i];
        } else if (!strcmp(argv[i], "--models") && i + 1 < argc) {
            g_models_dir = argv[++i];
        } else if (!strcmp(argv[i], "--adapters") && i + 1 < argc) {
            g_adapters_dir = argv[++i];
        } else if (!strcmp(argv[i], "--server-port") && i + 1 < argc) {
            g_server_port = atoi(argv[++i]);
        } else if (!strcmp(argv[i], "--control-port") && i + 1 < argc) {
            g_control_port = atoi(argv[++i]);
        } else if (!strcmp(argv[i], "--help") || !strcmp(argv[i], "-h")) {
            usage(argv[0]);
            return 0;
        } else {
            fprintf(stderr, "Unknown option: %s\n", argv[i]);
            usage(argv[0]);
            return 1;
        }
    }

    if (g_server_path.empty() || g_models_dir.empty() || g_adapters_dir.empty()) {
        fprintf(stderr, "[Supervisor] --server, --models and --adapters are required\n");
        usage(argv[0]);
        return 1;
    }

    httplib::Server server;
    g_server = &server;
    signal(SIGINT, on_signal);
    signal(SIGTERM, on_signal);

    server.Options(".*", [](const httplib::Request &, httplib::Response & res) {
        cors(res);
        res.status = 204;
    });
    server.Get("/status", [](const httplib::Request &, httplib::Response & res) { status_response(res); });
    server.Get("/health", [](const httplib::Request &, httplib::Response & res) { status_response(res); });
    server.Post("/start", [](const httplib::Request &, httplib::Response & res) {
        cors(res);
        std::lock_guard<std::mutex> lock(g_child_mutex);
        if (!start_child_locked()) {
            res.status = 500;
            res.set_content("{\"error\":\"Unable to start ace-server\"}", "application/json");
            return;
        }
        res.set_content("{\"state\":\"starting\"}", "application/json");
    });
    server.Post("/stop", [](const httplib::Request &, httplib::Response & res) {
        cors(res);
        std::lock_guard<std::mutex> lock(g_child_mutex);
        stop_child_locked();
        res.set_content("{\"state\":\"off\"}", "application/json");
    });

    {
        std::lock_guard<std::mutex> lock(g_child_mutex);
        if (!start_child_locked()) {
            fprintf(stderr, "[Supervisor] failed to start ace-server\n");
            return 1;
        }
    }

    fprintf(stderr, "[Supervisor] control listening on 127.0.0.1:%d\n", g_control_port);
    server.listen("127.0.0.1", g_control_port);

    {
        std::lock_guard<std::mutex> lock(g_child_mutex);
        stop_child_locked();
    }
    g_server = nullptr;
    return 0;
}
