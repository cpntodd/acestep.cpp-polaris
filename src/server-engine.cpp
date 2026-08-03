// server-engine.cpp — shared engine core for ace-server and polaris-engine
//
// Extracted from ace-server.cpp. Contains all model loading, pipeline
// execution, job management, and JSON formatting — everything that is
// transport-agnostic. The HTTP and IPC entry points wrap this core.

#include "server-engine.h"
#include "synth-batch-runner.h"
#include "system-metrics.h"
#include "version.h"
#include "yyjson.h"

#include <chrono>
#include <csignal>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <random>
#include <thread>

#ifdef _WIN32
#    include <fcntl.h>
#    include <io.h>
#    ifndef STDERR_FILENO
#        define STDERR_FILENO 2
#    endif
#else
#    include <unistd.h>
#endif

// portable fd wrappers
#ifdef _WIN32
static int fd_pipe(int fd[2]) { return _pipe(fd, 4096, _O_BINARY); }
static int fd_dup(int fd) { return _dup(fd); }
static int fd_dup2(int src, int dst) { return _dup2(src, dst); }
static int fd_read(int fd, void * buf, size_t n) { return _read(fd, buf, (unsigned)n); }
static int fd_write(int fd, const void * buf, size_t n) { return _write(fd, buf, (unsigned)n); }
static void fd_close(int fd) { _close(fd); }
#else
static int fd_pipe(int fd[2]) { return pipe(fd); }
static int fd_dup(int fd) { return dup(fd); }
static int fd_dup2(int src, int dst) { return dup2(src, dst); }
static int fd_read(int fd, void * buf, size_t n) { return (int)read(fd, buf, n); }
static int fd_write(int fd, const void * buf, size_t n) { return (int)write(fd, buf, n); }
static void fd_close(int fd) { close(fd); }
#endif

// ---------------------------------------------------------------------------
// log capture (shared between both transports)
// ---------------------------------------------------------------------------
#define LOG_RING_BITS 9
#define LOG_RING_SIZE (1 << LOG_RING_BITS)
#define LOG_RING_MASK (LOG_RING_SIZE - 1)

static std::mutex              g_mtx_log;
static std::condition_variable g_cv_log;
static std::string             g_log_ring[LOG_RING_SIZE];
static uint64_t                g_log_seq = 0;

static int g_real_stderr_fd = -1;
static int g_pipe_read_fd   = -1;

static void log_setup_capture() {
    g_real_stderr_fd = fd_dup(STDERR_FILENO);
    int pipefd[2];
    if (fd_pipe(pipefd) != 0) {
        g_real_stderr_fd = -1;
        return;
    }
    g_pipe_read_fd = pipefd[0];
    fd_dup2(pipefd[1], STDERR_FILENO);
    fd_close(pipefd[1]);
}

static void log_reader_main() {
    char        buf[4096];
    std::string partial;
    for (;;) {
        int n = fd_read(g_pipe_read_fd, buf, sizeof(buf));
        if (n <= 0) break;
        fd_write(g_real_stderr_fd, buf, (size_t)n);
        partial.append(buf, (size_t)n);
        size_t pos;
        while ((pos = partial.find('\n')) != std::string::npos) {
            std::lock_guard<std::mutex> lock(g_mtx_log);
            g_log_ring[g_log_seq & LOG_RING_MASK] = partial.substr(0, pos);
            g_log_seq++;
            g_cv_log.notify_all();
            partial.erase(0, pos + 1);
        }
    }
    if (!partial.empty()) {
        std::lock_guard<std::mutex> lock(g_mtx_log);
        g_log_ring[g_log_seq & LOG_RING_MASK] = std::move(partial);
        g_log_seq++;
        g_cv_log.notify_all();
    }
    fd_close(g_pipe_read_fd);
}

static void log_teardown_capture() {
    if (g_real_stderr_fd < 0) return;
    fflush(stderr);
    fd_dup2(g_real_stderr_fd, STDERR_FILENO);
}

