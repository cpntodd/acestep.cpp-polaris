#pragma once
// prompt.h: ACE-Step prompt building and CoT parsing
//
// AcePrompt struct, Qwen3 chat template formatting,
// CoT (Chain-of-Thought) metadata extraction, YAML builders.

#include "bpe.h"
#include "task-types.h"

#include <cstdlib>
#include <cstring>
#include <string>
#include <vector>

// Qwen3 special token IDs (ACE-Step LM vocabulary)
#define TOKEN_IM_START   151644
#define TOKEN_IM_END     151645
#define TOKEN_THINK      151667
#define TOKEN_THINK_END  151668
#define AUDIO_CODE_BASE  151669
#define AUDIO_CODE_COUNT 65535

// ACE-Step prompt
struct AcePrompt {
    std::string caption;
    std::string lyrics;
    float       duration;
    int         bpm;
    std::string keyscale;
    std::string timesignature;
    std::string vocal_language;
};

// Macedonian shares the Cyrillic script with Serbian and Bulgarian, but its
// letters ѓ, ќ, and ѕ are distinctive. A few orthographic words are also
// useful high-confidence markers (ќе, зошто, каде, сакам, можам): Serbian and
// Bulgarian use different standard forms for these. The model can also return
// Latin transliteration, so the correction below recognizes a small set of
// Macedonian-only Latin forms as well. These are deliberately conservative,
// but high-confidence lyric evidence may correct any language label emitted by
// the listener.
static bool text_has_macedonian_script(const std::string & text) {
    static const char * const markers[] = {
        "\xD1\x93", // ѓ
        "\xD0\x83", // Ѓ
        "\xD1\x9C", // ќ
        "\xD0\x8C", // Ќ
        "\xD1\x95", // ѕ
        "\xD0\x85", // Ѕ
    };
    for (const char * marker : markers) {
        if (text.find(marker) != std::string::npos) {
            return true;
        }
    }
    return false;
}

static bool text_marker_boundary(const std::string & text, size_t pos, size_t len) {
    auto is_word_byte = [](unsigned char c) {
        // Treat UTF-8 continuation/leading bytes as word characters too, so a
        // Cyrillic marker cannot match halfway through another word.
        return c >= 0x80 || (c >= 'a' && c <= 'z') || (c >= 'A' && c <= 'Z') ||
               (c >= '0' && c <= '9') || c == '_';
    };
    const bool left_word  = pos > 0 && is_word_byte((unsigned char) text[pos - 1]);
    const size_t end      = pos + len;
    const bool right_word = end < text.size() && is_word_byte((unsigned char) text[end]);
    return !left_word && !right_word;
}

static bool text_has_macedonian_word(const std::string & text) {
    static const char * const markers[] = {
        "\xD1\x9C\xD0\xB5",                                 // ќе
        "\xD0\x8C\xD0\xB5",                                 // Ќе
        "\xD1\x88\xD1\x82\xD0\xBE",                         // што
        "\xD0\xA8\xD1\x82\xD0\xBE",                         // Што
        "\xD0\xB7\xD0\xBE\xD1\x88\xD1\x82\xD0\xBE",       // зошто
        "\xD0\x97\xD0\xBE\xD1\x88\xD1\x82\xD0\xBE",       // Зошто
        "\xD0\xBA\xD0\xB0\xD0\xB4\xD0\xB5",                 // каде
        "\xD0\x9A\xD0\xB0\xD0\xB4\xD0\xB5",                 // Каде
        "\xD1\x81\xD0\xB0\xD0\xBA\xD0\xB0\xD0\xBC",       // сакам
        "\xD0\xA1\xD0\xB0\xD0\xBA\xD0\xB0\xD0\xBC",       // Сакам
        "\xD0\xBC\xD0\xBE\xD0\xB6\xD0\xB0\xD0\xBC",       // можам
        "\xD0\x9C\xD0\xBE\xD0\xB6\xD0\xB0\xD0\xBC",       // Можам
        "\xD1\x82\xD0\xB5 \xD1\x81\xD0\xB0\xD0\xBA\xD0\xB0\xD0\xBC", // те сакам
        "\xD0\xA2\xD0\xB5 \xD1\x81\xD0\xB0\xD0\xBA\xD0\xB0\xD0\xBC", // Те сакам
    };
    for (const char * marker : markers) {
        size_t pos = 0;
        const size_t len = strlen(marker);
        while ((pos = text.find(marker, pos)) != std::string::npos) {
            if (text_marker_boundary(text, pos, len)) {
                return true;
            }
            pos += len;
        }
    }
    return false;
}

