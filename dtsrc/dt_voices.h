// SPDX-License-Identifier: BSD-3-Clause
//
// dt_voices.h - the DoubleTalk PC voice table and its firmware presets.
//
// The card documents eight voice names (manual Table 1) selected with the nO
// command. The firmware behind them has only FIVE distinct presets: nO is
// taken modulo 5, so voices 5/6/7 (Biff/Skip/Robo Robert) render byte-for-byte
// identically to 0/1/2 (Perfect Paul/Vader/Big Bob) at their defaults. This
// was verified by rendering the same sentence on a freshly created emulator
// instance per voice and comparing the PCM.
//
// All eight are still exposed as SAPI5 voice tokens: they are the names the
// card documents, and once a user moves any of the six per-voice parameters
// off its preset the aliases do diverge. Callers that care about the
// distinction can consult `preset_index`.

#pragma once

#include <stddef.h>

namespace DoubleTalk {

// Card-native per-voice parameter block loaded by the nO command. Ranges are
// the firmware's real ones, established by sweeping each command against the
// ROM: everything is 0-9 except pitch (0-99) and tone (0-2).
struct VoicePreset
{
    int pitch;         // nP  0-99
    int articulation;  // nA  0-9
    int formant;       // nF  0-9
    int tone;          // nX  0-2  (0 bass, 1 normal, 2 treble)
    int expression;    // nE  0-9
    int reverb;        // nR  0-9
};

struct VoiceInfo
{
    const char*  name;
    int          card_id;       // the value sent as nO
    int          preset_index;  // 0-4; equal for aliased voices
    VoicePreset  preset;
};

// Presets are ROM constants, read out of the card's settings block with the
// upstream project's `settingsmap` diagnostic and re-confirmed here by
// rendering each voice.
inline constexpr int VOICE_COUNT = 8;

inline constexpr VoiceInfo VOICES[VOICE_COUNT] = {
    { "Perfect Paul", 0, 0, { 50, 5, 5, 1, 5, 0 } },
    { "Vader",        1, 1, { 30, 4, 5, 1, 7, 2 } },
    { "Big Bob",      2, 2, { 40, 4, 1, 0, 6, 0 } },
    { "Precise Pete", 3, 3, { 60, 8, 6, 2, 4, 0 } },
    { "Ricochet",     4, 4, { 40, 5, 2, 1, 5, 6 } },
    { "Biff",         5, 0, { 50, 5, 5, 1, 5, 0 } },  // = Perfect Paul
    { "Skip",         6, 1, { 30, 4, 5, 1, 7, 2 } },  // = Vader
    { "Robo Robert",  7, 2, { 40, 4, 1, 0, 6, 0 } },  // = Big Bob
};

// Firmware limits, all confirmed against the ROM rather than taken from the
// later RC8660 datasheet (whose ranges differ - it allows nS 0-13 and eleven
// voices, neither of which this card's firmware implements).
inline constexpr int RATE_MIN = 0,  RATE_MAX = 9;      // nS
inline constexpr int PITCH_MIN = 0, PITCH_MAX = 99;    // nP
inline constexpr int VOLUME_MIN = 0, VOLUME_MAX = 9;   // nV
inline constexpr int ARTIC_MIN = 0, ARTIC_MAX = 9;     // nA
inline constexpr int EXPR_MIN = 0,  EXPR_MAX = 9;      // nE
inline constexpr int FORMANT_MIN = 0, FORMANT_MAX = 9; // nF
inline constexpr int REVERB_MIN = 0, REVERB_MAX = 9;   // nR
inline constexpr int TONE_MIN = 0,  TONE_MAX = 2;      // nX

// Card defaults for the two settings nO does NOT reload (see dt_engine.cpp).
inline constexpr int RATE_DEFAULT = 5;
inline constexpr int VOLUME_DEFAULT = 9;

// Reconstruction low-pass corners offered by the host-side output stage. This
// is DSP in dtalk.dll, not a card command: dtalk_set_lowpass_hz clamps to
// 500..5000 because the stage runs at 10504 Hz and the biquad goes unstable
// above its 5252 Hz Nyquist.
struct FilterOption
{
    int          hz;
    const char*  label;
};

inline constexpr int FILTER_COUNT = 5;
inline constexpr FilterOption FILTERS[FILTER_COUNT] = {
    { 2000, "Muffled (2 kHz)"    },
    { 3000, "Classic (3 kHz)"    },
    { 3800, "Default (3.8 kHz)"  },
    { 4800, "Wide (4.8 kHz)"     },
    { 5000, "Widest (5 kHz)"     },
};
inline constexpr int FILTER_DEFAULT_HZ = 3800;

inline constexpr const char* TONE_LABELS[3] = { "Bass", "Normal", "Treble" };

[[nodiscard]] inline const VoiceInfo* find_voice(const char* name) noexcept
{
    if (!name) {
        return nullptr;
    }
    for (int i = 0; i < VOICE_COUNT; ++i) {
        const char* a = VOICES[i].name;
        const char* b = name;
        while (*a && *b) {
            const char ca = (*a >= 'A' && *a <= 'Z') ? static_cast<char>(*a + 32) : *a;
            const char cb = (*b >= 'A' && *b <= 'Z') ? static_cast<char>(*b + 32) : *b;
            if (ca != cb) {
                break;
            }
            ++a; ++b;
        }
        if (!*a && !*b) {
            return &VOICES[i];
        }
    }
    return nullptr;
}

}
