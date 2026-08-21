// SPDX-License-Identifier: BSD-3-Clause
//
// dt_render.cpp - console renderer for the DoubleTalk PC engine.
//
// Drives the exact code path the SAPI5 engine uses (dtalk_api -> dt_engine),
// so a problem reproduced here is in the engine and a problem that only shows
// up through SAPI is in the COM plumbing.
//
//   dt_render --list
//   dt_render --voice 3 --out pete.wav "Hello there"
//   dt_render --all-voices --outdir samples
//   dt_render --self-test

#include <windows.h>
#include <cstdio>
#include <cstring>
#include <string>
#include <vector>

#include "dt_engine.h"
#include "dt_log.h"
#include "dt_settings.h"
#include "dt_voices.h"
#include "dtalk_api.h"

using namespace DoubleTalk;

namespace {

bool write_wav(const std::wstring& path, const std::vector<int16_t>& pcm,
               uint32_t rate)
{
    FILE* f = nullptr;
    if (_wfopen_s(&f, path.c_str(), L"wb") != 0 || !f) {
        fwprintf(stderr, L"cannot open %s for writing\n", path.c_str());
        return false;
    }

    const uint32_t data_bytes = static_cast<uint32_t>(pcm.size() * sizeof(int16_t));
    const uint32_t byte_rate  = rate * 2;

    auto u32 = [&](uint32_t v) { fwrite(&v, 4, 1, f); };
    auto u16 = [&](uint16_t v) { fwrite(&v, 2, 1, f); };

    fwrite("RIFF", 1, 4, f);
    u32(36 + data_bytes);
    fwrite("WAVEfmt ", 1, 8, f);
    u32(16);            // PCM fmt chunk size
    u16(1);             // WAVE_FORMAT_PCM
    u16(1);             // mono
    u32(rate);
    u32(byte_rate);
    u16(2);             // block align
    u16(16);            // bits per sample
    fwrite("data", 1, 4, f);
    u32(data_bytes);
    fwrite(pcm.data(), 1, data_bytes, f);
    fclose(f);
    return true;
}

// Renders one complete utterance, draining until the engine goes idle.
std::vector<int16_t> render_all(Engine& engine, const std::string& bytes)
{
    engine.begin(bytes);

    std::vector<int16_t> pcm;
    std::vector<int16_t> chunk;
    // Safety stop: a malformed command could in principle leave the firmware
    // talking forever, and a CLI that never returns is worse than a truncated
    // file.
    const size_t cap = static_cast<size_t>(engine.output_rate()) * 300;

    while (engine.render(chunk) > 0) {
        pcm.insert(pcm.end(), chunk.begin(), chunk.end());
        if (pcm.size() > cap) {
            fwprintf(stderr, L"warning: hit the 300 s render cap\n");
            break;
        }
    }
    return pcm;
}

std::wstring widen(const std::string& s)
{
    if (s.empty()) {
        return {};
    }
    const int n = MultiByteToWideChar(CP_UTF8, 0, s.c_str(),
                                      static_cast<int>(s.size()), nullptr, 0);
    std::wstring w(static_cast<size_t>(n), L'\0');
    MultiByteToWideChar(CP_UTF8, 0, s.c_str(), static_cast<int>(s.size()),
                        w.data(), n);
    return w;
}

int usage()
{
    wprintf(
        L"dt_render - DoubleTalk PC offline renderer\n\n"
        L"  --list                  list voices and their firmware presets\n"
        L"  --voice N               voice 0-7 (default 0)\n"
        L"  --rate N                nS speed 0-9\n"
        L"  --pitch N               nP pitch 0-99\n"
        L"  --volume N              nV volume 0-9\n"
        L"  --articulation N        nA 0-9\n"
        L"  --expression N          nE 0-9\n"
        L"  --formant N             nF 0-9\n"
        L"  --reverb N              nR 0-9\n"
        L"  --tone N                nX 0-2 (bass/normal/treble)\n"
        L"  --filter HZ             reconstruction low-pass 500-5000\n"
        L"  --boost N               rate boost level (0 = authentic)\n"
        L"  --outrate HZ            output sample rate (0 = card native)\n"
        L"  --out FILE.wav          write to FILE.wav\n"
        L"  --outdir DIR            directory for --all-voices\n"
        L"  --all-voices            render the text once per voice\n"
        L"  --self-test             verify engine, ROM and every voice\n"
        L"  --log-level N           0 off .. 4 debug\n");
    return 2;
}

}

