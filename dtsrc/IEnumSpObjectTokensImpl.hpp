// SPDX-License-Identifier: BSD-3-Clause
//
// IEnumSpObjectTokensImpl.hpp - serves the eight DoubleTalk voices to SAPI.
//
// Registered once under Speech\Voices\TokenEnums; SAPI then calls this to
// enumerate voices instead of reading per-voice registry keys.

#pragma once

#include <vector>
#include <windows.h>
#include <sapi.h>
#include <sapiddk.h>
#include <sperror.h>
#include <comdef.h>
#include <comip.h>

#include "com.hpp"
#include "dt_voices.h"
#include "voice_token.hpp"

namespace DoubleTalk {
namespace sapi {

class __declspec(uuid("c54b5b3f-c33d-44ef-889c-54d0fea34e3a")) IEnumSpObjectTokensImpl :
    public IEnumSpObjectTokens
{
public:
    explicit IEnumSpObjectTokensImpl(bool initialize = true);

    IEnumSpObjectTokensImpl(const IEnumSpObjectTokensImpl&) = delete;
    IEnumSpObjectTokensImpl& operator=(const IEnumSpObjectTokensImpl&) = delete;

    STDMETHOD(Next)(ULONG celt, ISpObjectToken** pelt, ULONG* pceltFetched) override;
    STDMETHOD(Skip)(ULONG celt) override;
    STDMETHOD(Reset)() override;
    STDMETHOD(Clone)(IEnumSpObjectTokens** ppEnum) override;
    STDMETHOD(Item)(ULONG Index, ISpObjectToken** ppToken) override;
    STDMETHOD(GetCount)(ULONG* pulCount) override;

protected:
    [[nodiscard]] void* get_interface(REFIID riid) noexcept
    {
        return com::try_primary_interface<IEnumSpObjectTokens>(this, riid);
    }

private:
    _COM_SMARTPTR_TYPEDEF(ISpObjectToken, __uuidof(ISpObjectToken));
    _COM_SMARTPTR_TYPEDEF(ISpObjectTokenInit, __uuidof(ISpObjectTokenInit));

    [[nodiscard]] ISpObjectTokenPtr create_token(int voice_index) const;

    std::size_t      index_ = 0;
    std::vector<int> voices_;
};

}
}
