// polaris-engine.cpp — IPC worker for Polaris Studio
//
// This is the engine worker subprocess spawned by the Qt desktop app.
// It listens on a Unix domain socket (JSON-RPC 2.0 over length-prefixed
// frames) and runs the same model pipelines as ace-server, but without
// any HTTP transport. The shared engine core lives in server-engine.*.
//
// Phase 0: core endpoints (props, metrics, health, shutdown, job-based
// compute: lm, synth, understand, vae). Streaming binary audio uses
// IPC_FRAME_AUDIO frames.

#include "engine-ipc.h"
#include "server-engine.h"
#include "audio-io.h"
#include "synth-batch-runner.h"
#include "version.h"

#include <atomic>
#include <cerrno>
#include <csignal>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <thread>

#include <sys/un.h>
#include <unistd.h>

// global server fd for the signal handler
static int g_server_fd = -1;

static void on_signal(int) {
    if (g_server_fd >= 0) {
        close(g_server_fd);
        g_server_fd = -1;
    }
}

static void usage(const char * prog) {
    fprintf(stderr, "polaris-engine %s\n\n", ACE_VERSION);
    fprintf(stderr,
            "Usage: %s --models <dir> --adapters <dir> [options]\n"
            "\n"
            "Required:\n"
            "  --models <dir>          Directory of GGUF model files\n"
            "  --adapters <dir>        Directory of adapters\n"
            "\n"
            "Optional:\n"
            "  --socket <path>         Unix socket path (default: $XDG_RUNTIME_DIR/polaris/engine.sock)\n"
            "  --port <N>              Port for optional dev HTTP mode (0 = disabled, default: 0)\n"
            "  --language-model <f>    Whisper model for language ID\n"
            "  --language-gpu          Run language listener on Vulkan\n"
            "  --keep-loaded           Keep models in VRAM between requests\n"
            "  --max-batch <N>         LM batch limit (default: 1)\n"
            "  --max-seq <N>           KV cache size\n"
            "  --vae-chunk <N>         Latent frames per tile\n"
            "  --vae-overlap <N>       Overlap frames per side\n"
            "  --no-fsm                Disable FSM constrained decoding\n"
            "  --no-fa                 Disable flash attention\n"
            "  --no-batch-cfg          Split CFG into two separate forwards\n"
            "  --clamp-fp16            Clamp hidden states to FP16 range\n"
            "  --help, -h              Show this help\n",
            prog);
}