int wmain(int argc, wchar_t** argv)
{
    log::init(L"render");
    log::set_level(log::Level::Info);

    std::string text = "Hello. This is the Double Talk P C speech synthesizer.";
    std::wstring out_path;
    std::wstring out_dir = L".";
    int voice = 0;
    bool all_voices = false;
    bool self_test = false;
    bool list = false;

    Store& store = shared();
    store.load();
    GlobalSettings g = store.global();
    VoiceSettings overrides{ -1, -1, -1, -1, -1, -1, -1, -1 };

    for (int i = 1; i < argc; ++i) {
        const std::wstring a = argv[i];
        auto next_int = [&](int& dest) {
            if (i + 1 < argc) {
                dest = _wtoi(argv[++i]);
            }
        };

        if      (a == L"--list")         { list = true; }
        else if (a == L"--all-voices")   { all_voices = true; }
        else if (a == L"--self-test")    { self_test = true; }
        else if (a == L"--voice")        { next_int(voice); }
        else if (a == L"--rate")         { next_int(overrides.rate); }
        else if (a == L"--pitch")        { next_int(overrides.pitch); }
        else if (a == L"--volume")       { next_int(overrides.volume); }
        else if (a == L"--articulation") { next_int(overrides.articulation); }
        else if (a == L"--expression")   { next_int(overrides.expression); }
        else if (a == L"--formant")      { next_int(overrides.formant); }
        else if (a == L"--reverb")       { next_int(overrides.reverb); }
        else if (a == L"--tone")         { next_int(overrides.tone); }
        else if (a == L"--filter")       { next_int(g.filter_hz); }
        else if (a == L"--boost")        { next_int(g.rate_boost); }
        else if (a == L"--outrate")      { next_int(g.output_rate); }
        else if (a == L"--log-level")    { next_int(g.log_level); }
        else if (a == L"--out")          { if (i + 1 < argc) out_path = argv[++i]; }
        else if (a == L"--outdir")       { if (i + 1 < argc) out_dir  = argv[++i]; }
        else if (a == L"--help" || a == L"-h") { return usage(); }
        else if (!a.empty() && a[0] == L'-') {
            fwprintf(stderr, L"unknown option: %s\n", a.c_str());
            return usage();
        }
        else {
            // Bare argument: the text to speak.
            char buf[4096];
            const int n = WideCharToMultiByte(CP_UTF8, 0, a.c_str(), -1,
                                              buf, sizeof(buf), nullptr, nullptr);
            if (n > 0) {
                text.assign(buf);
            }
        }
    }

    log::set_level(static_cast<log::Level>(g.log_level));

    if (list) {
        wprintf(L"%-14s %3s %6s | %5s %5s %7s %4s %4s %6s\n",
                L"name", L"nO", L"preset", L"pitch", L"artic", L"formant",
                L"tone", L"expr", L"reverb");
        for (int i = 0; i < VOICE_COUNT; ++i) {
            const VoiceInfo& v = VOICES[i];
            wprintf(L"%-14S %3d %6d | %5d %5d %7d %4d %4d %6d%s\n",
                    v.name, v.card_id, v.preset_index,
                    v.preset.pitch, v.preset.articulation, v.preset.formant,
                    v.preset.tone, v.preset.expression, v.preset.reverb,
                    v.preset_index != i ? L"   (alias)" : L"");
        }
        return 0;
    }

    try {
        Library lib(GetModuleHandleW(nullptr));
        Engine engine(lib);
        engine.apply_global(g);

        auto settings_for = [&](int index) {
            VoiceSettings s = store.voice(index);
            // Any explicitly supplied switch wins over the stored value.
            if (overrides.pitch        >= 0) s.pitch        = overrides.pitch;
            if (overrides.articulation >= 0) s.articulation = overrides.articulation;
            if (overrides.formant      >= 0) s.formant      = overrides.formant;
            if (overrides.tone         >= 0) s.tone         = overrides.tone;
            if (overrides.expression   >= 0) s.expression   = overrides.expression;
            if (overrides.reverb       >= 0) s.reverb       = overrides.reverb;
            if (overrides.rate         >= 0) s.rate         = overrides.rate;
            if (overrides.volume       >= 0) s.volume       = overrides.volume;
            return s;
        };

        if (self_test) {
            wprintf(L"engine DLL : %s\n", lib.dll_path().c_str());
            wprintf(L"ROM        : %s (%zu bytes)\n",
                    lib.rom_path().c_str(), lib.rom_size());
            wprintf(L"native rate: %u Hz\n", engine.native_rate());
            wprintf(L"output rate: %u Hz\n", engine.output_rate());
            wprintf(L"log file   : %s\n\n", log::path().c_str());

            int failures = 0;
            for (int i = 0; i < VOICE_COUNT; ++i) {
                const std::string bytes =
                    build_prefix(VOICES[i], settings_for(i)) +
                    "The quick brown fox jumps over the lazy dog.\r";
                const std::vector<int16_t> pcm = render_all(engine, bytes);

                const double seconds =
                    static_cast<double>(pcm.size()) / engine.output_rate();
                const bool ok = pcm.size() > 1000;
                if (!ok) {
                    ++failures;
                }
                wprintf(L"  voice %d %-14S %8zu samples  %5.2f s  %s\n",
                        i, VOICES[i].name, pcm.size(), seconds,
                        ok ? L"OK" : L"FAILED");
            }
            wprintf(L"\n%s\n", failures == 0
                    ? L"self-test PASSED" : L"self-test FAILED");
            return failures == 0 ? 0 : 1;
        }

        if (all_voices) {
            CreateDirectoryW(out_dir.c_str(), nullptr);
            for (int i = 0; i < VOICE_COUNT; ++i) {
                const std::string bytes =
                    build_prefix(VOICES[i], settings_for(i)) + text + "\r";
                const std::vector<int16_t> pcm = render_all(engine, bytes);

                std::wstring name = out_dir + L"\\voice" +
                    std::to_wstring(i) + L"_" + widen(VOICES[i].name) + L".wav";
                for (wchar_t& c : name) {
                    if (c == L' ') {
                        c = L'_';
                    }
                }
                if (write_wav(name, pcm, engine.output_rate())) {
                    wprintf(L"%s  (%.2f s)\n", name.c_str(),
                            static_cast<double>(pcm.size()) / engine.output_rate());
                }
            }
            return 0;
        }

        if (voice < 0 || voice >= VOICE_COUNT) {
            fwprintf(stderr, L"voice must be 0-%d\n", VOICE_COUNT - 1);
            return 2;
        }

        const std::string bytes =
            build_prefix(VOICES[voice], settings_for(voice)) + text + "\r";
        const std::vector<int16_t> pcm = render_all(engine, bytes);

        if (out_path.empty()) {
            wprintf(L"rendered %zu samples (%.2f s) at %u Hz; "
                    L"pass --out FILE.wav to save\n",
                    pcm.size(),
                    static_cast<double>(pcm.size()) / engine.output_rate(),
                    engine.output_rate());
            return 0;
        }
        if (!write_wav(out_path, pcm, engine.output_rate())) {
            return 1;
        }
        wprintf(L"%s  (%.2f s, %u Hz)\n", out_path.c_str(),
                static_cast<double>(pcm.size()) / engine.output_rate(),
                engine.output_rate());
        return 0;
    }
    catch (const std::exception& e) {
        fwprintf(stderr, L"error: %S\n", e.what());
        fwprintf(stderr, L"see the log at %s\n", log::path().c_str());
        return 1;
    }
}
