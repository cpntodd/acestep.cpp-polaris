# Polaris Studio Desktop Rework — Proposal (for review/approval)

Status: **DRAFT — not yet approved**
Date: 2026-08-03
Supersedes: the packaged `ace-server` + Svelte WebUI + shell-script launcher stack as the user-facing product.

---

## 1. Goal

Replace the current architecture — a localhost HTTP server (`ace-server`) that serves an
embedded Svelte WebUI opened in a regular browser — with a **standalone native desktop
application** that:

- Runs as a real Linux desktop app (`.deb` + AppImage), **Linux-only**.
- Is built with **Qt 6 (C++), QML-first with Qt Widgets used where appropriate**.
- Contains **no webview, no JavaScript runtime, no Rust, no Electron/Chromium**.
- Keeps the existing C++/ggml engine as the backend, moved into a **worker subprocess**
  for crash isolation, communicating with the UI over **private IPC** (no HTTP in the app).
- Solves the **upload persistence problem**: reference songs are copied as real files into a
  **user-chosen library folder** and referenced by a **SQLite** database — never stored in a
  browser cache/IndexedDB.
- Ships **models separately** (settings panel + built-in downloader), so packages stay small.
- Carries the existing WebUI feature set over **at full parity**.

Final name (approved by owner): **Polaris Studio** (binary `polaris-studio`).

---

## 2. Current state (what we are replacing)

| Piece | Location | Role |
|---|---|---|
| `ace-server` | `tools/ace-server.cpp` + `src/*` | HTTP server; hosts JSON API + embedded WebUI; runs ggml pipelines |
| `acestep-supervisor` | `tools/acestep-supervisor.cpp` | Localhost process supervisor; power/restart switch for the server |
| `ace-lm/ace-synth/ace-understand/neural-codec/mp3-codec/quantize` | `tools/`, `src/` | CLI entry points to the engine |
| WebUI | `tools/webui/` (Svelte) | Rendered in the user's browser; stores uploads in IndexedDB (`lib/db.js`) |
| Launcher/stop | `packaging/acestep-cpp-launcher`, `-stop` | Shell scripts that drive supervisor + server + browser |
| Packaging | `packaging/build-deb.sh` | Bundles app **and all models** (multi-GB, fragile builds) |

Key facts from the engine that shape the rework (already runtime-oriented):
- `src/model-registry.h` — `registry_scan(models_dir)` discovers GGUF models by category
  (lm, text-enc/embedding, dit, vae) + adapters at runtime.
- `src/model-store.*` — loads/evicts GGML modules by path; `src/request.cpp` already has a
  JSON serializer (incl. a "sparse=false" full-doc mode for `/props`).
- The engine has no hard dependency on HTTP internally — HTTP is only the transport around
  request/response structs.

---

## 3. Target architecture

```
┌───────────────────────────────────────────────────────────────┐
│  polaris-studio  (Qt 6 native app, Linux)                     │
│                                                               │
│  QML UI (Qt Quick Controls)          Qt Widgets shell         │
│  • Library / song list                 • main window/tray     │
│  • Reference uploader + waveform       • native file dialogs  │
│  • Generate form + model pickers       • about/logs panel     │
│  • Player (Qt Multimedia)                                      │
│  • Metrics gauges (CPU/RAM/VRAM)                              │
│  • Models settings + downloader                                │
│                                                               │
│  IPC client  (JSON-RPC over Unix domain socket)               │
└───────────────┬───────────────────────────────────────────────┘
                │ spawn / monitor / restart / stop
                │ Unix socket: $XDG_RUNTIME_DIR/polaris/engine.sock
┌───────────────▼───────────────────────────────────────────────┐
│  polaris-engine  (worker subprocess, C++/ggml — crash-isolated)│
│                                                               │
│  • registry_scan(models_dir)         • ModelStore load/evict  │
│  • pipeline-lm / -synth / -understand  • language-id (en/mk)  │
│  • IPC server (JSON-RPC + binary audio frames)                │
└───────────────────────────────────────────────────────────────┘
```

**Process model**
- The Qt app is the only user-facing process. It spawns `polaris-engine` on launch (or on the
  "power" switch), watches its health, and **auto-restarts** it on crash — inheriting the
  resilience the old supervisor provided, but inside the app.
- Engine crashes never take down the UI (worker subprocess). The UI shows a friendly
  "engine restarted" state and re-syncs via a props handshake.
- On app quit, the engine is asked to shut down cleanly (releases model memory), then killed
  if unresponsive (5s grace).

**IPC protocol (replaces HTTP)**
- Unix domain socket at `$XDG_RUNTIME_DIR/polaris/engine.sock` (per-user, `0700`).
- **JSON-RPC 2.0** over length-prefixed frames for requests/responses/events.
- Binary audio chunks streamed as framed binary messages (e.g., `data:audio`, `data:waveform`).
- Messages map 1:1 onto today's HTTP endpoints: `props`, `metrics`, `generate`, `analyze`,
  `models.list`, `health`, `shutdown`, etc.