// Forward a JSON-RPC request to the appropriate engine function and send
// the response back over the socket. For long-running compute, we create
// a job and return the job ID; the client polls for results.
static void dispatch(int client_fd, const std::string & method,
                     const std::string & params_json, int64_t id) {
    // Parse JSON-RPC request
    yyjson_doc * doc  = yyjson_read(params_json.c_str(), params_json.size(), 0);
    yyjson_val * root = doc ? yyjson_doc_get_root(doc) : nullptr;

    auto send_result = [&](const std::string & json) {
        char buf[4096];
        snprintf(buf, sizeof(buf),
                 "{\"jsonrpc\":\"2.0\",\"result\":%s,\"id\":%lld}\n",
                 json.c_str(), (long long)id);
        ipc_write_json_frame(client_fd, buf);
    };
    auto send_error = [&](int code, const char * msg) {
        char buf[4096];
        snprintf(buf, sizeof(buf),
                 "{\"jsonrpc\":\"2.0\",\"error\":{\"code\":%d,\"message\":\"%s\"},\"id\":%lld}\n",
                 code, msg, (long long)id);
        ipc_write_json_frame(client_fd, buf);
    };
    auto send_ok = [&]() {
        char buf[256];
        snprintf(buf, sizeof(buf),
                 "{\"jsonrpc\":\"2.0\",\"result\":\"ok\",\"id\":%lld}\n",
                 (long long)id);
        ipc_write_json_frame(client_fd, buf);
    };
    auto send_job_id = [&](const std::string & job_id) {
        char buf[256];
        snprintf(buf, sizeof(buf),
                 "{\"jsonrpc\":\"2.0\",\"result\":{\"id\":\"%s\"},\"id\":%lld}\n",
                 job_id.c_str(), (long long)id);
        ipc_write_json_frame(client_fd, buf);
    };
    auto send_numeric = [&](int val) {
        char buf[128];
        snprintf(buf, sizeof(buf),
                 "{\"jsonrpc\":\"2.0\",\"result\":%d,\"id\":%lld}\n",
                 val, (long long)id);
        ipc_write_json_frame(client_fd, buf);
    };

    if (!doc || !root) {
        send_error(-32700, "Parse error");
        if (doc) yyjson_doc_free(doc);
        return;
    }

    if (method == "props") {
        send_result(engine_props_json());
    } else if (method == "metrics") {
        send_result(engine_metrics_json());
    } else if (method == "health") {
        send_ok();
    } else if (method == "shutdown") {
        send_ok();
        if (g_server_fd >= 0) {
            close(g_server_fd);
            g_server_fd = -1;
        }
    } else if (method == "job_status") {
        const char * job_id = yyjson_get_str(yyjson_obj_get(root, "id"));
        if (!job_id) { send_error(-32602, "Missing job id"); yyjson_doc_free(doc); return; }
        auto job = job_find(job_id);
        if (!job) { send_error(-32602, "Job not found"); yyjson_doc_free(doc); return; }
        std::string body = "{\"status\":\"";
        body += job_status_str(job->status.load());
        body += "\"}";
        send_result(body);
    } else if (method == "job_result") {
        const char * job_id = yyjson_get_str(yyjson_obj_get(root, "id"));
        if (!job_id) { send_error(-32602, "Missing job id"); yyjson_doc_free(doc); return; }
        auto job = job_find(job_id);
        if (!job) { send_error(-32602, "Job not found"); yyjson_doc_free(doc); return; }
        if (job->status.load() != EngineJobStatus::DONE) {
            send_error(-32602, "Result not ready");
            yyjson_doc_free(doc);
            return;
        }
        // Send result body as an IPC JSON frame (text payload — could be
        // application/json or multipart). For audio results the client reads
        // the mime from a separate metadata field. Phase 0 keeps it simple:
        // result body goes as JSON frame with MIME in the envelope.
        char buf[4096];
        snprintf(buf, sizeof(buf),
                 "{\"jsonrpc\":\"2.0\",\"result\":{\"mime\":\"%s\",\"body\":\"__binary__\"},\"id\":%lld}\n",
                 job->result_mime.c_str(), (long long)id);
        ipc_write_json_frame(client_fd, buf);
        // Then send the raw result body as a binary frame
        ipc_write_frame(client_fd, IPC_FRAME_AUDIO,
                        job->result_body.data(), (uint32_t)job->result_body.size());
    } else if (method == "job_cancel") {
        const char * job_id = yyjson_get_str(yyjson_obj_get(root, "id"));
        if (!job_id) { send_error(-32602, "Missing job id"); yyjson_doc_free(doc); return; }
        auto job = job_find(job_id);
        if (!job) { send_error(-32602, "Job not found"); yyjson_doc_free(doc); return; }
        EngineJobStatus status = job->status.load();
        if (status == EngineJobStatus::RUNNING) {
            job->cancel.store(true);
            fprintf(stderr, "[IPC] Cancel requested for job %s\n", job->id.c_str());
            status = EngineJobStatus::CANCELLED;
        }
        char buf[256];
        snprintf(buf, sizeof(buf),
                 "{\"jsonrpc\":\"2.0\",\"result\":{\"status\":\"%s\"},\"id\":%lld}\n",
                 job_status_str(status), (long long)id);
        ipc_write_json_frame(client_fd, buf);
    } else if (method == "lm") {
        AceRequest ace_req;
        if (!request_parse_json(&ace_req, params_json.c_str())) {
            send_error(-32602, "Invalid request params");
            yyjson_doc_free(doc);
            return;
        }
        if (ace_req.caption.empty()) { send_error(-32602, "Caption is required"); yyjson_doc_free(doc); return; }

        int mode = LM_MODE_GENERATE;
        if (ace_req.lm_mode == LM_MODE_NAME_INSPIRE) mode = LM_MODE_INSPIRE;
        else if (ace_req.lm_mode == LM_MODE_NAME_FORMAT) mode = LM_MODE_FORMAT;

        int lm_batch_size = ace_req.lm_batch_size;
        if (lm_batch_size < 1) lm_batch_size = 1;
        if (lm_batch_size > g_max_batch) lm_batch_size = g_max_batch;
        request_resolve_lm_seed(&ace_req);

        auto job = job_create();
        fprintf(stderr, "[IPC] Job %s created (LM, mode=%d)\n", job->id.c_str(), mode);

        // Inline LM worker (Phase 0 — shared workers come later)
        work_push([job, ace_req, lm_batch_size, mode]() {
            if (job->cancel.load()) { job->status.store(EngineJobStatus::CANCELLED); return; }
            std::string        lm_name = resolve_name(g_registry.lm, ace_req.lm_model, g_loaded_lm);
            const ModelEntry * entry   = registry_find(g_registry.lm, lm_name.c_str());
            if (!entry) {
                fprintf(stderr, "[IPC] LM not found: %s\n", lm_name.c_str());
                job->status.store(EngineJobStatus::FAILED); return;
            }
            AceLmParams p = g_lm_params;
            p.model_path  = entry->path.c_str();
            AceLm * ctx = ace_lm_load(g_store, &p);
            if (!ctx) {
                fprintf(stderr, "[IPC] LM load failed\n");
                job->status.store(EngineJobStatus::FAILED); return;
            }
            std::vector<AceRequest> out(lm_batch_size);
            int rc = ace_lm_generate(ctx, &ace_req, lm_batch_size, out.data(),
                                     NULL, NULL, server_cancel_job, (void*)&job->cancel, mode);
            ace_lm_free(ctx);
            if (rc != 0) {
                job->status.store(job->cancel.load() ? EngineJobStatus::CANCELLED : EngineJobStatus::FAILED);
                return;
            }
            if (g_keep_loaded) g_loaded_lm = lm_name; else g_loaded_lm.clear();
            std::string body = "[";
            for (int i = 0; i < lm_batch_size; i++) {
                if (i > 0) body += ",";
                body += request_to_json(&out[i]);
            }
            body += "]";
            job->result_body = std::move(body);
            job->result_mime = "application/json";
            job->status.store(EngineJobStatus::DONE);
            fprintf(stderr, "[IPC] Job %s done (LM, %d results)\n", job->id.c_str(), lm_batch_size);
        });
        send_job_id(job->id);
    } else if (method == "synth") {
        AceRequest ace_req;
        if (!request_parse_json(&ace_req, params_json.c_str())) {
            send_error(-32602, "Invalid request params"); yyjson_doc_free(doc); return;
        }

        auto job = job_create();
        fprintf(stderr, "[IPC] Job %s created (synth)\n", job->id.c_str());

        work_push([job, ace_req]() mutable {
            if (job->cancel.load()) { job->status.store(EngineJobStatus::CANCELLED); return; }

            // Resolve models
            std::string dit_name = resolve_name(g_registry.dit, ace_req.synth_model, g_loaded_dit);
            std::string vae_name = resolve_name(g_registry.vae, ace_req.vae, g_loaded_vae);
            const ModelEntry * dit = registry_find(g_registry.dit, dit_name.c_str());
            const ModelEntry * vae = registry_find(g_registry.vae, vae_name.c_str());
            if (!dit || !vae || g_registry.text_enc.empty()) {
                fprintf(stderr, "[IPC] Synth: model not found\n");
                job->status.store(EngineJobStatus::FAILED); return;
            }

            AceSynthParams p = g_synth_params;
            p.text_encoder_path = g_registry.text_enc[0].path.c_str();
            p.dit_path = dit->path.c_str();
            p.vae_path = vae->path.c_str();

            AceSynth * ctx = ace_synth_load(g_store, &p);
            if (!ctx) { job->status.store(EngineJobStatus::FAILED); return; }

            request_resolve_seed(&ace_req);
            std::vector<std::vector<AceRequest>> groups = { { ace_req } };
            AceAudio audio;
            std::vector<std::vector<float>> captured_latents;
            int rc = synth_batch_run(ctx, groups, nullptr, 0, nullptr, 0, nullptr, 0, nullptr, 0,
                                     &audio, &captured_latents, server_cancel_job, (void*)&job->cancel);
            ace_synth_free(ctx);

            if (rc != 0 || !audio.samples) {
                if (audio.samples) ace_audio_free(&audio);
                job->status.store(job->cancel.load() ? EngineJobStatus::CANCELLED : EngineJobStatus::FAILED);
                return;
            }

            if (g_keep_loaded) { g_loaded_dit = dit_name; g_loaded_vae = vae_name; }
            else { g_loaded_dit.clear(); g_loaded_vae.clear(); }

            audio_normalize(audio.samples, audio.n_samples * 2, 0);
            std::string wav = audio_encode_wav(audio.samples, audio.n_samples, 48000, WAV_S16);
            ace_audio_free(&audio);

            job->result_body = std::move(wav);
            job->result_mime = "audio/wav";
            job->status.store(EngineJobStatus::DONE);
            fprintf(stderr, "[IPC] Job %s done (synth)\n", job->id.c_str());
        });
        send_job_id(job->id);
    } else if (method == "understand") {
        AceRequest ace_req;
        if (!request_parse_json(&ace_req, params_json.c_str())) {
            send_error(-32602, "Invalid request params"); yyjson_doc_free(doc); return;
        }

        auto job = job_create();
        fprintf(stderr, "[IPC] Job %s created (understand)\n", job->id.c_str());

        work_push([job, ace_req]() mutable {
            if (job->cancel.load()) { job->status.store(EngineJobStatus::CANCELLED); return; }

            std::string lm_name  = resolve_name(g_registry.lm, ace_req.lm_model, g_loaded_lm);
            std::string dit_name = resolve_name(g_registry.dit, ace_req.synth_model, g_loaded_und_dit);
            std::string vae_name = resolve_name(g_registry.vae, ace_req.vae, g_loaded_vae);
            const ModelEntry * lm  = registry_find(g_registry.lm, lm_name.c_str());
            const ModelEntry * dit = registry_find(g_registry.dit, dit_name.c_str());
            const ModelEntry * vae = registry_find(g_registry.vae, vae_name.c_str());
            if (!lm || !dit || !vae) {
                fprintf(stderr, "[IPC] Understand: model not found\n");
                job->status.store(EngineJobStatus::FAILED); return;
            }

            AceUnderstandParams p = g_und_params;
            p.model_path = lm->path.c_str();
            p.dit_path   = dit->path.c_str();
            p.vae_path   = vae->path.c_str();

            AceUnderstand * ctx = ace_understand_load(g_store, &p);
            if (!ctx) { job->status.store(EngineJobStatus::FAILED); return; }

            AceRequest out;
            std::vector<float> captured_latent;
            int captured_T = 0;
            int rc = ace_understand_generate(ctx, nullptr, 0, nullptr, 0, &ace_req,
                                             &out, &captured_latent, &captured_T,
                                             server_cancel_job, (void*)&job->cancel);
            ace_understand_free(ctx);

            if (rc != 0) {
                job->status.store(job->cancel.load() ? EngineJobStatus::CANCELLED : EngineJobStatus::FAILED);
                return;
            }

            if (g_keep_loaded) {
                g_loaded_lm = lm_name; g_loaded_und_dit = dit_name; g_loaded_vae = vae_name;
            } else { g_loaded_lm.clear(); g_loaded_und_dit.clear(); g_loaded_vae.clear(); }

            std::string json = request_to_json(&out);
            job->result_body = multipart_build_json_latent(json, captured_latent, captured_T);
            job->result_mime = MULTIPART_MIME;
            job->status.store(EngineJobStatus::DONE);
            fprintf(stderr, "[IPC] Job %s done (understand)\n", job->id.c_str());
        });
        send_job_id(job->id);
    } else {
        send_error(-32601, "Method not found");
    }

    yyjson_doc_free(doc);
}

