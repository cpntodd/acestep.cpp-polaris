// test-language.cpp: Macedonian language parsing and normalization checks.

#include "prompt.h"

#include <cstdio>
#include <string>

static int fail(const char * message) {
    fprintf(stderr, "[Test] FAIL: %s\n", message);
    return 1;
}

int main() {
    AcePrompt macedonian = {};
    const std::string mk_text =
        "<think>\n"
        "bpm: 96\n"
        "duration: 30\n"
        "language: sr\n"
        "</think>\n"
        "# Lyric\n"
        "Ќе пеам за љубовта, ѓаволот нека спие.\n";
    if (!parse_cot_and_lyrics(mk_text, &macedonian)) {
        return fail("Macedonian fixture did not parse");
    }
    if (macedonian.vocal_language != "mk") {
        return fail("Macedonian-specific Cyrillic was not normalized to mk");
    }

    AcePrompt word_macedonian = {};
    const std::string mk_word_text =
        "<think>\n"
        "bpm: 96\n"
        "duration: 30\n"
        "language: bg\n"
        "</think>\n"
        "# Lyric\n"
        "Каде е мојата песна?\n";
    if (!parse_cot_and_lyrics(mk_word_text, &word_macedonian)) {
        return fail("Macedonian word-marker fixture did not parse");
    }
    if (word_macedonian.vocal_language != "mk") {
        return fail("Macedonian word marker was not normalized to mk");
    }

    AcePrompt bulgarian = {};
    const std::string bg_text =
        "<think>\n"
        "bpm: 96\n"
        "duration: 30\n"
        "language: bg\n"
        "</think>\n"
        "# Lyric\n"
        "Това е песен за любовта.\n";
    if (!parse_cot_and_lyrics(bg_text, &bulgarian)) {
        return fail("Bulgarian fixture did not parse");
    }
    if (bulgarian.vocal_language != "bg") {
        return fail("Bulgarian language was incorrectly changed");
    }

    AcePrompt explicit_macedonian = {};
    const std::string explicit_text =
        "<think>\n"
        "bpm: 96\n"
        "duration: 30\n"
        "language: mk\n"
        "</think>\n";
    if (!parse_cot_and_lyrics(explicit_text, &explicit_macedonian)) {
        return fail("Explicit mk fixture did not parse");
    }
    if (explicit_macedonian.vocal_language != "mk") {
        return fail("Explicit mk language was not preserved");
    }

    AcePrompt latin_macedonian = {};
    const std::string latin_text =
        "<think>\n"
        "bpm: 96\n"
        "duration: 30\n"
        "language: unknown\n"
        "</think>\n"
        "# Lyric\n"
        "Te sakam, zosto kade si?\n";
    if (!parse_cot_and_lyrics(latin_text, &latin_macedonian)) {
        return fail("Latin Macedonian fixture did not parse");
    }
    if (latin_macedonian.vocal_language != "mk") {
        return fail("Latin Macedonian markers were not normalized to mk");
    }

    AcePrompt misclassified_macedonian = {};
    const std::string misclassified_text =
        "<think>\n"
        "bpm: 96\n"
        "duration: 30\n"
        "language: tr\n"
        "caption: Turkish folk song with Indian influences\n"
        "</think>\n"
        "# Lyric\n"
        "Ќе те сакам, каде си?\n";
    if (!parse_cot_and_lyrics(misclassified_text, &misclassified_macedonian)) {
        return fail("Misclassified Macedonian fixture did not parse");
    }
    if (misclassified_macedonian.vocal_language != "mk") {
        return fail("Macedonian evidence did not override Turkish language");
    }
    if (misclassified_macedonian.caption.find("Macedonian") == std::string::npos ||
        misclassified_macedonian.caption.find("Turkish") != std::string::npos ||
        misclassified_macedonian.caption.find("Indian") != std::string::npos) {
        return fail("Macedonian style caption was not corrected");
    }

    AcePrompt named_macedonian = {};
    const std::string named_text =
        "<think>\n"
        "bpm: 96\n"
        "duration: 30\n"
        "language: Macedonian\n"
        "</think>\n";
    if (!parse_cot_and_lyrics(named_text, &named_macedonian)) {
        return fail("Named Macedonian fixture did not parse");
    }
    if (named_macedonian.vocal_language != "mk") {
        return fail("Named Macedonian language was not canonicalized to mk");
    }

    fprintf(stderr, "[Test] PASS\n");
    return 0;
}
