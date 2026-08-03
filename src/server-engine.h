// server-engine.h — shared engine core for ace-server and polaris-engine
//
// Both the HTTP server (ace-server) and the IPC worker (polaris-engine) share
// the same model registry, pipeline pipelines, job queue, worker thread, and
// parameter configuration. This header declares the shared globals and
// functional surface. The implementaton lives in server-engine.cpp.

#pragma once

#include "language-id.h"
#include "model-registry.h"
#include "model-store.h"
#include "pipeline-lm.h"
#include "pipeline-synth.h"
#include "pipeline-understand.h"
#include "request.h"
#include "system-metrics.h"
#include "task-types.h"
#include "vae.h"

#include <atomic>
#include <condition_variable>
#include <cstdio>
#include <deque>
#include <functional>
#include <memory>
#include <mutex>
#include <string>
#include <thread>
#include <unordered_map>
#include <vector>

// ---------------------------------------------------------------------------
// limits
// ---------------------------------------------------------------------------
#define POLARIS_MAX_T_LATENT    15000
#define POLARIS_LATENT_CHANNELS 64
#define POLARIS_LATENT_FRAME_BYTES (POLARIS_LATENT_CHANNELS * (int)sizeof(float))

// ---------------------------------------------------------------------------
// job system
// ---------------------------------------------------------------------------
enum class EngineJobStatus : int {
    RUNNING   = 0,
    DONE      = 1,
    FAILED    = 2,
    CANCELLED = 3,
};

struct EngineJob {
    std::string                  id;
    std::atomic<EngineJobStatus> status{ EngineJobStatus::RUNNING };
    std::string                  result_body;
    std::string                  result_mime;
    std::atomic<bool>            cancel{ false };
};

extern std::mutex                                              g_mtx_jobs;
extern std::unordered_map<std::string, std::shared_ptr<EngineJob>> g_jobs;
extern std::deque<std::string>                                 g_job_order;
#define POLARIS_MAX_JOBS 32

// ---------------------------------------------------------------------------
// work queue (FIFO, single worker thread)
// ---------------------------------------------------------------------------
extern std::deque<std::function<void()>> g_work_queue;
extern std::mutex                        g_mtx_work;
extern std::condition_variable           g_cv_work;
extern bool                              g_work_stop;

void work_push(std::function<void()> fn);
void worker_main();

// ---------------------------------------------------------------------------
// engine globals
// ---------------------------------------------------------------------------
extern ModelStore *                     g_store;
extern ModelRegistry                    g_registry;
extern AceLmParams                      g_lm_params;
extern AceSynthParams                   g_synth_params;
extern AceUnderstandParams              g_und_params;
extern std::string                      g_language_model_path;
extern AceLanguageIdentifier *          g_language_identifier;
extern bool                             g_language_id_attempted;
extern bool                             g_language_use_gpu;
extern std::string                      g_loaded_lm;
extern std::string                      g_loaded_dit;
extern std::string                      g_loaded_adapter;
extern float                            g_loaded_adapter_scale;
extern std::string                      g_loaded_und_dit;
extern std::string                      g_loaded_vae;
extern int                              g_max_batch;
extern bool                             g_keep_loaded;

extern const char * MULTIPART_BOUNDARY;
extern const std::string MULTIPART_MIME;

// ---------------------------------------------------------------------------
// helpers
// ---------------------------------------------------------------------------
bool file_exists(const std::string & path);
std::string find_language_model(const char * models_dir);
std::string resolve_name(const std::vector<ModelEntry> & bucket,
                         const std::string &             requested,
                         const std::string &             loaded);
const char * job_status_str(EngineJobStatus s);
std::string job_make_id();
std::shared_ptr<EngineJob> job_create();
std::shared_ptr<EngineJob> job_find(const std::string & id);
bool server_cancel_job(void * data);
int latent_payload_validate(size_t size, int * http_code_out);
std::string multipart_build_audio_latent(const std::vector<std::string> &   audio_parts,
                                         const char *                       audio_mime,
                                         const std::vector<std::vector<float>> & latents);
std::string multipart_build_json_latent(const std::string &   json_part,
                                         const std::vector<float> & latent,
                                         int                   T_latent);
std::string engine_json_error_str(const char * msg);

// ---------------------------------------------------------------------------
// engine lifecycle (shared between both entry points)
// ---------------------------------------------------------------------------
struct EngineArgs {
    const char * models_dir   = nullptr;
    const char * adapters_dir = nullptr;
    const char * language_model = nullptr;
    bool         language_gpu = false;
    int          max_seq      = 0;      // 0 = default
    int          vae_chunk    = 0;      // 0 = default
    int          vae_overlap  = 0;      // 0 = default
    bool         keep_loaded  = false;
    int          port         = 8080;
    int          max_batch    = 1;
    bool         no_fsm       = false;
    bool         no_fa        = false;
    bool         no_batch_cfg = false;
    bool         clamp_fp16   = false;
};

// Initialize the engine: scan models, validate pipelines, create store,
// start the log capture and worker thread. Returns true on success.
// Prints diagnostic info to stderr (captured by log system).
bool engine_init(const EngineArgs & args);

// Graceful shutdown: stop the worker thread, drain jobs, free store.
void engine_shutdown();

// ---------------------------------------------------------------------------
// JSON builders (transport-agnostic; return a string the caller wraps)
// ---------------------------------------------------------------------------
std::string engine_props_json();
std::string engine_metrics_json();