struct LogCapture {
    std::thread reader;
    LogCapture() {
        log_setup_capture();
        reader = std::thread(log_reader_main);
    }
    ~LogCapture() {
        log_teardown_capture();
        g_cv_log.notify_all();
        if (reader.joinable()) reader.join();
        if (g_real_stderr_fd >= 0) {
            fd_close(g_real_stderr_fd);
            g_real_stderr_fd = -1;
        }
    }
};

static std::unique_ptr<LogCapture> g_log_capture;

// ---------------------------------------------------------------------------
// globals
// ---------------------------------------------------------------------------
ModelStore *                     g_store = nullptr;
ModelRegistry                    g_registry;
std::string                      g_language_model_path;
AceLanguageIdentifier *          g_language_identifier    = nullptr;
bool                             g_language_id_attempted  = false;
bool                             g_language_use_gpu       = false;
AceLmParams                      g_lm_params;
AceSynthParams                   g_synth_params;
AceUnderstandParams              g_und_params;
std::string                      g_loaded_lm;
std::string                      g_loaded_dit;
std::string                      g_loaded_adapter;
float                            g_loaded_adapter_scale = 1.0f;
std::string                      g_loaded_und_dit;
std::string                      g_loaded_vae;
int                              g_max_batch   = 1;
bool                             g_keep_loaded = false;

// work queue
std::deque<std::function<void()>> g_work_queue;
std::mutex                        g_mtx_work;
std::condition_variable           g_cv_work;
bool                              g_work_stop = false;

// job system
std::mutex                                              g_mtx_jobs;
std::unordered_map<std::string, std::shared_ptr<EngineJob>> g_jobs;
std::deque<std::string>                                 g_job_order;

// multipart boundary
const char *        MULTIPART_BOUNDARY = "ace-batch-boundary";
const std::string   MULTIPART_MIME     = std::string("multipart/mixed; boundary=") + MULTIPART_BOUNDARY;

// ---------------------------------------------------------------------------
// helpers
// ---------------------------------------------------------------------------
bool file_exists(const std::string & path) {
    FILE * f = fopen(path.c_str(), "rb");
    if (!f) return false;
    fclose(f);
    return true;
}

std::string find_language_model(const char * models_dir) {
    if (!models_dir) return "";
    static const char * candidates[] = {
        "ggml-large-v3-turbo-q5_0.bin",
        "ggml-large-v3-turbo.bin",
        "ggml-medium.bin",
        "ggml-small.bin",
    };
    for (const char * candidate : candidates) {
        std::string path = std::string(models_dir) + "/" + candidate;
        if (file_exists(path)) return path;
    }
    return "";
}

std::string resolve_name(const std::vector<ModelEntry> & bucket,
                         const std::string &             requested,
                         const std::string &             loaded) {
    if (!requested.empty()) return requested;
    if (!loaded.empty()) return loaded;
    if (!bucket.empty()) return bucket[0].name;
    return "";
}

const char * job_status_str(EngineJobStatus s) {
    switch (s) {
        case EngineJobStatus::RUNNING:   return "running";
        case EngineJobStatus::DONE:      return "done";
        case EngineJobStatus::FAILED:    return "failed";
        case EngineJobStatus::CANCELLED: return "cancelled";
    }
    return "unknown";
}

std::string job_make_id() {
    static std::mt19937_64      rng(std::random_device{}());
    static std::mutex           mtx_rng;
    std::lock_guard<std::mutex> lock(mtx_rng);
    char buf[17];
    snprintf(buf, sizeof(buf), "%016llx", (unsigned long long)rng());
    return buf;
}

