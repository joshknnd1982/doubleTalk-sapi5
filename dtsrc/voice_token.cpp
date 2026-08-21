// SPDX-License-Identifier: BSD-3-Clause

#include <new>
#include <comdef.h>

#include "voice_token.hpp"
#include "ISpTTSEngineImpl.hpp"

namespace DoubleTalk {
namespace sapi {

voice_token::voice_token(int voice_index)
{
    if (voice_index < 0 || voice_index >= VOICE_COUNT) {
        voice_index = 0;
    }
    const VoiceInfo& v = VOICES[voice_index];
    const std::wstring name = utils::string_to_wstring(v.name);

    // The token's default value is the display name SAPI shows in voice
    // pickers; CLSID names the engine that will be created for it.
    set(name);

    utils::out_ptr<wchar_t> clsid_str(CoTaskMemFree);
    StringFromCLSID(__uuidof(ISpTTSEngineImpl), clsid_str.address());
    set(L"CLSID", clsid_str.get());

    // The engine needs to know WHICH voice this token stands for. Passing it as
    // an attribute means SetObjectToken can read the card's voice number
    // straight back rather than matching on the display name, so a renamed or
    // localized name could never select the wrong voice.
    attributes_[L"DTVoiceIndex"] = std::to_wstring(voice_index);

    attributes_[L"Name"]   = name;
    attributes_[L"Vendor"] = L"RC Systems";
    attributes_[L"Age"]    = L"Adult";

    // Every DoubleTalk PC voice is male; the female voices (Gretchen and the
    // rest) belong to the later RC8660 chip, not this card's firmware.
    attributes_[L"Gender"] = L"Male";

    // 409 is en-US. The card has one English text-to-phoneme rule set and no
    // language-select command, so this is not a per-voice property.
    attributes_[L"Language"] = L"409";

    // Advertised so SAPI hosts can filter on it; the engine really does emit
    // word-boundary and bookmark events, timed from the firmware's own index
    // markers rather than estimated from text offsets.
    attributes_[L"SpLexicon"] = L"";
}

STDMETHODIMP voice_token::OpenKey(LPCWSTR pszSubKeyName, ISpDataKey** ppSubKey)
{
    if (!pszSubKeyName) {
        return E_INVALIDARG;
    }
    if (!ppSubKey) {
        return E_POINTER;
    }
    *ppSubKey = nullptr;

    try {
        if (!str_equal(pszSubKeyName, L"Attributes")) {
            return SPERR_NOT_FOUND;
        }

        com::object<ISpDataKeyImpl> obj;
        for (const auto& entry : attributes_) {
            obj->set(entry.first, entry.second);
        }

        com::interface_ptr<ISpDataKey> ptr(obj);
        *ppSubKey = ptr.get();
        return S_OK;
    }
    catch (const std::bad_alloc&) {
        return E_OUTOFMEMORY;
    }
    catch (...) {
        return E_UNEXPECTED;
    }
}

STDMETHODIMP voice_token::EnumKeys(ULONG Index, LPWSTR* ppszSubKeyName)
{
    if (!ppszSubKeyName) {
        return E_POINTER;
    }
    *ppszSubKeyName = nullptr;

    if (Index > 0) {
        return SPERR_NO_MORE_ITEMS;
    }

    try {
        *ppszSubKeyName = com::strdup(L"Attributes");
        return S_OK;
    }
    catch (const std::bad_alloc&) {
        return E_OUTOFMEMORY;
    }
    catch (...) {
        return E_UNEXPECTED;
    }
}

}
}
