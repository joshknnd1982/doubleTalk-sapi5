// SPDX-License-Identifier: BSD-3-Clause

#include "dt_log.h"

#include <shlobj.h>
#include <cstdarg>
#include <cstdio>
#include <mutex>
#include <vector>
#include <string>

namespace DoubleTalk {
namespace log {

namespace {

// 4 MB, then rotate to .1 and start fresh. A screen reader speaking all day at
// Debug can produce a lot; capping keeps a runaway log from filling the disk
// while still leaving the most recent two files for a bug report.
constexpr long long MAX_BYTES = 4LL * 1024 * 1024;

std::atomic<int>  g_level{ static_cast<int>(Level::Info) };
std::mutex        g_mutex;
std::wstring      g_path;
FILE*             g_file = nullptr;

std::string to_utf8(const std::wstring& w)
{
    if (w.empty()) {
        return {};
    }
    const int n = WideCharToMultiByte(CP_UTF8, 0, w.c_str(), static_cast<int>(w.size()),
                                      nullptr, 0, nullptr, nullptr);
    std::string out(static_cast<size_t>(n), '\0');
    WideCharToMultiByte(CP_UTF8, 0, w.c_str(), static_cast<int>(w.size()),
                        out.data(), n, nullptr, nullptr);
    return out;
}

const char* level_name(Level l) noexcept
{
    switch (l) {
        case Level::Error: return "ERROR";
        case Level::Warn:  return "WARN ";
        case Level::Info:  return "INFO ";
        case Level::Debug: return "DEBUG";
        default:           return "?????";
    }
}

// %LOCALAPPDATA%\DoubleTalkSAPI\logs, created if absent. Falls back to %TEMP%
// so that a locked-down or roaming-profile machine still gets diagnostics.
std::wstring log_dir()
{
    wchar_t* base = nullptr;
    std::wstring dir;
    if (SUCCEEDED(SHGetKnownFolderPath(FOLDERID_LocalAppData, 0, nullptr, &base)) && base) {
        dir.assign(base);
        CoTaskMemFree(base);
    } else {
        wchar_t tmp[MAX_PATH]{};
        if (GetTempPathW(MAX_PATH, tmp) == 0) {
            return L".";
        }
        dir.assign(tmp);
        if (!dir.empty() && dir.back() == L'\\') {
            dir.pop_back();
        }
    }
    dir += L"\\DoubleTalkSAPI";
    CreateDirectoryW(dir.c_str(), nullptr);
    dir += L"\\logs";
    CreateDirectoryW(dir.c_str(), nullptr);
    return dir;
}

// Called with g_mutex held.
void rotate_if_needed()
{
    if (!g_file) {
        return;
    }
    if (_ftelli64(g_file) < MAX_BYTES) {
        return;
    }
    fclose(g_file);
    g_file = nullptr;

    const std::wstring old = g_path + L".1";
    DeleteFileW(old.c_str());
    MoveFileW(g_path.c_str(), old.c_str());

    _wfopen_s(&g_file, g_path.c_str(), L"ab");
}

}

void init(const wchar_t* tag)
{
    std::lock_guard<std::mutex> lock(g_mutex);
    if (g_file) {
        return;
    }

    wchar_t name[128]{};
    swprintf_s(name, L"doubletalk-%s-%s-%lu.log",
               tag ? tag : L"unknown",
               sizeof(void*) == 8 ? L"x64" : L"x86",
               GetCurrentProcessId());

    g_path = log_dir() + L"\\" + name;
    _wfopen_s(&g_file, g_path.c_str(), L"ab");

    if (g_file) {
        wchar_t exe[MAX_PATH]{};
        GetModuleFileNameW(nullptr, exe, MAX_PATH);
        fprintf(g_file,
                "\n===== DoubleTalk SAPI5 log opened =====\n"
                "host process : %s\n"
                "pid          : %lu\n"
                "architecture : %s\n",
                to_utf8(exe).c_str(), GetCurrentProcessId(),
                sizeof(void*) == 8 ? "x64" : "x86");
        fflush(g_file);
    }
}

void shutdown()
{
    std::lock_guard<std::mutex> lock(g_mutex);
    if (g_file) {
        fprintf(g_file, "===== log closed =====\n");
        fclose(g_file);
        g_file = nullptr;
    }
}

void set_level(Level level) noexcept
{
    g_level.store(static_cast<int>(level), std::memory_order_relaxed);
}

Level level() noexcept
{
    return static_cast<Level>(g_level.load(std::memory_order_relaxed));
}

std::wstring path()
{
    std::lock_guard<std::mutex> lock(g_mutex);
    return g_path;
}

void write(Level lvl, const char* fmt, ...)
{
    std::lock_guard<std::mutex> lock(g_mutex);
    if (!g_file) {
        return;
    }

    // Wall-clock plus milliseconds: timing matters when diagnosing why a
    // screen reader went quiet, and second resolution is too coarse to show
    // where a stall sits.
    SYSTEMTIME st;
    GetLocalTime(&st);

    std::vector<char> buf(1024);
    for (;;) {
        va_list args;
        va_start(args, fmt);
        const int n = vsnprintf(buf.data(), buf.size(), fmt, args);
        va_end(args);

        if (n < 0) {
            return;
        }
        if (static_cast<size_t>(n) < buf.size()) {
            break;
        }
        buf.resize(static_cast<size_t>(n) + 1);
    }

    fprintf(g_file, "[%02d:%02d:%02d.%03d] [%lu] %s  %s\n",
            st.wHour, st.wMinute, st.wSecond, st.wMilliseconds,
            GetCurrentThreadId(), level_name(lvl), buf.data());
    fflush(g_file);

    rotate_if_needed();
}

}
}
