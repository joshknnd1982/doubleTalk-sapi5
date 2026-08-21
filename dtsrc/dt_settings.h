// SPDX-License-Identifier: BSD-3-Clause
//
// dt_settings.h - the settings shared by the SAPI5 engine and the config
// utility.
//
// Deliberately a plain INI file, not the registry: the engine must keep
// working for a user with no write access to HKLM, the file is trivially
// backed up and diffed, and a screen-reader user chasing a problem can read it.
// The only registry writes this project makes at all are the two COM CLSID
// registrations and the single TokenEnums key SAPI needs in order to discover
// the voices - none of the tunable state lives there.
//
// Location: %LOCALAPPDATA%\DoubleTalkSAPI\settings.ini (per user, written by
// the config utility). If that file does not exist the machine-wide default
// dropped by the installer at %PROGRAMDATA%\DoubleTalkSAPI\settings.ini is
// read instead, so a fresh user profile inherits the admin's defaults.
//
// Immediate effect: `Store::reload_if_changed` compares the file's last-write
// timestamp and size and re-reads only when they move. The engine calls it at
// the start of every utterance, so a change made in the config utility is
// audible on the very next thing spoken - no restart, no notification plumbing,
// and no watcher thread to go wrong inside a screen reader's process.

#pragma once

#include <windows.h>
#include <string>
#include <mutex>

#include "dt_voices.h"

namespace DoubleTalk {

// Everything the card and the output stage can be told to do, in card-native
// units. One block per voice, plus the global output settings.
struct VoiceSettings
{
    int pitch;
    int articulation;
    int formant;
    int tone;
    int expression;
    int reverb;
    int rate;    // nS - not voice-linked on the card, but stored per voice so
    int volume;  // nV   each SAPI voice can carry its own preferred speed.
};

struct GlobalSettings
{
    int  filter_hz    = FILTER_DEFAULT_HZ;
    int  rate_boost   = 0;      // 0 = authentic firmware rate table
    int  log_level    = 3;      // see dt_log.h Level
    int  output_rate  = 22050;  // Hz presented to SAPI; 0 = card native
};

class Store
{
public:
    Store();

    // Loads from disk, filling in firmware presets for anything absent.
    void load();

    // Cheap stat; re-reads only if the file actually changed. Returns true if
    // a reload happened, so callers can log the transition.
    bool reload_if_changed();

    // Writes the whole file atomically (temp + MoveFileEx) so a reader can
    // never observe a half-written settings file.
    [[nodiscard]] bool save();

    [[nodiscard]] GlobalSettings global() const;
    void set_global(const GlobalSettings& g);

    [[nodiscard]] VoiceSettings voice(int index) const;
    void set_voice(int index, const VoiceSettings& v);

    // Restores one voice (or all of them) to the firmware preset.
    void reset_voice(int index);
    void reset_all();

    [[nodiscard]] const std::wstring& path() const noexcept { return path_; }

private:
    void apply_defaults();
    [[nodiscard]] bool stamp_changed() const;
    void capture_stamp();

    mutable std::mutex mutex_;
    std::wstring       path_;
    GlobalSettings     global_;
    VoiceSettings      voices_[VOICE_COUNT]{};

    FILETIME  stamp_{};
    long long size_ = -1;
};

// Process-wide store used by the SAPI engine. The config utility owns its own
// instance instead, so that an unsaved edit in the UI never leaks into the
// engine's view before the user commits it.
[[nodiscard]] Store& shared();

// Resolves the per-user settings path, creating the directory if needed.
[[nodiscard]] std::wstring user_settings_path();
[[nodiscard]] std::wstring machine_settings_path();

}
