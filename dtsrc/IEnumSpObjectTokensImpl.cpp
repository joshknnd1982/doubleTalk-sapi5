// SPDX-License-Identifier: BSD-3-Clause

#include <algorithm>
#include <new>
#include <stdexcept>

#include "IEnumSpObjectTokensImpl.hpp"
#include "dt_log.h"
#include "utils.hpp"

namespace DoubleTalk {
namespace sapi {

IEnumSpObjectTokensImpl::IEnumSpObjectTokensImpl(bool initialize)
{
    if (!initialize) {
        return;
    }
    voices_.reserve(VOICE_COUNT);
    for (int i = 0; i < VOICE_COUNT; ++i) {
        voices_.push_back(i);
    }
    DT_DEBUG("enum: created with %d voices", VOICE_COUNT);
}

IEnumSpObjectTokensImpl::ISpObjectTokenPtr
IEnumSpObjectTokensImpl::create_token(int voice_index) const
{
    const std::wstring name = utils::string_to_wstring(VOICES[voice_index].name);
    const std::wstring token_id =
        std::wstring(SPCAT_VOICES) + L"\\TokenEnums\\DoubleTalk\\" + name;

    com::object<voice_token> data_key(voice_index);
    com::interface_ptr<ISpDataKey> data_key_ptr(data_key);

    ISpObjectTokenInitPtr init(CLSID_SpObjectToken);
    if (!init) {
        throw std::runtime_error("cannot create an object token");
    }

    // InitFromDataKey binds the token to our in-memory attribute key rather
    // than to a registry path, which is what keeps the voices out of the
    // registry entirely.
    if (FAILED(init->InitFromDataKey(SPCAT_VOICES, token_id.c_str(),
                                     data_key_ptr.get(false)))) {
        throw std::runtime_error("cannot initialize an object token");
    }

    return ISpObjectTokenPtr(init);
}

STDMETHODIMP IEnumSpObjectTokensImpl::Next(ULONG celt, ISpObjectToken** pelt,
                                           ULONG* pceltFetched)
{
    if (celt == 0) {
        return E_INVALIDARG;
    }
    if (!pelt) {
        return E_POINTER;
    }
    if (!pceltFetched && celt > 1) {
        return E_POINTER;
    }
    if (pceltFetched) {
        *pceltFetched = 0;
    }

    try {
        std::vector<ISpObjectTokenPtr> tokens;
        tokens.reserve(celt);

        const std::size_t end =
            (std::min)(index_ + static_cast<std::size_t>(celt), voices_.size());
        for (std::size_t i = index_; i < end; ++i) {
            tokens.push_back(create_token(voices_[i]));
        }

        for (std::size_t i = 0; i < tokens.size(); ++i) {
            tokens[i].AddRef();
            pelt[i] = tokens[i].GetInterfacePtr();
        }
        if (pceltFetched) {
            *pceltFetched = static_cast<ULONG>(tokens.size());
        }
        index_ += tokens.size();
        return (tokens.size() == celt) ? S_OK : S_FALSE;
    }
    catch (const std::bad_alloc&) {
        return E_OUTOFMEMORY;
    }
    catch (...) {
        DT_ERROR("enum: Next failed");
        return E_UNEXPECTED;
    }
}

STDMETHODIMP IEnumSpObjectTokensImpl::Skip(ULONG celt)
{
    const std::size_t remaining = voices_.size() - index_;
    const std::size_t skipped = (std::min)(remaining, static_cast<std::size_t>(celt));
    index_ += skipped;
    return (skipped == celt) ? S_OK : S_FALSE;
}

STDMETHODIMP IEnumSpObjectTokensImpl::Reset()
{
    index_ = 0;
    return S_OK;
}

STDMETHODIMP IEnumSpObjectTokensImpl::GetCount(ULONG* pulCount)
{
    if (!pulCount) {
        return E_POINTER;
    }
    *pulCount = static_cast<ULONG>(voices_.size());
    return S_OK;
}

STDMETHODIMP IEnumSpObjectTokensImpl::Item(ULONG Index, ISpObjectToken** ppToken)
{
    if (!ppToken) {
        return E_POINTER;
    }
    *ppToken = nullptr;

    if (Index >= voices_.size()) {
        return SPERR_NO_MORE_ITEMS;
    }

    try {
        ISpObjectTokenPtr token = create_token(voices_[Index]);
        token.AddRef();
        *ppToken = token.GetInterfacePtr();
        return S_OK;
    }
    catch (const std::bad_alloc&) {
        return E_OUTOFMEMORY;
    }
    catch (...) {
        DT_ERROR("enum: Item(%lu) failed", Index);
        return E_UNEXPECTED;
    }
}

STDMETHODIMP IEnumSpObjectTokensImpl::Clone(IEnumSpObjectTokens** ppEnum)
{
    if (!ppEnum) {
        return E_POINTER;
    }
    *ppEnum = nullptr;

    try {
        com::object<IEnumSpObjectTokensImpl> obj(false);
        obj->voices_ = voices_;
        obj->index_  = index_;
        com::interface_ptr<IEnumSpObjectTokens> ptr(obj);
        *ppEnum = ptr.get();
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
