// SPDX-License-Identifier: BSD-3-Clause
//
// dt_engine.h - synthesis on top of the raw dtalk API.
//
// Three jobs live here:
//
//  1. Building the card's command prefix from a VoiceSettings block. The
//     firmware's commands are Ctrl-A <n> <LETTER>; order matters, because nO
//     reloads the whole per-voice parameter block and would clobber anything
//     sent before it.
//
//  2. Turning the card's 10504 Hz output into whatever rate SAPI was told to
//     expect. 10504 is not a rate audio stacks generally like, so by default
//     the output is resampled to 22050.
//
//  3. Index markers. The firmware reports the exact output-sample position at
//     which each embedded marker was reached, which is what lets bookmark and
//     word-boundary events land on the right audio rather than being guessed
//     at from text offsets.

#pragma once

#include <stdint.h>
#include <string>
#include <vector>

#include "dtalk_api.h"
#include "dt_settings.h"
#include "dt_voices.h"

namespace DoubleTalk {

// Resamples mono 16-bit PCM with Catmull-Rom cubic interpolation. The card's
// output has already been low-passed to at most 5 kHz by dtalk's own
// reconstruction filter, and we only ever upsample, so there is no aliasing to
// guard against and cubic is comfortably good enough.
class Resampler
{
public:
    void configure(uint32_t in_rate, uint32_t out_rate);
    void reset();

    // Appends the resampled result of `n` input samples to `out`.
    void process(const int16_t* in, size_t n, std::vector<int16_t>& out);

    // Input-sample position -> output-sample position, for index markers.
    [[nodiscard]] double to_output(double in_pos) const noexcept
    {
        return in_pos * ratio_;
    }

    [[nodiscard]] bool passthrough() const noexcept { return passthrough_; }

private:
    double  ratio_ = 1.0;   // out_rate / in_rate
    double  step_  = 1.0;   // input samples consumed per output sample
    double  pos_   = 0.0;   // fractional read position within buf_
    bool    passthrough_ = true;
    std::vector<int16_t> buf_;  // 3 history samples + unconsumed input
};

// Builds the Ctrl-A command prefix that puts the card into the requested
// state. Returned as raw bytes ready to hand to dtalk_queue.
[[nodiscard]] std::string build_prefix(const VoiceInfo& voice,
                                       const VoiceSettings& s);

// Maps SAPI's relative rate/pitch/volume onto the card's absolute ranges,
// starting from the user's configured values.
[[nodiscard]] VoiceSettings apply_sapi_modifiers(const VoiceSettings& base,
                                                 int sapi_rate,
                                                 int sapi_pitch,
                                                 int sapi_volume);

// Strips everything the firmware cannot take literally. Bytes below 0x20 are
// commands to the card, so any control character in the source text - Ctrl-A
// above all - must never reach it.
[[nodiscard]] std::string sanitize(const std::wstring& text);

class Engine
{
public:
    explicit Engine(Library& lib);

    // Applies the global output settings. Safe to call between utterances;
    // dtalk_set_lowpass_hz is documented as safe mid-stream too.
    void apply_global(const GlobalSettings& g);

    [[nodiscard]] uint32_t output_rate() const noexcept { return output_rate_; }
    [[nodiscard]] uint32_t native_rate() const noexcept { return instance_.sample_rate(); }

    // Clears any speech in progress and queues a fresh utterance. `bytes` is
    // a complete command-plus-text string ending in CR.
    void begin(const std::string& bytes);

    // Renders the next chunk into `out` (cleared first). Returns the number of
    // OUTPUT samples produced; 0 means the utterance is finished.
    [[nodiscard]] size_t render(std::vector<int16_t>& out);

    // Dequeues index markers reached so far, converting each card sample
    // position into an offset in output samples from the start of the current
    // utterance.
    struct Mark { uint8_t value; uint64_t output_sample; };
    [[nodiscard]] size_t read_marks(Mark* out, size_t max);

    void stop();

private:
    Library&  lib_;
    Instance  instance_;
    Resampler resampler_;

    uint32_t  output_rate_ = 22050;

    // Absolute count of card samples pulled since the instance was created.
    // dtalk's own marker positions use the same origin, so tracking it here
    // lets a marker be expressed relative to the current utterance.
    uint64_t  card_pos_ = 0;
    uint64_t  stream_start_ = 0;

    std::vector<int16_t> scratch_;
};

}
