#include "audio-io.h"
#include "language-id.h"

#include <cstdio>
#include <cstdlib>
#include <string>

int main(int argc, char ** argv) {
    if (argc < 3 || argc > 4) {
        fprintf(stderr, "Usage: %s <whisper-model.bin> <audio.wav|audio.mp3> [--gpu]\n", argv[0]);
        return 2;
    }

    const bool use_gpu = argc == 4 && std::string(argv[3]) == "--gpu";
    AceLanguageIdentifier * identifier = ace_language_id_create(argv[1], 0, use_gpu);
    if (!identifier) return 1;

    int     samples_per_channel = 0;
    float * planar = audio_read_48k(argv[2], &samples_per_channel);
    if (!planar || samples_per_channel <= 0) {
        fprintf(stderr, "Could not decode %s\n", argv[2]);
        free(planar);
        ace_language_id_free(identifier);
        return 1;
    }
    float * interleaved = audio_planar_to_interleaved(planar, samples_per_channel);
    free(planar);
    if (!interleaved) {
        ace_language_id_free(identifier);
        return 1;
    }

    AceLanguageResult result;
    const bool        ok = ace_language_id_analyze(identifier, interleaved, samples_per_channel, &result);
    fprintf(stderr, "language=%s confidence=%.3f en=%.3f mk=%.3f windows=%d\n", result.language.c_str(),
            result.confidence, result.english_probability, result.macedonian_probability, result.voice_windows);
    free(interleaved);
    ace_language_id_free(identifier);
    return ok ? 0 : 1;
}
