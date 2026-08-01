#pragma once

// Dedicated local speech-language identification for reference analysis.
// The recognizer is intentionally separate from ACE-Step's music listener:
// VAD selects vocal-active windows, then Whisper supplies language evidence.

#include <string>

struct AceLanguageIdentifier;

struct AceLanguageResult {
    std::string language; // "en", "mk", or "unknown"
    float       confidence = 0.0f;
    // Consensus support across accepted voice-active windows, expressed as a
    // value from 0 to 1 for the UI/logging layer.
    float       english_probability = 0.0f;
    float       macedonian_probability = 0.0f;
    int         voice_windows = 0;
};

AceLanguageIdentifier * ace_language_id_create(const char * model_path,
                                               int         n_threads = 0,
                                               bool        use_gpu   = false);
void                   ace_language_id_free(AceLanguageIdentifier * identifier);

// samples are interleaved stereo 48 kHz [L0,R0,L1,R1,...]. The recognizer
// performs local energy VAD and evaluates several vocal windows. It never
// contacts a remote service and returns unknown when English/Macedonian
// evidence is not strong enough.
bool ace_language_id_analyze(AceLanguageIdentifier * identifier,
                             const float *           samples,
                             int                     sample_count,
                             AceLanguageResult *     result);