static bool text_has_macedonian_latin(const std::string & text) {
    static const char * const markers[] = {
        "sakam",     // сакам
        "zosto",     // зошто
        "zoshto",    // alternative sh transliteration
        "što",       // што
        "kade",      // каде
        "mozam",     // можам
        "te sakam",  // те сакам
        "jas sum",   // јас сум
        "kaj",       // кај
        "ke",        // ќе (used in standard Latin transliteration)
    };
    for (const char * marker : markers) {
        size_t pos = 0;
        const size_t len = strlen(marker);
        while ((pos = text.find(marker, pos)) != std::string::npos) {
            if (text_marker_boundary(text, pos, len)) {
                // The two-letter transliteration is too common on its own;
                // require a second Macedonian marker before accepting it.
                if (len > 2 || text.find("sakam") != std::string::npos ||
                    text.find("zosto") != std::string::npos || text.find("kade") != std::string::npos) {
                    return true;
                }
            }
            pos += len;
        }
    }
    return false;
}

static void replace_ascii_insensitive(std::string & text, const char * needle, const char * replacement) {
    const size_t needle_len = strlen(needle);
    const size_t repl_len   = strlen(replacement);
    if (needle_len == 0) {
        return;
    }
    for (size_t pos = 0; pos + needle_len <= text.size();) {
        bool match = true;
        for (size_t i = 0; i < needle_len; i++) {
            char a = text[pos + i];
            char b = needle[i];
            if (a >= 'A' && a <= 'Z') {
                a = (char) (a - 'A' + 'a');
            }
            if (b >= 'A' && b <= 'Z') {
                b = (char) (b - 'A' + 'a');
            }
            if (a != b) {
                match = false;
                break;
            }
        }
        if (match) {
            text.replace(pos, needle_len, replacement);
            pos += repl_len;
        } else {
            pos++;
        }
    }
}

static bool caption_mentions_macedonian(const std::string & caption) {
    const std::string lower = [&]() {
        std::string value = caption;
        for (char & ch : value) {
            if (ch >= 'A' && ch <= 'Z') {
                ch = (char) (ch - 'A' + 'a');
            }
        }
        return value;
    }();
    return lower.find("macedonian") != std::string::npos || lower.find("македон") != std::string::npos;
}

// The understand model sometimes describes a Macedonian-language recording as
// Turkish, Indian, or generic Eastern European folk because it recognizes
// shared Balkan instrumentation. Once the language evidence is high
// confidence, keep the musical texture but make the regional identity explicit
// in the reusable style prompt.
static void normalize_macedonian_caption(AcePrompt * out) {
    if (out->vocal_language != "mk") {
        return;
    }
    replace_ascii_insensitive(out->caption, "Eastern European or Turkish folk", "Macedonian/Balkan folk");
    replace_ascii_insensitive(out->caption, "Eastern European/Turkish folk", "Macedonian/Balkan folk");
    replace_ascii_insensitive(out->caption, "Balkan or Turkish folk", "Macedonian/Balkan folk");
    replace_ascii_insensitive(out->caption, "Turkish-style folk", "Macedonian/Balkan folk");
    replace_ascii_insensitive(out->caption, "Turkish folk", "Macedonian/Balkan folk");
    replace_ascii_insensitive(out->caption, "Indian folk", "Macedonian/Balkan folk");
    replace_ascii_insensitive(out->caption, "Indian classical", "Macedonian/Balkan folk");
    replace_ascii_insensitive(out->caption, "Indian-inspired", "Macedonian/Balkan-inspired");
    replace_ascii_insensitive(out->caption, "Eastern European", "Macedonian/Balkan");
    replace_ascii_insensitive(out->caption, "Turkish", "Macedonian/Balkan");
    replace_ascii_insensitive(out->caption, "Indian", "Macedonian/Balkan");
    if (out->caption.empty()) {
        out->caption = "Macedonian-language Balkan folk music";
    } else if (!caption_mentions_macedonian(out->caption)) {
        out->caption = "Macedonian-language " + out->caption;
    }
}

static std::string canonicalize_language_hint(std::string language) {
    while (!language.empty() && (language.front() == ' ' || language.front() == '\t' || language.front() == '\'' ||
                                 language.front() == '"')) {
        language.erase(language.begin());
    }
    while (!language.empty() && (language.back() == ' ' || language.back() == '\t' || language.back() == '\'' ||
                                 language.back() == '"' || language.back() == '\r')) {
        language.pop_back();
    }
    for (char & ch : language) {
        if (ch >= 'A' && ch <= 'Z') {
            ch = (char) (ch - 'A' + 'a');
        }
    }
    if (language == "macedonian" || language == "macedonian (mk)" || language == "mkd" ||
        language == "mac") {
        return "mk";
    }
    return language;
}