std::shared_ptr<EngineJob> job_create() {
    std::lock_guard<std::mutex> lock(g_mtx_jobs);
    auto job = std::make_shared<EngineJob>();
    job->id  = job_make_id();
    g_jobs[job->id] = job;
    g_job_order.push_back(job->id);

    while ((int)g_job_order.size() > POLARIS_MAX_JOBS) {
        bool evicted = false;
        for (auto it = g_job_order.begin(); it != g_job_order.end(); ++it) {
            auto jit = g_jobs.find(*it);
            if (jit == g_jobs.end() || jit->second->status.load() != EngineJobStatus::RUNNING) {
                if (jit != g_jobs.end()) g_jobs.erase(jit);
                g_job_order.erase(it);
                evicted = true;
                break;
            }
        }
        if (!evicted) break;
    }
    return job;
}

std::shared_ptr<EngineJob> job_find(const std::string & id) {
    std::lock_guard<std::mutex> lock(g_mtx_jobs);
    auto it = g_jobs.find(id);
    return it != g_jobs.end() ? it->second : nullptr;
}

bool server_cancel_job(void * data) {
    auto * flag = (const std::atomic<bool> *)data;
    return flag && flag->load(std::memory_order_relaxed);
}

void work_push(std::function<void()> fn) {
    std::lock_guard<std::mutex> lock(g_mtx_work);
    g_work_queue.push_back(std::move(fn));
    g_cv_work.notify_one();
}

void worker_main() {
    for (;;) {
        std::function<void()> fn;
        {
            std::unique_lock<std::mutex> lock(g_mtx_work);
            g_cv_work.wait(lock, [] { return g_work_stop || !g_work_queue.empty(); });
            if (g_work_stop) break;
            fn = std::move(g_work_queue.front());
            g_work_queue.pop_front();
        }
        fn();
    }
}

std::string engine_json_error_str(const char * msg) {
    yyjson_mut_doc * doc  = yyjson_mut_doc_new(NULL);
    yyjson_mut_val * root = yyjson_mut_obj(doc);
    yyjson_mut_doc_set_root(doc, root);
    yyjson_mut_obj_add_str(doc, root, "error", msg);
    char * json = yyjson_mut_write(doc, 0, NULL);
    yyjson_mut_doc_free(doc);
    std::string result(json);
    free(json);
    return result;
}

int latent_payload_validate(size_t size, int * http_code_out) {
    if (size == 0 || (size % POLARIS_LATENT_FRAME_BYTES) != 0) {
        if (http_code_out) *http_code_out = 400;
        return -1;
    }
    int T = (int)(size / (size_t)POLARIS_LATENT_FRAME_BYTES);
    if (T <= 0) {
        if (http_code_out) *http_code_out = 400;
        return -1;
    }
    if (T > POLARIS_MAX_T_LATENT) {
        if (http_code_out) *http_code_out = 413;
        return -1;
    }
    return T;
}

std::string multipart_build_audio_latent(const std::vector<std::string> &   audio_parts,
                                         const char *                       audio_mime,
                                         const std::vector<std::vector<float>> & latents) {
    std::string body;
    for (size_t i = 0; i < audio_parts.size(); i++) {
        if (audio_parts[i].empty()) continue;
        body += "--";
        body += MULTIPART_BOUNDARY;
        body += "\r\nContent-Type: ";
        body += audio_mime;
        body += "\r\n\r\n";
        body += audio_parts[i];
        body += "\r\n";
        body += "--";
        body += MULTIPART_BOUNDARY;
        body += "\r\nContent-Type: application/octet-stream\r\n";
        body += "Content-Disposition: form-data; name=\"latent\"\r\n\r\n";
        body.append(reinterpret_cast<const char *>(latents[i].data()), latents[i].size() * sizeof(float));
        body += "\r\n";
    }
    body += "--";
    body += MULTIPART_BOUNDARY;
    body += "--\r\n";
    return body;
}