// Accept and handle one client connection.
// Reads JSON-RPC frames and dispatches them sequentially.
static void handle_client(int client_fd) {
    for (;;) {
        IpcFrameType type;
        uint32_t     len;
        if (!ipc_read_frame_header(client_fd, type, len)) break;
        if (type != IPC_FRAME_JSON || len == 0) continue;

        std::string payload(len, '\0');
        size_t      offset = 0;
        while (offset < len) {
            ssize_t n = read(client_fd, &payload[offset], len - offset);
            if (n <= 0) goto done;
            offset += (size_t)n;
        }

        // Parse JSON-RPC envelope
        yyjson_doc * doc  = yyjson_read(payload.c_str(), payload.size(), 0);
        yyjson_val * root = doc ? yyjson_doc_get_root(doc) : nullptr;
        if (!doc || !root) { yyjson_doc_free(doc); continue; }

        const char * method = yyjson_get_str(yyjson_obj_get(root, "method"));
        int64_t      id     = yyjson_get_int(yyjson_obj_get(root, "id"));
        yyjson_val * params = yyjson_obj_get(root, "params");

        if (!method) { yyjson_doc_free(doc); continue; }

        std::string params_json = "{}";
        if (params) {
            yyjson_mut_doc * tmp_doc = yyjson_mut_doc_new(NULL);
            yyjson_mut_val * copy = yyjson_val_mut_copy(tmp_doc, params);
            yyjson_mut_doc_set_root(tmp_doc, copy);
            char * j = yyjson_mut_write(tmp_doc, 0, NULL);
            if (j) { params_json = j; free(j); }
            yyjson_mut_doc_free(tmp_doc);
        }

        dispatch(client_fd, method, params_json, id);
        yyjson_doc_free(doc);
    }
done:
    close(client_fd);
}