static void normalize_detected_language(const std::string & cot, const std::string & lyrics, AcePrompt * out) {
    const bool macedonian_evidence =
        text_has_macedonian_script(cot) || text_has_macedonian_script(lyrics) || text_has_macedonian_word(cot) ||
        text_has_macedonian_word(lyrics) || text_has_macedonian_latin(cot) || text_has_macedonian_latin(lyrics);
    if (macedonian_evidence) {
        out->vocal_language = "mk";
        normalize_macedonian_caption(out);
        return;
    }
    out->vocal_language = canonicalize_language_hint(out->vocal_language);
    if (out->vocal_language == "mk") {
        normalize_macedonian_caption(out);
    }
}

// CoT parsing (extract metadata + lyrics from LLM Phase1 output)
static bool parse_cot_and_lyrics(const std::string & text, AcePrompt * out) {
    // Extract CoT content between <think>...</think>
    size_t ts = text.find("<think>");
    size_t te = text.find("</think>");

    std::string cot;
    std::string lyrics_after;

    if (ts != std::string::npos && te != std::string::npos) {
        cot          = text.substr(ts + 7, te - ts - 7);
        lyrics_after = text.substr(te + 8);
    } else if (te != std::string::npos) {
        cot          = text.substr(0, te);
        lyrics_after = text.substr(te + 8);
    } else {
        cot = text;
    }

    // Parse YAML-like fields from CoT
    auto get_field = [&](const std::string & key) -> std::string {
        std::string needle = key + ":";
        size_t      p      = cot.find(needle);
        if (p == std::string::npos) {
            return "";
        }
        p += needle.size();
        while (p < cot.size() && (cot[p] == ' ' || cot[p] == '\'')) {
            p++;
        }
        size_t end = cot.find('\n', p);
        if (end == std::string::npos) {
            end = cot.size();
        }
        std::string val = cot.substr(p, end - p);
        // Strip trailing whitespace and quotes
        while (!val.empty() && (val.back() == ' ' || val.back() == '\'' || val.back() == '\r')) {
            val.pop_back();
        }
        return val;
    };

    std::string bpm_s = get_field("bpm");
    if (!bpm_s.empty()) {
        out->bpm = atoi(bpm_s.c_str());
    }

    std::string dur_s = get_field("duration");
    if (!dur_s.empty()) {
        out->duration = (float) atof(dur_s.c_str());
    }

    std::string ks = get_field("keyscale");
    if (!ks.empty()) {
        out->keyscale = ks;
    }

    std::string ts_s = get_field("timesignature");
    if (!ts_s.empty()) {
        out->timesignature = ts_s;
    }

    std::string lang = get_field("language");
    if (!lang.empty()) {
        out->vocal_language = lang;
    }

    std::string cap = get_field("caption");
    if (!cap.empty()) {
        // Caption may span multiple lines (YAML word-wrap), read until next field
        size_t cp = cot.find("caption:");
        if (cp != std::string::npos) {
            cp += 8;
            size_t end = cot.find("\nduration:", cp);
            if (end == std::string::npos) {
                end = cot.find("\nkeyscale:", cp);
            }
            if (end == std::string::npos) {
                end = cot.size();
            }
            std::string full_cap = cot.substr(cp, end - cp);
            // Trim and collapse whitespace
            std::string cleaned;
            bool        in_space = true;
            for (char ch : full_cap) {
                if (ch == '\n' || ch == '\r') {
                    ch = ' ';
                }
                if (ch == ' ') {
                    if (!in_space) {
                        cleaned += ' ';
                    }
                    in_space = true;
                } else {
                    cleaned += ch;
                    in_space = false;
                }
            }
            while (!cleaned.empty() && cleaned.back() == ' ') {
                cleaned.pop_back();
            }
            while (!cleaned.empty() && cleaned.front() == ' ') {
                cleaned.erase(cleaned.begin());
            }
            if (!cleaned.empty()) {
                out->caption = cleaned;
            }
        }
    }

    // Lyrics after </think>
    if (!lyrics_after.empty()) {
        // Trim leading whitespace
        size_t s = lyrics_after.find_first_not_of(" \t\n\r");
        if (s != std::string::npos) {
            lyrics_after = lyrics_after.substr(s);
        }
        // Strip "# Lyric\n" header the LM may echo back
        // (may be preceded by "# Languages\n...\n\n")
        size_t lp = lyrics_after.find("# Lyric\n");
        if (lp != std::string::npos && lp < 64) {
            lyrics_after = lyrics_after.substr(lp + 8);
        }
        // Trim trailing whitespace
        while (!lyrics_after.empty() &&
               (lyrics_after.back() == ' ' || lyrics_after.back() == '\n' || lyrics_after.back() == '\r')) {
            lyrics_after.pop_back();
        }
        if (!lyrics_after.empty()) {
            out->lyrics = lyrics_after;
        }
    }

    // Correct regional-language confusion from both the model's metadata and
    // the recovered lyrics/caption. This also handles Turkish/Indian labels
    // produced when Macedonian instrumentation is misread.
    normalize_detected_language(text, out->lyrics, out);

    return (out->bpm > 0 || out->duration > 0);
}