std::string multipart_build_json_latent(const std::string &   json_part,
                                         const std::vector<float> & latent,
                                         int                   T_latent) {
    std::string body;
    body += "--";
    body += MULTIPART_BOUNDARY;
    body += "\r\nContent-Type: application/json\r\n\r\n";
    body += json_part;
    body += "\r\n";
    if (T_latent > 0 && !latent.empty()) {
        body += "--";
        body += MULTIPART_BOUNDARY;
        body += "\r\nContent-Type: application/octet-stream\r\n";
        body += "Content-Disposition: form-data; name=\"latent\"\r\n\r\n";
        body.append(reinterpret_cast<const char *>(latent.data()), (size_t)T_latent * POLARIS_LATENT_FRAME_BYTES);
        body += "\r\n";
    }
    body += "--";
    body += MULTIPART_BOUNDARY;
    body += "--\r\n";
    return body;
}

// ---------------------------------------------------------------------------
// JSON builders (the real ones — worker functions live in each entry point)
// ---------------------------------------------------------------------------
std::string engine_props_json() {
    yyjson_mut_doc * doc  = yyjson_mut_doc_new(NULL);
    yyjson_mut_val * root = yyjson_mut_obj(doc);
    yyjson_mut_doc_set_root(doc, root);
    yyjson_mut_obj_add_str(doc, root, "version", ACE_VERSION);

    auto add_names = [&](yyjson_mut_val * parent, const char * key, const std::vector<ModelEntry> & bucket) {
        yyjson_mut_val * arr = yyjson_mut_arr(doc);
        for (const auto & e : bucket) yyjson_mut_arr_add_str(doc, arr, e.name.c_str());
        yyjson_mut_obj_add_val(doc, parent, key, arr);
    };

    yyjson_mut_val * models = yyjson_mut_obj(doc);
    yyjson_mut_obj_add_val(doc, root, "models", models);
    add_names(models, "lm", g_registry.lm);
    add_names(models, "embedding", g_registry.text_enc);
    add_names(models, "dit", g_registry.dit);
    add_names(models, "vae", g_registry.vae);

    yyjson_mut_val * adapters_arr = yyjson_mut_arr(doc);
    for (const auto & e : g_registry.adapters)
        yyjson_mut_arr_add_str(doc, adapters_arr, e.name.c_str());
    yyjson_mut_obj_add_val(doc, root, "adapters", adapters_arr);

    yyjson_mut_val * cli = yyjson_mut_obj(doc);
    yyjson_mut_obj_add_val(doc, root, "cli", cli);
    yyjson_mut_obj_add_int(doc, cli, "max_batch", g_max_batch);

    AceRequest defaults;
    request_init(&defaults);
    std::string defaults_str  = request_to_json(&defaults, false);
    yyjson_doc * defaults_doc = yyjson_read(defaults_str.c_str(), defaults_str.size(), 0);
    yyjson_mut_val * defaults_copy = yyjson_val_mut_copy(doc, yyjson_doc_get_root(defaults_doc));
    yyjson_mut_obj_add_val(doc, root, "default", defaults_copy);
    yyjson_doc_free(defaults_doc);

    yyjson_mut_val * presets = yyjson_mut_obj(doc);
    yyjson_mut_obj_add_val(doc, root, "presets", presets);
    yyjson_mut_val * turbo = yyjson_mut_obj(doc);
    yyjson_mut_obj_add_int(doc, turbo, "inference_steps", 8);
    yyjson_mut_obj_add_real(doc, turbo, "guidance_scale", 1.0);
    yyjson_mut_obj_add_real(doc, turbo, "shift", 3.0);
    yyjson_mut_obj_add_val(doc, presets, "turbo", turbo);
    yyjson_mut_val * sft = yyjson_mut_obj(doc);
    yyjson_mut_obj_add_int(doc, sft, "inference_steps", 50);
    yyjson_mut_obj_add_real(doc, sft, "guidance_scale", 1.0);
    yyjson_mut_obj_add_real(doc, sft, "shift", 1.0);
    yyjson_mut_obj_add_val(doc, presets, "sft", sft);

    yyjson_write_flag flags = YYJSON_WRITE_PRETTY | YYJSON_WRITE_PRETTY_TWO_SPACES | YYJSON_WRITE_FP_TO_FIXED(2);
    char * json = yyjson_mut_write(doc, flags, NULL);
    yyjson_mut_doc_free(doc);
    std::string result(json);
    free(json);
    return result;
}