// Start the worker thread
static void start_worker(std::thread & worker) {
    worker = std::thread(worker_main);
}

int main(int argc, char ** argv) {
    EngineArgs args;
    const char * socket_path = nullptr;

    for (int i = 1; i < argc; i++) {
        if (!strcmp(argv[i], "--models") && i + 1 < argc)
            args.models_dir = argv[++i];
        else if (!strcmp(argv[i], "--adapters") && i + 1 < argc)
            args.adapters_dir = argv[++i];
        else if (!strcmp(argv[i], "--socket") && i + 1 < argc)
            socket_path = argv[++i];
        else if (!strcmp(argv[i], "--language-model") && i + 1 < argc)
            args.language_model = argv[++i];
        else if (!strcmp(argv[i], "--language-gpu"))
            args.language_gpu = true;
        else if (!strcmp(argv[i], "--keep-loaded"))
            args.keep_loaded = true;
        else if (!strcmp(argv[i], "--max-batch") && i + 1 < argc)
            args.max_batch = atoi(argv[++i]);
        else if (!strcmp(argv[i], "--max-seq") && i + 1 < argc)
            args.max_seq = atoi(argv[++i]);
        else if (!strcmp(argv[i], "--vae-chunk") && i + 1 < argc)
            args.vae_chunk = atoi(argv[++i]);
        else if (!strcmp(argv[i], "--vae-overlap") && i + 1 < argc)
            args.vae_overlap = atoi(argv[++i]);
        else if (!strcmp(argv[i], "--no-fsm"))
            args.no_fsm = true;
        else if (!strcmp(argv[i], "--no-fa"))
            args.no_fa = true;
        else if (!strcmp(argv[i], "--no-batch-cfg"))
            args.no_batch_cfg = true;
        else if (!strcmp(argv[i], "--clamp-fp16"))
            args.clamp_fp16 = true;
        else if (!strcmp(argv[i], "--help") || !strcmp(argv[i], "-h")) {
            usage(argv[0]);
            return 0;
        } else {
            fprintf(stderr, "Unknown option: %s\n", argv[i]);
            usage(argv[0]);
            return 1;
        }
    }

    if (!args.models_dir) { fprintf(stderr, "[Polaris] ERROR: --models is required\n"); return 1; }
    if (!args.adapters_dir) { fprintf(stderr, "[Polaris] ERROR: --adapters is required\n"); return 1; }

    // init engine
    if (!engine_init(args)) { fprintf(stderr, "[Polaris] Engine init failed\n"); return 1; }

    // start worker thread (FIFO GPU jobs)
    std::thread worker;
    start_worker(worker);

    // create listening socket
    std::string sock_path = socket_path ? socket_path : ipc_socket_path();
    if (sock_path.empty()) {
        fprintf(stderr, "[Polaris] ERROR: cannot determine socket path (XDG_RUNTIME_DIR?)\n");
        engine_shutdown();
        return 1;
    }

    // The desktop client expects the standard $XDG_RUNTIME_DIR/polaris
    // socket location. Create its private parent directory before bind;
    // otherwise a first launch fails with ENOENT and the Model Vault never
    // receives the registry props even when the model folder is valid.
    const size_t last_slash = sock_path.find_last_of('/');
    if (last_slash != std::string::npos) {
        const std::string socket_dir = sock_path.substr(0, last_slash);
        if (mkdir(socket_dir.c_str(), 0700) != 0 && errno != EEXIST) {
            fprintf(stderr, "[Polaris] ERROR: cannot create socket directory %s: %s\n",
                    socket_dir.c_str(), strerror(errno));
            engine_shutdown();
            return 1;
        }
    }

    // remove stale socket from previous instance
    unlink(sock_path.c_str());

    int sfd = socket(AF_UNIX, SOCK_STREAM, 0);
    if (sfd < 0) { perror("socket"); engine_shutdown(); return 1; }

    struct sockaddr_un addr;
    memset(&addr, 0, sizeof(addr));
    addr.sun_family = AF_UNIX;
    snprintf(addr.sun_path, sizeof(addr.sun_path), "%s", sock_path.c_str());

    if (bind(sfd, (struct sockaddr *)&addr, sizeof(addr)) < 0) {
        perror("bind"); close(sfd); engine_shutdown(); return 1;
    }
    if (listen(sfd, 5) < 0) { perror("listen"); close(sfd); engine_shutdown(); return 1; }

    signal(SIGINT, on_signal);
    signal(SIGTERM, on_signal);

    fprintf(stderr, "[Polaris] Polaris engine %s\n", ACE_VERSION);
    fprintf(stderr, "[Polaris] IPC socket: %s\n", sock_path.c_str());
    fprintf(stderr, "[Polaris] Models: %zu LM, %zu Text-Enc, %zu DiT, %zu VAE, %zu Adapter\n",
            g_registry.lm.size(), g_registry.text_enc.size(),
            g_registry.dit.size(), g_registry.vae.size(), g_registry.adapters.size());

    g_server_fd = sfd;

    // accept loop
    while (g_server_fd >= 0) {
        int client = accept(g_server_fd, nullptr, nullptr);
        if (client < 0) {
            if (g_server_fd < 0) break;
            perror("accept");
            continue;
        }
        // Limit to local connections only
        // (AF_UNIX is inherently local — no security concern)
        handle_client(client);
    }

    if (g_server_fd >= 0) {
        close(g_server_fd);
        g_server_fd = -1;
    }
    unlink(sock_path.c_str());

    fprintf(stderr, "[Polaris] Shutting down worker...\n");
    engine_shutdown();
    worker.join();
    fprintf(stderr, "[Polaris] Done\n");
    return 0;
}
