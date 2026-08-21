// SPDX-License-Identifier: BSD-3-Clause

#include "dtalk_api.h"
#include "dt_log.h"

#include <algorithm>
#include <stdexcept>
#include <string>
#include <vector>

namespace DoubleTalk {

namespace {

// Directory containing `module`, with the trailing backslash.
std::wstring module_directory(HMODULE module)
{
    std::vector<wchar_t> buf(MAX_PATH);
    for (;;) {
        const DWORD n = GetModuleFileNameW(module, buf.data(),
                                           static_cast<DWORD>(buf.size()));
        if (n == 0) {
            throw std::runtime_error("GetModuleFileNameW failed");
        }
        // Pre-Windows-10 returns the truncated name with ERROR_INSUFFICIENT_
        // BUFFER; Windows 10+ returns n == size. Grow on either signal.
        if (n < buf.size() && GetLastError() != ERROR_INSUFFICIENT_BUFFER) {
            break;
        }
        buf.resize(buf.size() * 2);
    }

    std::wstring path(buf.data());
    const size_t slash = path.find_last_of(L'\\');
    return slash == std::wstring::npos ? std::wstring(L".\\")
                                       : path.substr(0, slash + 1);
}

// One level up from a directory that already ends in a backslash. Empty if
// there is no parent (a drive root).
std::wstring parent_directory(const std::wstring& dir)
{
    if (dir.size() < 2) {
        return {};
    }
    const size_t slash = dir.find_last_of(L'\\', dir.size() - 2);
    if (slash == std::wstring::npos) {
        return {};
    }
    return dir.substr(0, slash + 1);
}

std::string read_file(const std::wstring& path)
{
    HANDLE h = CreateFileW(path.c_str(), GENERIC_READ, FILE_SHARE_READ, nullptr,
                           OPEN_EXISTING, FILE_ATTRIBUTE_NORMAL, nullptr);
    if (h == INVALID_HANDLE_VALUE) {
        return {};
    }

    LARGE_INTEGER size{};
    if (!GetFileSizeEx(h, &size) || size.QuadPart <= 0 ||
        size.QuadPart > 64LL * 1024 * 1024) {
        CloseHandle(h);
        return {};
    }

    std::string data(static_cast<size_t>(size.QuadPart), '\0');
    size_t done = 0;
    while (done < data.size()) {
        DWORD got = 0;
        const DWORD want = static_cast<DWORD>(
            (std::min)(static_cast<size_t>(1u << 20), data.size() - done));
        if (!ReadFile(h, data.data() + done, want, &got, nullptr) || got == 0) {
            CloseHandle(h);
            return {};
        }
        done += got;
    }
    CloseHandle(h);
    return data;
}

}

Library::Library(HMODULE module)
{
    // Each architecture has its own DLL name, so one directory can hold both
    // and each process picks the one it can actually load.
    const wchar_t* engine_name = sizeof(void*) == 8 ? L"dtalk64.dll" : L"dtalk.dll";

    const std::wstring own = module_directory(module);

    // Look beside this module first, then one directory up. The second place
    // is what lets the installer put the x86 SAPI DLL in {app}\x86 while the
    // 1.2 MB of engine DLLs and firmware ROM live once in {app}, rather than
    // being duplicated per architecture.
    const std::wstring candidates[] = { own, parent_directory(own) };

    for (const std::wstring& dir : candidates) {
        if (dir.empty()) {
            continue;
        }
        const std::wstring dll = dir + engine_name;
        const std::wstring rom = dir + L"doubletalkpc.bin";
        if (GetFileAttributesW(dll.c_str()) != INVALID_FILE_ATTRIBUTES &&
            GetFileAttributesW(rom.c_str()) != INVALID_FILE_ATTRIBUTES) {
            dir_ = dir;
            dll_path_ = dll;
            rom_path_ = rom;
            break;
        }
    }

    if (dll_path_.empty()) {
        // Nothing found; report against the module's own directory, which is
        // the location a user would expect to be told about.
        dir_ = own;
        dll_path_ = own + engine_name;
        rom_path_ = own + L"doubletalkpc.bin";
        DT_ERROR("engine: neither %ls nor its parent holds %ls + doubletalkpc.bin",
                 own.c_str(), engine_name);
    }

    DT_INFO("engine: loading %ls", dll_path_.c_str());

    // Absolute path, so nothing on PATH can shadow the shipped engine.
    handle_ = LoadLibraryExW(dll_path_.c_str(), nullptr,
                             LOAD_WITH_ALTERED_SEARCH_PATH);
    if (!handle_) {
        const DWORD err = GetLastError();
        DT_ERROR("engine: LoadLibrary failed for %ls (error %lu)",
                 dll_path_.c_str(), err);
        throw std::runtime_error("cannot load the DoubleTalk engine DLL");
    }

    // Resolving every symbol up front means a truncated or mismatched DLL
    // fails here, at load, rather than as a null call during speech.
    struct Binding { const char* name; FARPROC* slot; };
    const Binding bindings[] = {
        { "dtalk_create",           reinterpret_cast<FARPROC*>(&create)           },
        { "dtalk_destroy",          reinterpret_cast<FARPROC*>(&destroy)          },
        { "dtalk_reset",            reinterpret_cast<FARPROC*>(&reset)            },
        { "dtalk_sample_rate",      reinterpret_cast<FARPROC*>(&sample_rate)      },
        { "dtalk_queue",            reinterpret_cast<FARPROC*>(&queue)            },
        { "dtalk_say",              reinterpret_cast<FARPROC*>(&say)              },
        { "dtalk_stop",             reinterpret_cast<FARPROC*>(&stop)             },
        { "dtalk_active",           reinterpret_cast<FARPROC*>(&active)           },
        { "dtalk_lpc_status",       reinterpret_cast<FARPROC*>(&lpc_status)       },
        { "dtalk_synth",            reinterpret_cast<FARPROC*>(&synth)            },
        { "dtalk_synth16",          reinterpret_cast<FARPROC*>(&synth16)          },
        { "dtalk_set_lowpass_hz",   reinterpret_cast<FARPROC*>(&set_lowpass_hz)   },
        { "dtalk_rate_boost_max",   reinterpret_cast<FARPROC*>(&rate_boost_max)   },
        { "dtalk_set_rate_boost",   reinterpret_cast<FARPROC*>(&set_rate_boost)   },
        { "dtalk_get_rate_boost",   reinterpret_cast<FARPROC*>(&get_rate_boost)   },
        { "dtalk_read_index_marks", reinterpret_cast<FARPROC*>(&read_index_marks) },
    };

    for (const Binding& b : bindings) {
        *b.slot = GetProcAddress(handle_, b.name);
        if (!*b.slot) {
            DT_ERROR("engine: missing export %s", b.name);
            FreeLibrary(handle_);
            handle_ = nullptr;
            throw std::runtime_error("DoubleTalk engine DLL is missing an export");
        }
    }

    rom_ = read_file(rom_path_);
    if (rom_.size() != 512 * 1024) {
        DT_ERROR("engine: ROM %ls is %zu bytes, expected 524288",
                 rom_path_.c_str(), rom_.size());
        FreeLibrary(handle_);
        handle_ = nullptr;
        throw std::runtime_error("doubletalkpc.bin is missing or the wrong size");
    }

    DT_INFO("engine: ROM loaded (%zu bytes), boost levels 0..%d",
            rom_.size(), rate_boost_max());
}

Library::~Library()
{
    // Deliberately does not FreeLibrary: the process-wide Library outlives
    // every Instance and is torn down at exit, where unloading buys nothing
    // and risks running the engine's destructors after its dependencies.
    handle_ = nullptr;
}

Instance::Instance(Library& lib)
    : lib_(lib)
{
    handle_ = lib_.create(lib_.rom(), lib_.rom_size());
    if (!handle_) {
        DT_ERROR("engine: dtalk_create failed");
        throw std::runtime_error("dtalk_create failed");
    }
    sample_rate_ = lib_.sample_rate(handle_);
    DT_INFO("engine: instance created, native rate %u Hz", sample_rate_);
}

Instance::~Instance()
{
    if (handle_) {
        lib_.destroy(handle_);
        handle_ = nullptr;
    }
}

}
