// SPDX-License-Identifier: BSD-3-Clause
//
// ISpTTSEngineImpl.hpp - the SAPI5 TTS engine object.
//
// One instance per voice a SAPI host creates. Owns its own emulated card, so
// two applications speaking at once do not fight over one firmware state.

#pragma once

#include <windows.h>
#include <sapi.h>
#include <sapiddk.h>
#include <comdef.h>
#include <comip.h>

#include <memory>
#include <mutex>

#include "com.hpp"
#include "dt_engine.h"

namespace DoubleTalk {
namespace sapi {

class __declspec(uuid("0b5f5547-10b5-4435-9534-11502cc8461d")) ISpTTSEngineImpl :
    public ISpTTSEngine, public ISpObjectWithToken
{
public:
    ISpTTSEngineImpl();
    ~ISpTTSEngineImpl();

    ISpTTSEngineImpl(const ISpTTSEngineImpl&) = delete;
    ISpTTSEngineImpl& operator=(const ISpTTSEngineImpl&) = delete;

    STDMETHOD(Speak)(DWORD dwSpeakFlags, REFGUID rguidFormatId,
                     const WAVEFORMATEX* pWaveFormatEx,
                     const SPVTEXTFRAG* pTextFragList,
                     ISpTTSEngineSite* pOutputSite) override;

    STDMETHOD(GetOutputFormat)(const GUID* pTargetFmtId,
                               const WAVEFORMATEX* pTargetWaveFormatEx,
                               GUID* pOutputFormatId,
                               WAVEFORMATEX** ppCoMemOutputWaveFormatEx) override;

    STDMETHOD(SetObjectToken)(ISpObjectToken* pToken) override;
    STDMETHOD(GetObjectToken)(ISpObjectToken** ppToken) override;

protected:
    [[nodiscard]] void* get_interface(REFIID riid) noexcept
    {
        void* ptr = com::try_primary_interface<ISpTTSEngine>(this, riid);
        return ptr ? ptr : com::try_interface<ISpObjectWithToken>(this, riid);
    }

private:
    _COM_SMARTPTR_TYPEDEF(ISpObjectToken, __uuidof(ISpObjectToken));
    _COM_SMARTPTR_TYPEDEF(ISpDataKey, __uuidof(ISpDataKey));

    // Creates the emulated card on first use. Returns false if the engine DLL
    // or the ROM is unavailable, which is reported to SAPI as a failure rather
    // than as silence.
    [[nodiscard]] bool ensure_engine();

    ISpObjectTokenPtr       token_;
    int                     voice_index_ = 0;
    std::mutex              mutex_;
    std::unique_ptr<Engine> engine_;
};

// Called from DllMain so the engine can find dtalk*.dll and the ROM beside
// this DLL rather than beside whatever host process loaded it.
void set_module_handle(HMODULE h) noexcept;

}
}
