#include <algorithm>
#include <stdexcept>
#include "com.hpp"

namespace DoubleTalk {
namespace com {

wchar_t* strdup(const std::wstring& s)
{
    const std::size_t size = s.size();
    auto* b = static_cast<wchar_t*>(CoTaskMemAlloc((size + 1) * sizeof(wchar_t)));
    if (!b) {
        throw std::bad_alloc();
    }
    std::copy(s.begin(), s.end(), b);
    b[size] = L'\0';
    return b;
}

std::atomic<long> object_counter::count_{0};

HRESULT class_object_factory::create(REFCLSID rclsid, REFIID riid, void** ppv) const noexcept
{
    if (!ppv) {
        return E_POINTER;
    }
    *ppv = nullptr;

    for (const auto& creator : creators_) {
        if (creator->matches(rclsid)) {
            try {
                return creator->create(riid, ppv);
            }
            catch (const std::bad_alloc&) {
                return E_OUTOFMEMORY;
            }
            catch (...) {
                return E_UNEXPECTED;
            }
        }
    }
    return CLASS_E_CLASSNOTAVAILABLE;
}

HKEY class_registrar::writable_root() noexcept
{
    // Probing beats checking for an admin token: what matters is whether this
    // process can actually create the key, which also covers a machine where
    // HKLM has been locked down for administrators too.
    HKEY probe = nullptr;
    const LONG rc = RegCreateKeyExW(HKEY_LOCAL_MACHINE, clsid_key_path.c_str(),
                                    0, nullptr, 0, KEY_CREATE_SUB_KEY, nullptr,
                                    &probe, nullptr);
    if (rc == ERROR_SUCCESS) {
        RegCloseKey(probe);
        return HKEY_LOCAL_MACHINE;
    }
    return HKEY_CURRENT_USER;
}

class_registrar::class_registrar(HINSTANCE dll_handle)
    : root_(writable_root())
{
    wchar_t buffer[MAX_PATH + 1];
    const DWORD size = GetModuleFileNameW(dll_handle, buffer, MAX_PATH);
    if (size == 0) {
        throw std::runtime_error("Unable to get the path of the dll");
    }
    buffer[size] = L'\0';
    dll_path_.assign(buffer);
}

const std::wstring class_registrar::clsid_key_path(L"Software\\Classes\\CLSID");
}
}