- The existing request/response serializers in `src/request.cpp` are reused behind a thin
  transport abstraction (`ServerTransport`: HTTP-for-dev vs IPC-for-app). A dev-only HTTP mode
  is kept in-tree for regression testing against `client-batch.py` and the old WebUI fixtures.

---

## 4. Data & persistence

**Library folder (user-chosen)**
- Default: `~/Music/PolarisStudio` (via `QStandardPaths::MusicLocation`, falling back to
  `~/Music` when `$XDG_MUSIC_DIR` is unset on a basic Debian install), created on first run,
  configurable in Settings, persisted in the DB.
- Layout:
  ```
  <library>/
    reference/    <sha1>.<ext>     # uploaded source songs (copied in, never referenced in place)
    generated/    <song-id>.wav    # synthesized output
  ```
- Import semantics: **copy into the library** so songs persist even if the original is moved or
  deleted. Duplicates detected by content hash (re-import is a no-op that returns the existing id).

**Metadata — SQLite (Qt SQL)**
- DB at `~/.local/share/polaris-studio/polaris.db` (`QStandardPaths::AppDataLocation`).
- Tables: `songs` (id, kind=reference|generated, relative path, hash, size, duration, language,
  analysis state, created_at), `requests` (params snapshot per generation), `settings`
  (key/value: models dir, adapters dir, library path, engine autostart, …), `downloads`
  (model download state), `analysis` (per-song analysis results).
- File paths stored **relative** to the library so the library is portable/relocatable.
- No migration from the old IndexedDB data (fresh start, per decision).

**Models**
- Not bundled. Settings panel shows current models dir + scanned registry (lm/dit/vae/text-enc/adapters).
- **Default models dir** (Debian-friendly, no config needed): `~/.local/share/polaris-studio/models`
  (`QStandardPaths::AppDataLocation/models`), created on first run. The in-app downloader
  populates it; Settings lets the user point elsewhere (and the engine rescans).
- Built-in **downloader** pulls the default model set with progress (network via
  `QNetworkAccessManager`), verifies size/hash, then triggers a rescan.
- First-run empty state: "No models found — download the default set or choose a folder."

### Model downloader manifest (default set — mirrors `models.sh` defaults)

| File | Role | Source repo | Size (approx) |
|---|---|---|---|
| `vae-BF16.gguf` | VAE (decode latents → 48 kHz stereo) | `Serveurperso/ACE-Step-1.5-GGUF` | 322 MB |
| `Qwen3-Embedding-0.6B-Q8_0.gguf` | Text encoder (captions → DiT) | `Serveurperso/ACE-Step-1.5-GGUF` | 748 MB |
| `acestep-5Hz-lm-4B-Q8_0.gguf` | Causal LM (lyrics + audio codes) | `Serveurperso/ACE-Step-1.5-GGUF` | 4.2 GB |
| `acestep-v15-turbo-Q8_0.gguf` | DiT (render audio codes → sound) | `Serveurperso/ACE-Step-1.5-GGUF` | 2.4 GB |
| `ggml-large-v3-turbo-q5_0.bin` | Local Whisper language listener (en/mk) | `ggerganov/whisper.cpp` | ~1.5 GB |

Optional variants offered in the downloader UI (advanced): smaller LM
`acestep-5Hz-lm-0.6B-Q8_0.gguf` (fast), other quants (`Q4_K_M`/`Q5_K_M`/`Q6_K`/`BF16` for DiT;
LM-4B excludes `Q4_K_M` — it breaks audio codes), and DiT variants (`sft`, `base`, `shift1`,
`shift3`, `continuous`) — matching `models.sh`/`quantize.sh` rules.

---

## 5. UI — full parity with the current WebUI

| WebUI today | Qt 6 equivalent |
|---|---|
| Song list (uploaded + generated, provenance) | `SongList` QML component backed by SQLite model |
| Reference uploader (drag-drop) | `ReferenceUploader` — native drop + `QFileDialog`; **file written via Qt** to library (the persistence fix) |
| Analysis status (en/mk) | `AnalysisView` — language-id + per-song analysis from engine IPC |
| Request/generate form (params, model pickers) | `RequestForm` — pickers populated from IPC `models.list` |
| Steam gauge / metrics (CPU/RAM/VRAM) | `MetricsGauge` — Qt Charts or custom QML Canvas, polled via IPC `metrics` |
| Playback of reference/generated audio | Qt Multimedia (`QMediaPlayer`) + waveform scrubber |
| Power switch / engine state | Toolbar switch → IPC `start/stop` on the worker |
| Toasts / status | QML toast/status strip + native notifications |

**Qt flavor (hybrid, per decision):**
- **QML-first**: all main screens use Qt Quick Controls 2 (fluid, less C++ boilerplate, good for
  gauges/waveforms via `Canvas`/custom items).
- **Qt Widgets where they fit**: system tray (QML has no native tray → thin `QSystemTrayIcon` in a
  Qt Widgets shell that hosts the QML scene), advanced logs/about panel, and native file dialogs
  (though QML can call `QFileDialog` via a small wrapper).

---

## 6. Engine reuse & refactor plan