std::string engine_metrics_json() {
    const AceSystemMetrics metrics = system_metrics_sample();
    yyjson_mut_doc * doc  = yyjson_mut_doc_new(NULL);
    yyjson_mut_val * root = yyjson_mut_obj(doc);
    yyjson_mut_doc_set_root(doc, root);

    yyjson_mut_val * cpu = yyjson_mut_obj(doc);
    yyjson_mut_obj_add_bool(doc, cpu, "available", metrics.cpu_available);
    yyjson_mut_obj_add_real(doc, cpu, "usage", metrics.cpu_usage);
    yyjson_mut_obj_add_uint(doc, cpu, "cores", metrics.cpu_cores);
    yyjson_mut_obj_add_val(doc, root, "cpu", cpu);

    yyjson_mut_val * gpu = yyjson_mut_obj(doc);
    yyjson_mut_obj_add_bool(doc, gpu, "available", metrics.gpu_available);
    yyjson_mut_obj_add_bool(doc, gpu, "usage_available", metrics.gpu_usage_available);
    yyjson_mut_obj_add_real(doc, gpu, "usage", metrics.gpu_usage);
    yyjson_mut_obj_add_str(doc, gpu, "name", metrics.gpu_name.c_str());
    yyjson_mut_obj_add_str(doc, gpu, "backend", metrics.gpu_backend.c_str());
    yyjson_mut_obj_add_val(doc, root, "gpu", gpu);

    yyjson_mut_val * vram = yyjson_mut_obj(doc);
    yyjson_mut_obj_add_bool(doc, vram, "available", metrics.vram_available);
    yyjson_mut_obj_add_uint(doc, vram, "used", metrics.vram_used);
    yyjson_mut_obj_add_uint(doc, vram, "total", metrics.vram_total);
    yyjson_mut_obj_add_val(doc, root, "vram", vram);

    yyjson_mut_val * memory = yyjson_mut_obj(doc);
    yyjson_mut_obj_add_bool(doc, memory, "available", metrics.memory_available);
    yyjson_mut_obj_add_real(doc, memory, "usage", metrics.memory_usage);
    yyjson_mut_obj_add_uint(doc, memory, "used", metrics.memory_used);
    yyjson_mut_obj_add_uint(doc, memory, "total", metrics.memory_total);
    yyjson_mut_obj_add_val(doc, root, "memory", memory);

    char * json = yyjson_mut_write(doc, YYJSON_WRITE_FP_TO_FIXED(2), NULL);
    yyjson_mut_doc_free(doc);
    std::string result(json);
    free(json);
    return result;
}

