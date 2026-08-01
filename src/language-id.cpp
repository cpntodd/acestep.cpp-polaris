#include "language-id.h"

#include "whisper.h"

#include <algorithm>
#include <cmath>
#include <cstdio>
#include <mutex>
#include <thread>
#include <vector>

namespace {

constexpr int   SAMPLE_RATE = 48000;
constexpr int   WHISPER_RATE = 16000;
constexpr int   WINDOW_SAMPLES = WHISPER_RATE * 30;
constexpr int   FRAME_SAMPLES = SAMPLE_RATE / 50; // 20 ms
constexpr float MIN_RMS = 0.004f;

struct VoiceWindow {
    int begin;
    int end;
};

static float frame_rms(const float * samples, int begin, int end) {
    if (!samples || end <= begin) return 0.0f;
    double sum = 0.0;
    for (int i = begin; i < end; i++) {
        const double v = samples[i];
        sum += v * v;
    }
    return (float) std::sqrt(sum / (double) (end - begin));
}

static std::vector<VoiceWindow> find_voice_windows(const float * mono, int n_samples) {
    std::vector<float> rms;
    for (int begin = 0; begin < n_samples; begin += FRAME_SAMPLES) {
        rms.push_back(frame_rms(mono, begin, std::min(begin + FRAME_SAMPLES, n_samples)));
    }
    if (rms.empty()) return {};

    std::vector<float> sorted = rms;
    std::sort(sorted.begin(), sorted.end());
    const size_t noise_index = std::min(sorted.size() - 1, sorted.size() * 2 / 10);
    const float  noise_floor = sorted[noise_index];
    // Music is not a conventional speech-recording noise floor: a quiet
    // instrumental passage can occupy most of the lower quantile. Keep the
    // energy gate permissive. Whisper then makes the multilingual language
    // decision over these candidate voice-active windows.
    const float  threshold = std::max(MIN_RMS, noise_floor * 1.35f);

    std::vector<VoiceWindow> windows;
    int                     run_begin = -1;
    int                     last_active = -1;
    const int                max_gap_frames = 15; // 300 ms
    const int                min_frames = 75;     // 1.5 s

    auto finish = [&](int frame_end) {
        if (run_begin < 0) return;
        const int active_end = std::max(run_begin, frame_end);
        if (active_end - run_begin >= min_frames) {
            const int pad = SAMPLE_RATE / 2;
            windows.push_back({ std::max(0, run_begin * FRAME_SAMPLES - pad),
                                std::min(n_samples, active_end * FRAME_SAMPLES + pad) });
        }
        run_begin = -1;
        last_active = -1;
    };

    for (int frame = 0; frame < (int) rms.size(); frame++) {
        if (rms[frame] >= threshold) {
            if (run_begin < 0) run_begin = frame;
            last_active = frame;
        } else if (run_begin >= 0 && frame - last_active > max_gap_frames) {
            finish(last_active + 1);
        }
    }
    finish(last_active + 1);

    // Split long runs into independent 30-second recognizer windows. This
    // keeps the language decision grounded in multiple vocal moments rather
    // than one intro or one instrumental break.
    std::vector<VoiceWindow> split;
    const int max_window = SAMPLE_RATE * 30;
    const int step = SAMPLE_RATE * 20;
    for (const VoiceWindow & window : windows) {
        if (window.end - window.begin <= max_window) {
            split.push_back(window);
            continue;
        }
        for (int begin = window.begin; begin < window.end; begin += step) {
            split.push_back({ begin, std::min(window.end, begin + max_window) });
            if ((int) split.size() >= 8) break;
        }
        if ((int) split.size() >= 8) break;
    }

    // Prefer a spread across the recording if there are many active windows.
    // Three independent vocal moments are enough for a language decision and
    // keep the CPU-only recognizer from making a long reference analysis feel
    // stuck on multi-minute songs.
    constexpr size_t MAX_LANGUAGE_WINDOWS = 3;
    if (split.size() > MAX_LANGUAGE_WINDOWS) {
        std::vector<VoiceWindow> selected;
        selected.reserve(MAX_LANGUAGE_WINDOWS);
        for (size_t i = 0; i < MAX_LANGUAGE_WINDOWS; i++) {
            const size_t index = i * (split.size() - 1) / (MAX_LANGUAGE_WINDOWS - 1);
            selected.push_back(split[index]);
        }
        return selected;
    }
    return split;
}

static std::vector<float> downmix_resample(const float * interleaved, int sample_count, int begin, int end) {
    const int source_count = std::max(0, end - begin);
    const int output_count = std::max(1, (int) std::lround((double) source_count * WHISPER_RATE / SAMPLE_RATE));
    std::vector<float> mono((size_t) source_count);
    for (int i = 0; i < source_count; i++) {
        const float left = interleaved[(begin + i) * 2];
        const float right = interleaved[(begin + i) * 2 + 1];
        mono[(size_t) i] = 0.5f * (left + right);
    }

    std::vector<float> output((size_t) output_count, 0.0f);
    for (int i = 0; i < output_count; i++) {
        const double position = (double) i * SAMPLE_RATE / WHISPER_RATE;
        const int    index = std::min(source_count - 1, (int) position);
        output[(size_t) i] = index >= 0 ? mono[(size_t) index] : 0.0f;
    }
    if ((int) output.size() > WINDOW_SAMPLES) output.resize(WINDOW_SAMPLES);
    // whisper_full accepts short clips. Avoid padding every small vocal
    // window to 30 seconds: the dedicated listener is run on several windows
    // and should not spend most of its time decoding silence.
    return output;
}

static int whisper_window_language(whisper_context * context, const float * pcm, int n_samples, int threads) {
    whisper_full_params params = whisper_full_default_params(WHISPER_SAMPLING_GREEDY);
    params.n_threads       = threads;
    params.no_context      = true;
    params.no_timestamps   = true;
    params.print_special   = false;
    params.print_progress  = false;
    params.print_realtime  = false;
    params.print_timestamps = false;
    params.language        = nullptr;
    params.detect_language = true;
    params.suppress_nst    = true;
    // Singing and heavily compressed folk previews can produce a higher
    // no-speech score than clean speech. The energy VAD has already selected
    // the candidate window; retain Whisper's language decision even when its
    // decoder produces no text segment for a short or highly musical phrase.
    params.no_speech_thold = 0.90f;
    if (whisper_full(context, params, pcm, (int) n_samples) != 0) {
        return -1;
    }
    return whisper_full_lang_id(context);
}

} // namespace