**Keep untouched:** `ggml/`, `src/dit*`, `src/pipeline-*`, `src/model-store.*`, `src/model-registry.h`,
`src/cond-enc.h`, `src/language-id.*`, `src/backend.h`, VAE/FSQ, neural-codec/mp3 codecs, etc.

**Refactor:**
1. `ServerTransport` abstraction in the engine — `http` (dev/tests) vs `ipc` (Unix socket).
   The HTTP handler logic in `tools/ace-server.cpp` is split so the request/response handling is
   transport-agnostic.
2. New `tools/polaris-engine.cpp` — the worker: parses `--models/--adapters/--socket`, opens the
   Unix socket, runs the existing pipelines, streams audio/progress events. Built from the same
   CMake targets as `ace-server`.
3. New `src/engine-ipc.*` — JSON-RPC framing + binary frame codec shared by worker and app.
4. New `src/engine-client.*` (Qt side, in `app/`) — spawn/monitor the worker, socket client,
   typed RPC stubs, event relay into QML.

**CLI tools:** folded out of the shipped product (not packaged). Sources stay for dev/tests.

---

## 7. Build & packaging

**Build (CMake — already in place)**
- Keep all ggml/engine targets. Add:
  - `polaris-engine` (worker, from engine sources).
  - `polaris-studio` (Qt app: links Qt6::Quick, Qt6::QuickControls2, Qt6::Multimedia,
    Qt6::Sql, Qt6::Network; QML resources compiled via `qt_add_qml_module` / `qt_add_resources`).
- Optional `-DPOLARIS_HTTP_DEV=ON` for the dev-only HTTP server mode (tests only, not packaged).

**Packaging**
- `.deb` (`polaris-studio`, via updated `packaging/build-deb.sh` or CMake `CPack`): **small** —
  app + engine worker + adapters + icons/desktop entry. **No models** (downloaded in-app).
  Depends on `qt6-*` runtime packages (`qt6-declarative`, `qt6-multimedia`, `qt6-sql-sqlite`,
  `qt6-network`), `libc6`, `libstdc++6`, `libgomp1`, `libvulkan1` (engine), and the engine's deps.
- **AppImage** via `linuxdeploy` + Qt plugin, bundling the app, the worker, and Qt libs.
- AppStream metadata, icon, desktop file under the `polaris-studio` app id.
- The old multi-GB bundling path in `build-deb.sh` is removed (models handled by the app's
  downloader/settings), which also eliminates the truncated-deb fragility seen earlier.

---

## 8. Milestones

| Phase | Scope | Exit criteria |
|---|---|---|
| **P0** | Extract engine worker + IPC transport (keep HTTP dev-mode) | `polaris-engine` answers `props/metrics/generate/analyze` over the Unix socket; HTTP mode still passes `client-batch.py` |
| **P1** | Qt shell: spawn/monitor worker, QML skeleton, props+metrics gauges | App launches, engine auto-starts/restarts, gauges live |
| **P2** | Library + upload + SQLite + playback | Import copies file into library, DB rows created, player plays reference/generated audio |
| **P3** | Generate + analyze flows, waveforms, full form parity | Full pipeline works end-to-end in the native UI |
| **P4** | Models settings + downloader + first-run empty state | Download defaults → rescan → pickers populate |
| **P5** | Packaging (.deb + AppImage), rename, AppStream, icon, tray | Clean install on a clean Linux box; models fetched in-app |
| **P6** | Polish: error surfaces, engine-crash UX, autostart, logging panel | Crash of engine auto-recovers without data loss |

---

## 9. Risks & notes

- **Largest effort: QML rewrite for full parity** (Svelte → QML). Mitigation: ship in the phased
  order above; P3 is the biggest single phase.
- **Worker IPC complexity** is modest (JSON-RPC over Unix socket is well-trodden); streaming
  binary audio needs careful framing (addressed in P0).
- **LGPLv3** with dynamically-linked Qt (system Qt6 packages) is acceptable per decision.
- **Engine crash isolation** is handled by the worker subprocess; the app must re-sync state
  (props handshake) after auto-restart.
- **Rename to Polaris Studio** touches the desktop file id, package name, docs, and `$XDG` paths —
  done once in P5 (binary `polaris-studio`).
- **Model downloads** require network and Hugging Face availability; the downloader must resume
  and verify (checksum) large GGUFs.

---

## 10. Resolved decisions (owner-approved)

1. **Name:** **Polaris Studio** / `polaris-studio`.
2. **Models:** downloader manifest = `models.sh` default set from
   `Serveurperso/ACE-Step-1.5-GGUF` (+ `ggerganov/whisper.cpp` listener), with the advanced
   variants above.
3. **Defaults (basic Debian installs, no config):** library `~/Music/PolarisStudio`;
   models `~/.local/share/polaris-studio/models`. Both auto-created; user-changeable in Settings.
4. **IPC framing:** **JSON-RPC 2.0 over a Unix domain socket** (length-prefixed frames + binary
   audio frames), as recommended.

---

*This document is a proposal for review. No implementation has been started.*
