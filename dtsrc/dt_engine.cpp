// SPDX-License-Identifier: BSD-3-Clause

#include "dt_engine.h"
#include "dt_log.h"

#include <algorithm>
#include <cmath>
#include <cstdio>

namespace DoubleTalk {

namespace {

// Card samples pulled per dtalk_synth16 call. 2048 at 10504 Hz is ~195 ms,
// small enough that an abort from the SAPI host is acted on promptly and large
// enough that the per-call overhead is irrelevant.
constexpr size_t CHUNK_SAMPLES = 2048;

constexpr char CMD = '\x01';   // the card's command introducer (Ctrl-A)

[[nodiscard]] int clamp_int(int v, int lo, int hi) noexcept
{
    return v < lo ? lo : (v > hi ? hi : v);
}

void append_cmd(std::string& s, int value, char letter)
{
    char buf[16];
    const int n = snprintf(buf, sizeof(buf), "%c%d%c", CMD, value, letter);
    if (n > 0) {
        s.append(buf, static_cast<size_t>(n));
    }
}

}

// ---------------------------------------------------------------- Resampler

void Resampler::configure(uint32_t in_rate, uint32_t out_rate)
{
    if (in_rate == 0) {
        in_rate = 10504;
    }
    // out_rate 0 is the documented "give SAPI the card's native rate" setting.
    if (out_rate == 0 || out_rate == in_rate) {
        passthrough_ = true;
        ratio_ = 1.0;
        step_  = 1.0;
    } else {
        passthrough_ = false;
        ratio_ = static_cast<double>(out_rate) / static_cast<double>(in_rate);
        step_  = static_cast<double>(in_rate) / static_cast<double>(out_rate);
    }
    reset();
}

void Resampler::reset()
{
    buf_.clear();
    // Three leading zero samples give the cubic kernel the history it needs
    // for the very first real sample without a special case.
    buf_.assign(3, 0);
    pos_ = 1.0;
}

void Resampler::process(const int16_t* in, size_t n, std::vector<int16_t>& out)
{
    if (passthrough_) {
        out.insert(out.end(), in, in + n);
        return;
    }

    buf_.insert(buf_.end(), in, in + n);

    // Catmull-Rom needs samples at floor(pos)-1 .. floor(pos)+2, so the last
    // position we can evaluate is buf_.size() - 3.
    while (pos_ + 2.0 < static_cast<double>(buf_.size())) {
        const size_t i = static_cast<size_t>(pos_);
        const double t = pos_ - static_cast<double>(i);

        const double y0 = buf_[i - 1];
        const double y1 = buf_[i];
        const double y2 = buf_[i + 1];
        const double y3 = buf_[i + 2];

        const double a = 2.0 * y1;
        const double b = -y0 + y2;
        const double c = 2.0 * y0 - 5.0 * y1 + 4.0 * y2 - y3;
        const double d = -y0 + 3.0 * y1 - 3.0 * y2 + y3;

        double v = 0.5 * (a + b * t + c * t * t + d * t * t * t);

        // Cubic interpolation overshoots on steep transitions; clamp rather
        // than letting it wrap into a loud click.
        v = v < -32768.0 ? -32768.0 : (v > 32767.0 ? 32767.0 : v);
        out.push_back(static_cast<int16_t>(std::lround(v)));

        pos_ += step_;
    }

    // Drop input we can no longer need, keeping one sample of history behind
    // the read position for the next call's kernel.
    const size_t consumed = static_cast<size_t>(pos_) - 1;
    if (consumed > 0) {
        buf_.erase(buf_.begin(), buf_.begin() + static_cast<ptrdiff_t>(consumed));
        pos_ -= static_cast<double>(consumed);
    }
}

// ------------------------------------------------------------ command build

std::string build_prefix(const VoiceInfo& voice, const VoiceSettings& s)
{
    std::string p;
    p.reserve(64);

    // nO first and unconditionally: it reloads the card's entire per-voice
    // parameter block, so anything emitted before it would be discarded.
    append_cmd(p, voice.card_id, 'O');

    // Number mode 14B. Left at its default the firmware reads "007" as the
    // value seven and swallows the leading zeros, which is wrong when the
    // digits on screen are the content. 14B pronounces leading zeros while
    // still reading ordinary numbers as numbers. It is sticky on the card, but
    // re-sent every utterance so a card reset cannot silently drop it.
    p += CMD;
    p += "14B";

    // Everything else is emitted absolutely. The values default to this
    // voice's firmware preset, so a user who has changed nothing gets exactly
    // the preset - but stating them explicitly means the card cannot drift out
    // of sync with what the settings file says.
    append_cmd(p, clamp_int(s.rate,         RATE_MIN,    RATE_MAX),    'S');
    append_cmd(p, clamp_int(s.pitch,        PITCH_MIN,   PITCH_MAX),   'P');
    append_cmd(p, clamp_int(s.volume,       VOLUME_MIN,  VOLUME_MAX),  'V');
    append_cmd(p, clamp_int(s.tone,         TONE_MIN,    TONE_MAX),    'X');
    append_cmd(p, clamp_int(s.articulation, ARTIC_MIN,   ARTIC_MAX),   'A');
    append_cmd(p, clamp_int(s.expression,   EXPR_MIN,    EXPR_MAX),    'E');
    append_cmd(p, clamp_int(s.formant,      FORMANT_MIN, FORMANT_MAX), 'F');
    append_cmd(p, clamp_int(s.reverb,       REVERB_MIN,  REVERB_MAX),  'R');

    return p;
}

VoiceSettings apply_sapi_modifiers(const VoiceSettings& base,
                                   int sapi_rate, int sapi_pitch, int sapi_volume)
{
    VoiceSettings s = base;

    // SAPI rate is -10..+10 around the user's configured speed. The card has
    // ten speed steps, so a full swing of the SAPI control covers most of the
    // range without ever pinning it at one end from the default.
    s.rate = clamp_int(
        static_cast<int>(std::lround(base.rate + sapi_rate * 0.45)),
        RATE_MIN, RATE_MAX);

    // SAPI pitch is also -10..+10; the card's nP is a much finer 0-99, so each
    // SAPI step is five card steps.
    s.pitch = clamp_int(base.pitch + sapi_pitch * 5, PITCH_MIN, PITCH_MAX);

    // SAPI volume is a 0-100 percentage and scales whatever the user set,
    // rather than replacing it.
    s.volume = clamp_int(
        static_cast<int>(std::lround(base.volume * (sapi_volume / 100.0))),
        VOLUME_MIN, VOLUME_MAX);

    return s;
}

std::string sanitize(const std::wstring& text)
{
    std::string out;
    out.reserve(text.size());

    for (const wchar_t wc : text) {
        // The firmware speaks 7-bit ASCII and treats every byte below 0x20 as
        // a command. Anything else becomes a space, which keeps word spacing
        // intact instead of running words together.
        if (wc >= 0x20 && wc <= 0x7e) {
            out.push_back(static_cast<char>(wc));
            continue;
        }

        // A few punctuation characters are common enough in real text that
        // mapping them to their ASCII equivalents is worth the special case;
        // otherwise a smart-quoted sentence loses its apostrophes.
        switch (wc) {
            case 0x2018: case 0x2019: out.push_back('\''); break;
            case 0x201c: case 0x201d: out.push_back('"');  break;
            case 0x2013: case 0x2014: out.push_back('-');  break;
            case 0x2026: out.append("...");                break;
            case 0x00a0: out.push_back(' ');               break;
            default:     out.push_back(' ');               break;
        }
    }
    return out;
}

// -------------------------------------------------------------------- Engine

Engine::Engine(Library& lib)
    : lib_(lib)
    , instance_(lib)
{
    resampler_.configure(instance_.sample_rate(), output_rate_);
    scratch_.resize(CHUNK_SAMPLES);
}

void Engine::apply_global(const GlobalSettings& g)
{
    instance_.set_lowpass_hz(static_cast<uint32_t>(g.filter_hz));

    const int max_boost = instance_.rate_boost_max();
    const int boost = clamp_int(g.rate_boost, 0, max_boost);
    instance_.set_rate_boost(boost);

    const uint32_t want = g.output_rate == 0
        ? instance_.sample_rate()
        : static_cast<uint32_t>(g.output_rate);

    if (want != output_rate_) {
        output_rate_ = want;
        resampler_.configure(instance_.sample_rate(), output_rate_);
        DT_INFO("engine: output rate now %u Hz", output_rate_);
    }

    DT_DEBUG("engine: filter=%d Hz boost=%d/%d", g.filter_hz, boost, max_boost);
}

void Engine::begin(const std::string& bytes)
{
    // Stop first: anything still queued from a cancelled utterance would
    // otherwise be spoken ahead of this one.
    instance_.stop();
    resampler_.reset();
    stream_start_ = card_pos_;

    instance_.queue(bytes.data(), bytes.size());
    DT_DEBUG("engine: queued %zu bytes, stream starts at card sample %llu",
             bytes.size(), static_cast<unsigned long long>(stream_start_));
}

size_t Engine::render(std::vector<int16_t>& out)
{
    out.clear();

    const size_t n = instance_.synth16(scratch_.data(), CHUNK_SAMPLES);
    if (n == 0) {
        return 0;
    }
    card_pos_ += n;

    resampler_.process(scratch_.data(), n, out);
    return out.size();
}

size_t Engine::read_marks(Mark* out, size_t max)
{
    IndexMark raw[32];
    const size_t want = (std::min)(max, sizeof(raw) / sizeof(raw[0]));
    const size_t got = instance_.read_index_marks(raw, want);

    for (size_t i = 0; i < got; ++i) {
        // Marker positions are absolute card samples since instance creation;
        // rebase onto this utterance, then convert to the output rate.
        const uint64_t abs_pos = raw[i].sample_pos;
        const double rel = abs_pos > stream_start_
            ? static_cast<double>(abs_pos - stream_start_)
            : 0.0;

        out[i].value = raw[i].value;
        out[i].output_sample =
            static_cast<uint64_t>(resampler_.to_output(rel));
    }
    return got;
}

void Engine::stop()
{
    instance_.stop();
    resampler_.reset();
    DT_DEBUG("engine: stopped");
}

}
