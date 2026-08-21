// SPDX-License-Identifier: BSD-3-Clause
//
// dt_log.h - file logging for the DoubleTalk SAPI5 engine and config utility.
//
// Unlike the BestSpeech wrapper's debug_log.h (compiled out by default via
// ENABLE_DEBUG_LOG 0), logging here is always compiled in and controlled at
// runtime, because the people most likely to hit a bug are screen-reader users
// who cannot be asked to rebuild the DLL to get diagnostics.
//
// Log level is read from the shared settings file (LogLevel=0..4); the default
// is Info. Level 0 disables logging entirely and skips the file open, so the
// cost when off is a single relaxed atomic load.
//
// Writes go to %LOCALAPPDATA%\DoubleTalkSAPI\logs\, one file per architecture
// and process, so a 32-bit host and a 64-bit host logging at once never
// interleave into the same file. Files are size-capped and rotated once.

#pragma once

#include <windows.h>
#include <atomic>
#include <string>

namespace DoubleTalk {
namespace log {

enum class Level : int
{
    Off   = 0,
    Error = 1,
    Warn  = 2,
    Info  = 3,
    Debug = 4,
};

// Called once at DLL attach / app start. `tag` becomes part of the filename
// (e.g. "sapi", "config") so different components stay in separate files.
void init(const wchar_t* tag);
void shutdown();

void set_level(Level level) noexcept;
[[nodiscard]] Level level() noexcept;

// Returns the active log file path, for the config utility's "open log" button.
[[nodiscard]] std::wstring path();

// Never call directly - use the macros, which skip formatting work when the
// level is disabled.
void write(Level level, const char* fmt, ...);

[[nodiscard]] inline bool enabled(Level l) noexcept
{
    return static_cast<int>(l) <= static_cast<int>(level());
}

}
}

#define DT_LOG(lvl, ...)                                                    \
    do {                                                                    \
        if (::DoubleTalk::log::enabled(lvl)) {                              \
            ::DoubleTalk::log::write(lvl, __VA_ARGS__);                     \
        }                                                                   \
    } while (0)

#define DT_ERROR(...) DT_LOG(::DoubleTalk::log::Level::Error, __VA_ARGS__)
#define DT_WARN(...)  DT_LOG(::DoubleTalk::log::Level::Warn,  __VA_ARGS__)
#define DT_INFO(...)  DT_LOG(::DoubleTalk::log::Level::Info,  __VA_ARGS__)
#define DT_DEBUG(...) DT_LOG(::DoubleTalk::log::Level::Debug, __VA_ARGS__)
