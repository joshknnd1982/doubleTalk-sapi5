// SPDX-License-Identifier: BSD-3-Clause
//
// dtalk_api.h - runtime loader for the DoubleTalk PC emulator DLL.
//
// The emulator ships as two prebuilt mingw-w64 DLLs exporting an identical
// undecorated cdecl C API: dtalk.dll (x86) and dtalk64.dll (x64). Both are
// loaded the same way, so unlike the BestSpeech wrapper this needs no 32-bit
// helper process and no pipe bridge - each architecture drives the engine
// in-process.
//
// The DLL is resolved next to whichever module contains this code, never via
// the search path, so a stray dtalk.dll elsewhere on PATH cannot be picked up.

#pragma once

#include <windows.h>
#include <stdint.h>
#include <cstddef>
#include <string>

namespace DoubleTalk {

// Layout must match dtalk.h's dtalk_index_mark exactly. Natural alignment on
// both MSVC and mingw puts value at 0 and sample_pos at 8 (sizeof 16); the
// static_assert below is the guard, since a mismatch would silently corrupt
// every bookmark position rather than fail loudly.
struct IndexMark
{
    uint8_t  value;
    uint64_t sample_pos;
};
static_assert(sizeof(IndexMark) == 16, "IndexMark must match the DLL's dtalk_index_mark");
static_assert(offsetof(IndexMark, sample_pos) == 8, "IndexMark layout mismatch");

struct dtalk_opaque;

// Loads the engine DLL once per process and resolves its entry points.
// Construction throws std::runtime_error if the DLL or the ROM is unusable;
// callers treat that as "engine unavailable" rather than retrying.
class Library
{
public:
    // module: the HMODULE whose directory holds dtalk*.dll and the ROM.
    // Pass the SAPI DLL's own instance so the engine is found next to it.
    explicit Library(HMODULE module);
    ~Library();

    Library(const Library&) = delete;
    Library& operator=(const Library&) = delete;

    [[nodiscard]] const std::wstring& directory() const noexcept { return dir_; }
    [[nodiscard]] const std::wstring& dll_path() const noexcept { return dll_path_; }
    [[nodiscard]] const std::wstring& rom_path() const noexcept { return rom_path_; }

    // Resolved entry points. Names match dtalk.h one-for-one.
    dtalk_opaque* (*create)(const void* rom, size_t rom_size) = nullptr;
    void     (*destroy)(dtalk_opaque*)                        = nullptr;
    void     (*reset)(dtalk_opaque*)                          = nullptr;
    uint32_t (*sample_rate)(const dtalk_opaque*)              = nullptr;
    void     (*queue)(dtalk_opaque*, const void*, size_t)     = nullptr;
    void     (*say)(dtalk_opaque*, const char*)               = nullptr;
    void     (*stop)(dtalk_opaque*)                           = nullptr;
    int      (*active)(dtalk_opaque*)                         = nullptr;
    uint8_t  (*lpc_status)(dtalk_opaque*)                     = nullptr;
    size_t   (*synth)(dtalk_opaque*, uint8_t*, size_t)        = nullptr;
    size_t   (*synth16)(dtalk_opaque*, int16_t*, size_t)      = nullptr;
    void     (*set_lowpass_hz)(dtalk_opaque*, uint32_t)       = nullptr;
    int      (*rate_boost_max)()                              = nullptr;
    void     (*set_rate_boost)(dtalk_opaque*, int)            = nullptr;
    int      (*get_rate_boost)(const dtalk_opaque*)           = nullptr;
    size_t   (*read_index_marks)(dtalk_opaque*, IndexMark*, size_t) = nullptr;

    // The 512KB firmware image, read once and shared by every instance.
    [[nodiscard]] const void* rom() const noexcept { return rom_.data(); }
    [[nodiscard]] size_t rom_size() const noexcept { return rom_.size(); }

private:
    HMODULE           handle_ = nullptr;
    std::wstring      dir_;
    std::wstring      dll_path_;
    std::wstring      rom_path_;
    std::string       rom_;
};

// One emulated card. Not thread-safe: the firmware is a single CPU being
// stepped, so every call for a given instance must be serialized by the owner.
class Instance
{
public:
    explicit Instance(Library& lib);
    ~Instance();

    Instance(const Instance&) = delete;
    Instance& operator=(const Instance&) = delete;

    [[nodiscard]] explicit operator bool() const noexcept { return handle_ != nullptr; }
    [[nodiscard]] uint32_t sample_rate() const noexcept { return sample_rate_; }

    void queue(const void* bytes, size_t len) { lib_.queue(handle_, bytes, len); }
    void queue(const std::string& s)          { lib_.queue(handle_, s.data(), s.size()); }
    void stop()                               { lib_.stop(handle_); }
    void reset()                              { lib_.reset(handle_); }
    [[nodiscard]] bool active()               { return lib_.active(handle_) != 0; }

    [[nodiscard]] size_t synth16(int16_t* out, size_t max_samples)
    {
        return lib_.synth16(handle_, out, max_samples);
    }

    [[nodiscard]] size_t read_index_marks(IndexMark* out, size_t max)
    {
        return lib_.read_index_marks(handle_, out, max);
    }

    void set_lowpass_hz(uint32_t hz)   { lib_.set_lowpass_hz(handle_, hz); }
    void set_rate_boost(int level)     { lib_.set_rate_boost(handle_, level); }
    [[nodiscard]] int rate_boost_max() { return lib_.rate_boost_max(); }

private:
    Library&      lib_;
    dtalk_opaque* handle_ = nullptr;
    uint32_t      sample_rate_ = 10504;
};

}