// Prompt building (Qwen3 chat template)
static std::vector<int> build_lm_prompt(BPETokenizer & bpe, const AcePrompt & prompt) {
    std::vector<int> ids;
    auto             append = [&](const std::string & text) {
        auto t = bpe_encode(&bpe, text, false);
        ids.insert(ids.end(), t.begin(), t.end());
    };
    ids.push_back(TOKEN_IM_START);
    append(std::string("system\n# Instruction\n") + LM_INSTRUCTION + "\n\n");
    ids.push_back(TOKEN_IM_END);
    append("\n");
    ids.push_back(TOKEN_IM_START);
    append("user\n# Caption\n" + prompt.caption + "\n\n# Lyric\n" + prompt.lyrics + "\n");
    ids.push_back(TOKEN_IM_END);
    append("\n");
    ids.push_back(TOKEN_IM_START);
    append("assistant\n");
    return ids;
}

static std::vector<int> build_lm_prompt_uncond(BPETokenizer &    bpe,
                                               const AcePrompt & prompt,
                                               const char *      negative_prompt) {
    std::vector<int> ids;
    auto             append = [&](const std::string & text) {
        auto t = bpe_encode(&bpe, text, false);
        ids.insert(ids.end(), t.begin(), t.end());
    };
    ids.push_back(TOKEN_IM_START);
    append(std::string("system\n# Instruction\n") + LM_INSTRUCTION + "\n\n");
    ids.push_back(TOKEN_IM_END);
    append("\n");
    ids.push_back(TOKEN_IM_START);
    bool has_neg = negative_prompt && strlen(negative_prompt) > 0;
    if (has_neg) {
        append("user\n# Caption\n" + std::string(negative_prompt) + "\n\n# Lyric\n" + prompt.lyrics + "\n");
    } else {
        append("user\n# Lyric\n" + prompt.lyrics + "\n");
    }
    ids.push_back(TOKEN_IM_END);
    append("\n");
    ids.push_back(TOKEN_IM_START);
    append("assistant\n");
    return ids;
}

// Build CoT YAML content (matching Python yaml.dump sort_keys=True)
static std::string build_cot_yaml(const AcePrompt & prompt) {
    // Matches Python yaml.dump(allow_unicode=True, sort_keys=True) wrapping:
    // line break + 2-space indent when current column exceeds 80.
    auto yaml_wrap = [](const std::string & key, const std::string & val) -> std::string {
        std::string result = key + ":";
        int         col    = (int) (key.size() + 1);
        size_t      i      = 0;
        while (i < val.size()) {
            size_t end = val.find(' ', i);
            if (end == std::string::npos) {
                end = val.size();
            }
            std::string word = val.substr(i, end - i);
            if (col > 80) {
                result += "\n  ";
                col = 2;
            } else {
                result += " ";
                col += 1;
            }
            result += word;
            col += (int) word.size();
            i = (end < val.size()) ? end + 1 : val.size();
        }
        result += "\n";
        return result;
    };

    std::string yaml;
    if (prompt.bpm > 0) {
        yaml += "bpm: " + std::to_string(prompt.bpm) + "\n";
    }
    if (!prompt.caption.empty()) {
        yaml += yaml_wrap("caption", prompt.caption);
    }
    if (prompt.duration > 0) {
        yaml += "duration: " + std::to_string((int) prompt.duration) + "\n";
    }
    if (!prompt.keyscale.empty()) {
        yaml += "keyscale: " + prompt.keyscale + "\n";
    }
    if (!prompt.vocal_language.empty()) {
        yaml += "language: " + prompt.vocal_language + "\n";
    }
    if (!prompt.timesignature.empty()) {
        yaml += "timesignature: " + prompt.timesignature + "\n";
    }
    return yaml;
}

