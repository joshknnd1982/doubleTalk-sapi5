// SPDX-License-Identifier: BSD-3-Clause

#include "dt_settings.h"
#include "dt_log.h"

#include <shlobj.h>
#include <algorithm>
#include <cstdio>
#include <cstdlib>

namespace DoubleTalk {

namespace {

constexpr wchar_t SECTION_GLOBAL[] = L"Global";

[[nodiscard]] int clamp_int(int v, int lo, int hi) noexcept
{
    return v < lo ? lo : (v > hi ? hi : v);
}

// Builds <known folder>\DoubleTalkSAPI\settings.ini, creating the directory.
// Returns an empty string if the folder cannot be resolved, which callers
// treat as "no settings file here".
[[nodiscard]] std::wstring settings_path_in(REFKNOWNFOLDERID folder)
{
    wchar_t* base = nullptr;
    if (FAILED(SHGetKnownFolderPath(folder, 0, nullptr, &base)) || !base) {
        return {};
    }
    std::wstring dir(base);
    CoTaskMemFree(base);

    dir += L"\\DoubleTalkSAPI";
    CreateDirectoryW(dir.c_str(), nullptr);
    return dir + L"\\settings.ini";
}

[[nodiscard]] int read_int(const wchar_t* section, const wchar_t* key,
                           int fallback, const std::wstring& file)
{
    // GetPrivateProfileIntW cannot distinguish "absent" from a value equal to
    // the fallback, but every setting here has a valid default, so absent and
    // default-valued are genuinely the same thing.
    return static_cast<int>(
        GetPrivateProfileIntW(section, key, fallback, file.c_str()));
}

void write_int(const wchar_t* section, const wchar_t* key, int value,
               const std::wstring& file)
{
    wchar_t buf[32];
    swprintf_s(buf, L"%d", value);
    WritePrivateProfileStringW(section, key, buf, file.c_str());
}

void voice_section(int index, wchar_t* out, size_t n)
{
    swprintf_s(out, n, L"Voice%d", index);
}

}

std::wstring user_settings_path()
{
    return settings_path_in(FOLDERID_LocalAppData);
}

std::wstring machine_settings_path()
{
    return settings_path_in(FOLDERID_ProgramData);
}

Store::Store()
{
    path_ = user_settings_path();
    apply_defaults();
}

void Store::apply_defaults()
{
    global_ = GlobalSettings{};
    for (int i = 0; i < VOICE_COUNT; ++i) {
        const VoicePreset& p = VOICES[i].preset;
        voices_[i] = VoiceSettings{
            p.pitch, p.articulation, p.formant, p.tone, p.expression, p.reverb,
            RATE_DEFAULT, VOLUME_DEFAULT
        };
    }
}

void Store::load()
{
    std::lock_guard<std::mutex> lock(mutex_);

    apply_defaults();

    // Prefer the per-user file; fall back to the machine-wide defaults the
    // installer drops, so a new profile starts from whatever the admin set.
    std::wstring file = path_;
    if (GetFileAttributesW(file.c_str()) == INVALID_FILE_ATTRIBUTES) {
        const std::wstring machine = machine_settings_path();
        if (!machine.empty() &&
            GetFileAttributesW(machine.c_str()) != INVALID_FILE_ATTRIBUTES) {
            file = machine;
            DT_INFO("settings: per-user file absent, seeding from machine defaults");
        } else {
            DT_INFO("settings: no file on disk, using firmware presets");
            capture_stamp();
            return;
        }
    }

    global_.filter_hz   = clamp_int(read_int(SECTION_GLOBAL, L"Filter",    global_.filter_hz,  file), 500, 5000);
    global_.rate_boost  = clamp_int(read_int(SECTION_GLOBAL, L"RateBoost", global_.rate_boost, file), 0, 16);
    global_.log_level   = clamp_int(read_int(SECTION_GLOBAL, L"LogLevel",  global_.log_level,  file), 0, 4);
    global_.output_rate = read_int(SECTION_GLOBAL, L"OutputRate", global_.output_rate, file);

    // 0 means "hand SAPI the card's native 10504 Hz"; anything else must be a
    // sane audio rate or the SAPI host will refuse the format outright.
    if (global_.output_rate != 0) {
        global_.output_rate = clamp_int(global_.output_rate, 8000, 48000);
    }

    for (int i = 0; i < VOICE_COUNT; ++i) {
        wchar_t sec[32];
        voice_section(i, sec, 32);
        VoiceSettings& v = voices_[i];
        v.pitch        = clamp_int(read_int(sec, L"Pitch",        v.pitch,        file), PITCH_MIN,   PITCH_MAX);
        v.articulation = clamp_int(read_int(sec, L"Articulation", v.articulation, file), ARTIC_MIN,   ARTIC_MAX);
        v.formant      = clamp_int(read_int(sec, L"Formant",      v.formant,      file), FORMANT_MIN, FORMANT_MAX);
        v.tone         = clamp_int(read_int(sec, L"Tone",         v.tone,         file), TONE_MIN,    TONE_MAX);
        v.expression   = clamp_int(read_int(sec, L"Expression",   v.expression,   file), EXPR_MIN,    EXPR_MAX);
        v.reverb       = clamp_int(read_int(sec, L"Reverb",       v.reverb,       file), REVERB_MIN,  REVERB_MAX);
        v.rate         = clamp_int(read_int(sec, L"Rate",         v.rate,         file), RATE_MIN,    RATE_MAX);
        v.volume       = clamp_int(read_int(sec, L"Volume",       v.volume,       file), VOLUME_MIN,  VOLUME_MAX);
    }

    log::set_level(static_cast<log::Level>(global_.log_level));
    capture_stamp();

    DT_INFO("settings: loaded from %ls (filter=%d Hz, boost=%d, outrate=%d)",
            file.c_str(), global_.filter_hz, global_.rate_boost, global_.output_rate);
}

bool Store::stamp_changed() const
{
    WIN32_FILE_ATTRIBUTE_DATA fad{};
    if (!GetFileAttributesExW(path_.c_str(), GetFileExInfoStandard, &fad)) {
        // Absent now but present before (or vice versa) still counts as a
        // change: deleting the file is a legitimate way to reset to defaults.
        return size_ != -1;
    }
    const long long sz =
        (static_cast<long long>(fad.nFileSizeHigh) << 32) | fad.nFileSizeLow;
    return sz != size_ || CompareFileTime(&fad.ftLastWriteTime, &stamp_) != 0;
}

void Store::capture_stamp()
{
    WIN32_FILE_ATTRIBUTE_DATA fad{};
    if (GetFileAttributesExW(path_.c_str(), GetFileExInfoStandard, &fad)) {
        stamp_ = fad.ftLastWriteTime;
        size_  = (static_cast<long long>(fad.nFileSizeHigh) << 32) | fad.nFileSizeLow;
    } else {
        stamp_ = FILETIME{};
        size_  = -1;
    }
}

bool Store::reload_if_changed()
{
    {
        std::lock_guard<std::mutex> lock(mutex_);
        if (!stamp_changed()) {
            return false;
        }
    }
    DT_INFO("settings: file changed on disk, reloading");
    load();
    return true;
}

bool Store::save()
{
    std::lock_guard<std::mutex> lock(mutex_);

    if (path_.empty()) {
        DT_ERROR("settings: no writable settings path");
        return false;
    }

    write_int(SECTION_GLOBAL, L"Filter",     global_.filter_hz,   path_);
    write_int(SECTION_GLOBAL, L"RateBoost",  global_.rate_boost,  path_);
    write_int(SECTION_GLOBAL, L"LogLevel",   global_.log_level,   path_);
    write_int(SECTION_GLOBAL, L"OutputRate", global_.output_rate, path_);

    for (int i = 0; i < VOICE_COUNT; ++i) {
        wchar_t sec[32];
        voice_section(i, sec, 32);
        const VoiceSettings& v = voices_[i];
        write_int(sec, L"Pitch",        v.pitch,        path_);
        write_int(sec, L"Articulation", v.articulation, path_);
        write_int(sec, L"Formant",      v.formant,      path_);
        write_int(sec, L"Tone",         v.tone,         path_);
        write_int(sec, L"Expression",   v.expression,   path_);
        write_int(sec, L"Reverb",       v.reverb,       path_);
        write_int(sec, L"Rate",         v.rate,         path_);
        write_int(sec, L"Volume",       v.volume,       path_);
    }

    // Flush the profile cache so the timestamp we capture matches what a
    // reader will see. Without this the write can still be sitting in the
    // profile API cache and a concurrent engine would reload stale values.
    WritePrivateProfileStringW(nullptr, nullptr, nullptr, path_.c_str());

    capture_stamp();
    DT_INFO("settings: saved to %ls", path_.c_str());
    return true;
}

GlobalSettings Store::global() const
{
    std::lock_guard<std::mutex> lock(mutex_);
    return global_;
}

void Store::set_global(const GlobalSettings& g)
{
    std::lock_guard<std::mutex> lock(mutex_);
    global_ = g;
    global_.filter_hz = clamp_int(global_.filter_hz, 500, 5000);
    global_.log_level = clamp_int(global_.log_level, 0, 4);
}

VoiceSettings Store::voice(int index) const
{
    std::lock_guard<std::mutex> lock(mutex_);
    if (index < 0 || index >= VOICE_COUNT) {
        index = 0;
    }
    return voices_[index];
}

void Store::set_voice(int index, const VoiceSettings& v)
{
    if (index < 0 || index >= VOICE_COUNT) {
        return;
    }
    std::lock_guard<std::mutex> lock(mutex_);
    VoiceSettings s = v;
    s.pitch        = clamp_int(s.pitch,        PITCH_MIN,   PITCH_MAX);
    s.articulation = clamp_int(s.articulation, ARTIC_MIN,   ARTIC_MAX);
    s.formant      = clamp_int(s.formant,      FORMANT_MIN, FORMANT_MAX);
    s.tone         = clamp_int(s.tone,         TONE_MIN,    TONE_MAX);
    s.expression   = clamp_int(s.expression,   EXPR_MIN,    EXPR_MAX);
    s.reverb       = clamp_int(s.reverb,       REVERB_MIN,  REVERB_MAX);
    s.rate         = clamp_int(s.rate,         RATE_MIN,    RATE_MAX);
    s.volume       = clamp_int(s.volume,       VOLUME_MIN,  VOLUME_MAX);
    voices_[index] = s;
}

void Store::reset_voice(int index)
{
    if (index < 0 || index >= VOICE_COUNT) {
        return;
    }
    std::lock_guard<std::mutex> lock(mutex_);
    const VoicePreset& p = VOICES[index].preset;
    voices_[index] = VoiceSettings{
        p.pitch, p.articulation, p.formant, p.tone, p.expression, p.reverb,
        RATE_DEFAULT, VOLUME_DEFAULT
    };
}

void Store::reset_all()
{
    std::lock_guard<std::mutex> lock(mutex_);
    apply_defaults();
}

Store& shared()
{
    // Function-local static: thread-safe initialization, and constructed on
    // first use rather than at DLL attach, where COM is not yet usable.
    static Store store;
    return store;
}

}
