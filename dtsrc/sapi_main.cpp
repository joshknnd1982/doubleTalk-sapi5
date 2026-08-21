// SPDX-License-Identifier: BSD-3-Clause
//
// sapi_main.cpp - DLL entry points and COM self-registration.
//
// Registry footprint, in full:
//
//   HKLM\Software\Classes\CLSID\{engine}\InProcServer32
//   HKLM\Software\Classes\CLSID\{enumerator}\InProcServer32
//   HKLM\Software\Microsoft\Speech\Voices\TokenEnums\DoubleTalk
//
// That is three keys, and it is the irreducible minimum: SAPI discovers voices
// through the registry, so an engine that wrote nothing there would simply not
// be found. Every voice attribute and every tunable parameter is served from
// memory or from the INI file instead - the eight voices themselves have no
// registry presence at all.

#include <new>
#include <sapi.h>

#include "com.hpp"
#include "dt_log.h"
#include "dt_settings.h"
#include "IEnumSpObjectTokensImpl.hpp"
#include "ISpTTSEngineImpl.hpp"
#include "registry.hpp"

namespace {

HINSTANCE g_dll_handle = nullptr;
DoubleTalk::com::class_object_factory g_factory;

const std::wstring token_enums_path = L"Software\\Microsoft\\Speech\\Voices\\TokenEnums";
const std::wstring enum_name = L"DoubleTalk";

[[nodiscard]] std::wstring clsid_to_string(const GUID& clsid)
{
    wchar_t buf[64];
    StringFromGUID2(clsid, buf, 64);
    return std::wstring(buf);
}

void register_token_enumerator(HKEY root)
{
    using namespace DoubleTalk::sapi;
    using namespace DoubleTalk::registry;

    key enums(root, token_enums_path,
              KEY_CREATE_SUB_KEY | KEY_SET_VALUE, true);
    key mine(enums, enum_name, KEY_SET_VALUE, true);

    mine.set(L"DoubleTalk PC Voices");
    mine.set(L"CLSID", clsid_to_string(__uuidof(IEnumSpObjectTokensImpl)));
}

void unregister_token_enumerator() noexcept
{
    using namespace DoubleTalk::registry;
    // Remove from both hives regardless of which one this process could write
    // to, so an uninstall run elevated still clears a per-user registration
    // left behind by a non-elevated install (and vice versa).
    for (HKEY root : { HKEY_LOCAL_MACHINE, HKEY_CURRENT_USER }) {
        try {
            key enums(root, token_enums_path, KEY_ALL_ACCESS);
            enums.delete_subkey(enum_name);
        }
        catch (...) {
            // Already gone, or no permission. Neither is worth failing an
            // uninstall over.
        }
    }
}

}

BOOL APIENTRY DllMain(HINSTANCE hInstance, DWORD dwReason, LPVOID /*lpReserved*/)
{
    if (dwReason == DLL_PROCESS_ATTACH) {
        g_dll_handle = hInstance;
        DisableThreadLibraryCalls(hInstance);

        // Only cheap, loader-lock-safe work here. Opening the log is a file
        // handle and a formatted line; loading the engine DLL and the ROM is
        // deferred to first use precisely because doing it under the loader
        // lock could deadlock.
        DoubleTalk::log::init(L"sapi");
        DoubleTalk::sapi::set_module_handle(hInstance);

        try {
            DoubleTalk::shared().load();
        }
        catch (...) {
            // Defaults are already in place; a bad settings file must never
            // stop the voices from loading.
        }

        DT_INFO("dll: attached");

        try {
            g_factory.register_class<DoubleTalk::sapi::IEnumSpObjectTokensImpl>();
            g_factory.register_class<DoubleTalk::sapi::ISpTTSEngineImpl>();
        }
        catch (...) {
            DT_ERROR("dll: class factory registration failed");
            return FALSE;
        }
    }
    else if (dwReason == DLL_PROCESS_DETACH) {
        DT_INFO("dll: detaching");
        DoubleTalk::log::shutdown();
    }
    return TRUE;
}

STDAPI DllGetClassObject(REFCLSID rclsid, REFIID riid, void** ppv)
{
    return g_factory.create(rclsid, riid, ppv);
}

STDAPI DllCanUnloadNow()
{
    return DoubleTalk::com::object_counter::is_zero() ? S_OK : S_FALSE;
}

STDAPI DllRegisterServer()
{
    try {
        DoubleTalk::com::class_registrar r(g_dll_handle);
        r.register_class<DoubleTalk::sapi::IEnumSpObjectTokensImpl>();
        r.register_class<DoubleTalk::sapi::ISpTTSEngineImpl>();
        register_token_enumerator(r.root());
        DT_INFO("dll: registered under %s",
                r.root() == HKEY_LOCAL_MACHINE ? "HKLM (all users)"
                                               : "HKCU (this user only)");
        return S_OK;
    }
    catch (const std::bad_alloc&) {
        return E_OUTOFMEMORY;
    }
    catch (const std::exception& e) {
        DT_ERROR("dll: registration failed - %s", e.what());
        return SELFREG_E_CLASS;
    }
    catch (...) {
        DT_ERROR("dll: registration failed");
        return SELFREG_E_CLASS;
    }
}

STDAPI DllUnregisterServer()
{
    try {
        unregister_token_enumerator();
        DoubleTalk::com::class_registrar r(g_dll_handle);
        r.unregister_class<DoubleTalk::sapi::IEnumSpObjectTokensImpl>();
        r.unregister_class<DoubleTalk::sapi::ISpTTSEngineImpl>();
        DT_INFO("dll: unregistered");
        return S_OK;
    }
    catch (const std::bad_alloc&) {
        return E_OUTOFMEMORY;
    }
    catch (...) {
        // An unregister that cannot find what it is removing has still
        // achieved the caller's goal.
        return S_OK;
    }
}