// Prompt with injected CoT (Phase 2: all metas known)
// Assistant turn stays open so the LM generates audio codes inside it
// and emits im_end itself as stop.
static std::vector<int> build_lm_prompt_with_cot(BPETokenizer &      bpe,
                                                 const AcePrompt &   prompt,
                                                 const std::string & cot_yaml) {
    std::vector<int> ids;
    auto             append = [&](const std::string & text) {
        auto t = bpe_encode(&bpe, text, false);
        ids.insert(ids.end(), t.begin(), t.end());
    };
    ids.push_back(TOKEN_IM_START);
    append(std::string("system\n# Instruction\n") + LM_INSTRUCTION + "\n\n");
    ids.push_back(TOKEN_IM_END);
    append("\n");
    ids.push_back(TOKEN_IM_START);
    append("user\n# Caption\n" + prompt.caption + "\n\n# Lyric\n" + prompt.lyrics + "\n");
    ids.push_back(TOKEN_IM_END);
    append("\n");
    ids.push_back(TOKEN_IM_START);
    append("assistant\n");
    ids.push_back(TOKEN_THINK);
    append("\n" + cot_yaml);
    ids.push_back(TOKEN_THINK_END);
    append("\n\n");
    return ids;
}

// Unconditional prompt with empty CoT for CFG (Phase 2)
// Bare user content (no Caption/Lyric wrapper) matches the training CFG dropout.
// Assistant turn stays open so the LM generates audio codes inside it.
// Empty CoT uses two inner newlines because Qwen's chat template renders
// reasoning via `<think>\n{reasoning.strip('\n')}\n</think>`.
static std::vector<int> build_lm_prompt_uncond_with_cot(BPETokenizer & bpe, const char * negative_prompt) {
    std::vector<int> ids;
    auto             append = [&](const std::string & text) {
        auto t = bpe_encode(&bpe, text, false);
        ids.insert(ids.end(), t.begin(), t.end());
    };
    ids.push_back(TOKEN_IM_START);
    append(std::string("system\n# Instruction\n") + LM_INSTRUCTION + "\n\n");
    ids.push_back(TOKEN_IM_END);
    append("\n");
    ids.push_back(TOKEN_IM_START);
    const char * neg = (negative_prompt && *negative_prompt) ? negative_prompt : "";
    append(std::string("user\n") + neg);
    ids.push_back(TOKEN_IM_END);
    append("\n");
    ids.push_back(TOKEN_IM_START);
    append("assistant\n");
    ids.push_back(TOKEN_THINK);
    append("\n\n");
    ids.push_back(TOKEN_THINK_END);
    append("\n\n");
    return ids;
}

// Build Qwen3 chat prompt: <|im_start|>system\n...<|im_end|>\n<|im_start|>user\n...<|im_end|>\n<|im_start|>assistant\n
static std::vector<int> build_custom_prompt(BPETokenizer & bpe, const char * sys, const char * user) {
    std::vector<int> ids;
    auto             append = [&](const std::string & text) {
        auto t = bpe_encode(&bpe, text, false);
        ids.insert(ids.end(), t.begin(), t.end());
    };
    ids.push_back(TOKEN_IM_START);
    append("system\n" + std::string(sys) + "\n");
    ids.push_back(TOKEN_IM_END);
    append("\n");
    ids.push_back(TOKEN_IM_START);
    append("user\n" + std::string(user) + "\n");
    ids.push_back(TOKEN_IM_END);
    append("\n");
    ids.push_back(TOKEN_IM_START);
    append("assistant\n");
    return ids;
}

// Build understand prompt: system instruction + raw audio code tokens as user input.
// codes[] are FSQ indices (from tok_ggml_encode or parsed from audio_codes string).
// They become token IDs AUDIO_CODE_BASE + codes[i] in the user turn.
static std::vector<int> build_understand_prompt(BPETokenizer & bpe, const int * codes, int n_codes) {
    std::vector<int> ids;
    auto             append = [&](const std::string & text) {
        auto t = bpe_encode(&bpe, text, false);
        ids.insert(ids.end(), t.begin(), t.end());
    };
    ids.push_back(TOKEN_IM_START);
    append(std::string("system\n# Instruction\n") + LM_UNDERSTAND_INSTRUCTION + "\n\n");
    ids.push_back(TOKEN_IM_END);
    append("\n");
    ids.push_back(TOKEN_IM_START);
    append("user\n");
    for (int i = 0; i < n_codes; i++) {
        ids.push_back(AUDIO_CODE_BASE + codes[i]);
    }
    append("\n");
    ids.push_back(TOKEN_IM_END);
    append("\n");
    ids.push_back(TOKEN_IM_START);
    append("assistant\n");
    return ids;
}