struct AceLanguageIdentifier {
    whisper_context * context = nullptr;
    int               threads = 4;
    bool              use_gpu = false;
    std::mutex        mutex;
};

AceLanguageIdentifier * ace_language_id_create(const char * model_path, int n_threads, bool use_gpu) {
    if (!model_path || !*model_path) return nullptr;
    auto * identifier = new AceLanguageIdentifier();
    identifier->threads = n_threads > 0 ? n_threads : std::max(1u, std::min(8u, std::thread::hardware_concurrency()));
    identifier->use_gpu = use_gpu;

    whisper_context_params params = whisper_context_default_params();
    // CPU is the safe default because ACE-Step may already occupy most of the
    // card. The explicit GPU option uses the same GGML Vulkan backend as the
    // rest of the application when the caller has enough VRAM headroom.
    params.use_gpu = use_gpu;
    params.flash_attn = false;
    identifier->context = whisper_init_from_file_with_params(model_path, params);
    if (!identifier->context) {
        fprintf(stderr, "[Language-ID] Could not load local speech model: %s\n", model_path);
        delete identifier;
        return nullptr;
    }
    fprintf(stderr, "[Language-ID] Ready: Whisper %s (backend=%s, %d CPU threads)\n", whisper_version(),
            use_gpu ? "Vulkan/GGML" : "CPU/GGML", identifier->threads);
    return identifier;
}

void ace_language_id_free(AceLanguageIdentifier * identifier) {
    if (!identifier) return;
    if (identifier->context) whisper_free(identifier->context);
    delete identifier;
}

bool ace_language_id_analyze(AceLanguageIdentifier * identifier,
                             const float *           samples,
                             int                     sample_count,
                             AceLanguageResult *     result) {
    if (!result) return false;
    *result = {};
    result->language = "unknown";
    if (!identifier || !identifier->context || !samples || sample_count <= 0) return false;

    std::vector<float> mono((size_t) sample_count);
    for (int i = 0; i < sample_count; i++) {
        mono[(size_t) i] = 0.5f * (samples[i * 2] + samples[i * 2 + 1]);
    }
    const std::vector<VoiceWindow> windows = find_voice_windows(mono.data(), sample_count);
    if (windows.empty()) return true;

    const int en_id = whisper_lang_id("en");
    const int mk_id = whisper_lang_id("mk");
    if (en_id < 0 || mk_id < 0) return false;

    std::lock_guard<std::mutex> lock(identifier->mutex);
    double en_sum = 0.0;
    double mk_sum = 0.0;
    int    valid = 0;
    for (const VoiceWindow & window : windows) {
        std::vector<float> clip = downmix_resample(samples, sample_count, window.begin, window.end);
        // Energy VAD only finds plausible active regions. Run the actual
        // multilingual speech recognizer once on each region so instrumental
        // passages do not become a random language label.
        const int top = whisper_window_language(identifier->context, clip.data(), (int) clip.size(), identifier->threads);
        if (top < 0) {
            continue;
        }
        if (top == en_id) en_sum += 1.0;
        if (top == mk_id) mk_sum += 1.0;
        valid++;
    }
    if (valid == 0) return true;

    const float english = (float) (en_sum / valid);
    const float macedonian = (float) (mk_sum / valid);
    result->english_probability = english;
    result->macedonian_probability = macedonian;
    result->voice_windows = valid;
    const bool macedonian_wins = macedonian > english;
    result->confidence = macedonian_wins ? macedonian : english;
    const float margin = std::fabs(macedonian - english);
    // The remaining languages are deliberately not exposed by this build.
    // If neither target wins with enough probability and separation, return
    // unknown instead of relabeling Macedonian as a nearby language.
    if (result->confidence >= 0.34f && margin >= 0.08f) {
        result->language = macedonian_wins ? "mk" : "en";
    }
    return true;
}