// ---------------------------------------------------------------------------
// engine lifecycle
// ---------------------------------------------------------------------------
bool engine_init(const EngineArgs & args) {
    ace_lm_default_params(&g_lm_params);
    ace_synth_default_params(&g_synth_params);

    if (args.max_seq > 0)       g_lm_params.max_seq    = args.max_seq;
    if (args.vae_chunk > 0)     g_synth_params.vae_chunk = args.vae_chunk;
    if (args.vae_overlap > 0)   g_synth_params.vae_overlap = args.vae_overlap;
    g_keep_loaded = args.keep_loaded;
    g_max_batch   = args.max_batch;

    if (args.no_fsm)       g_lm_params.use_fsm       = false;
    if (args.no_fa)        { g_lm_params.use_fa = false; g_synth_params.use_fa = false; }
    if (args.no_batch_cfg) { g_lm_params.use_batch_cfg = false; g_synth_params.use_batch_cfg = false; }
    if (args.clamp_fp16)   { g_lm_params.clamp_fp16 = true; g_synth_params.clamp_fp16 = true; }

    // language
    g_language_use_gpu     = args.language_gpu;
    g_language_model_path = args.language_model ? args.language_model
                                                 : find_language_model(args.models_dir);
    if (!g_language_model_path.empty() && !file_exists(g_language_model_path)) {
        fprintf(stderr, "[Engine] WARNING: speech language model not found: %s\n",
                g_language_model_path.c_str());
        g_language_model_path.clear();
    }
    fprintf(stderr, "[Engine] Speech language ID: %s (backend=%s)\n",
            g_language_model_path.empty() ? "unavailable (unknown fallback)"
                                          : g_language_model_path.c_str(),
            g_language_use_gpu ? "Vulkan/GGML (opt-in)" : "CPU/GGML (default)");

    // log capture
    g_log_capture.reset(new LogCapture());

    // scan models
    fprintf(stderr, "[Engine] Scanning models in %s\n", args.models_dir);
    if (!registry_scan(&g_registry, args.models_dir)) {
        fprintf(stderr, "[Engine] ERROR: no GGUF models found in %s\n", args.models_dir);
        return false;
    }

    if (args.adapters_dir) {
        fprintf(stderr, "[Engine] Scanning adapters in %s\n", args.adapters_dir);
        registry_scan_adapters(&g_registry, args.adapters_dir);
    }

    bool have_lm  = !g_registry.lm.empty();
    bool have_dit = !g_registry.dit.empty();
    bool have_enc = !g_registry.text_enc.empty();
    bool have_vae = !g_registry.vae.empty();
    bool have_synth = have_dit && have_enc && have_vae;

    if (!have_synth && (have_dit || have_enc || have_vae)) {
        char missing[64]; int n = 0;
        if (!have_dit) n += snprintf(missing + n, sizeof(missing) - n, "%sDiT", n ? ", " : "");
        if (!have_enc) n += snprintf(missing + n, sizeof(missing) - n, "%sText-Enc", n ? ", " : "");
        if (!have_vae) n += snprintf(missing + n, sizeof(missing) - n, "%sVAE", n ? ", " : "");
        if (have_lm)
            fprintf(stderr, "[Engine] WARNING: /synth unavailable, missing: %s\n", missing);
        else {
            fprintf(stderr, "[Engine] ERROR: no usable pipeline, synth missing: %s\n", missing);
            return false;
        }
    }

    if (g_max_batch < 1) g_max_batch = 1;
    if (g_max_batch > 9) g_max_batch = 9;
    g_lm_params.max_batch = g_max_batch;

    ace_understand_default_params(&g_und_params);
    g_und_params.use_fa      = g_lm_params.use_fa;
    g_und_params.use_fsm     = g_lm_params.use_fsm;
    g_und_params.max_seq     = g_lm_params.max_seq;
    g_und_params.max_batch   = g_lm_params.max_batch;
    g_und_params.vae_chunk   = g_synth_params.vae_chunk;
    g_und_params.vae_overlap = g_synth_params.vae_overlap;

    g_store = store_create(g_keep_loaded ? EVICT_NEVER : EVICT_STRICT);

    fprintf(stderr, "[Engine] Polaris Studio engine %s\n", ACE_VERSION);
    fprintf(stderr, "[Engine] Pipelines:%s%s%s\n",
            have_lm ? " /lm" : "",
            have_synth ? " /synth" : "",
            have_lm && have_dit && have_vae ? " /understand" : "");
    fprintf(stderr, "[Engine] Models: %zu LM, %zu Text-Enc, %zu DiT, %zu VAE, %zu Adapter\n",
            g_registry.lm.size(), g_registry.text_enc.size(), g_registry.dit.size(),
            g_registry.vae.size(), g_registry.adapters.size());
    return true;
}

void engine_shutdown() {
    {
        std::lock_guard<std::mutex> lock(g_mtx_work);
        g_work_stop = true;
    }
    g_cv_work.notify_one();

    fprintf(stderr, "[Engine] Shutting down...\n");
    store_free(g_store);
    g_store = nullptr;
    g_log_capture.reset();
}
