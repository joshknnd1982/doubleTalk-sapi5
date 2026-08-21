// SPDX-License-Identifier: BSD-3-Clause

#include "ISpTTSEngineImpl.hpp"

#include <algorithm>
#include <map>
#include <new>
#include <string>
#include <vector>

#include "dt_log.h"
#include "dt_settings.h"
#include "dt_voices.h"
#include "utils.hpp"

namespace DoubleTalk {
namespace sapi {

namespace {

HMODULE g_module = nullptr;

// The engine DLL and the 512 KB ROM are loaded once and shared by every voice
// instance in the process. Created on first use rather than at DLL attach,
// where doing file I/O under the loader lock would be a deadlock risk.
Library* shared_library()
{
    static Library* lib = nullptr;
    static bool     tried = false;
    static std::mutex m;

    std::lock_guard<std::mutex> lock(m);
    if (!tried) {
        tried = true;
        try {
            lib = new Library(g_module);
        }
        catch (const std::exception& e) {
            DT_ERROR("engine: unavailable - %s", e.what());
            lib = nullptr;
        }
    }
    return lib;
}

// The firmware's index markers are numbered 0-99 and roll over, so an
// utterance can carry at most 100 before an old, still-pending marker value
// would be reused and land on the wrong event.
constexpr int MAX_MARKS = 100;

struct PendingEvent
{
    SPEVENTENUM  type = SPEI_WORD_BOUNDARY;
    ULONG        text_offset = 0;
    ULONG        text_length = 0;
    std::wstring bookmark_text;
    LONG         bookmark_id = 0;
};

// Emits a Ctrl-A <n> I index marker.
void append_mark(std::string& s, int n)
{
    char buf[16];
    const int len = snprintf(buf, sizeof(buf), "\x01%dI", n);
    if (len > 0) {
        s.append(buf, static_cast<size_t>(len));
    }
}

[[nodiscard]] bool is_word_char(wchar_t c) noexcept
{
    return iswalnum(c) || c == L'\'' || c == L'-';
}

}

void set_module_handle(HMODULE h) noexcept
{
    g_module = h;
}

ISpTTSEngineImpl::ISpTTSEngineImpl() = default;
ISpTTSEngineImpl::~ISpTTSEngineImpl() = default;

bool ISpTTSEngineImpl::ensure_engine()
{
    if (engine_) {
        return true;
    }
    Library* lib = shared_library();
    if (!lib) {
        return false;
    }
    try {
        engine_ = std::make_unique<Engine>(*lib);
        engine_->apply_global(shared().global());
        return true;
    }
    catch (const std::exception& e) {
        DT_ERROR("engine: cannot create instance - %s", e.what());
        return false;
    }
}

STDMETHODIMP ISpTTSEngineImpl::SetObjectToken(ISpObjectToken* pToken)
{
    if (!pToken) {
        return E_INVALIDARG;
    }

    try {
        ISpDataKeyPtr attr;
        if (FAILED(pToken->OpenKey(L"Attributes", &attr))) {
            DT_ERROR("token: cannot open Attributes");
            return E_INVALIDARG;
        }

        voice_index_ = 0;

        // Prefer the explicit index we put on the token; it cannot be
        // ambiguous the way a display name can.
        utils::out_ptr<wchar_t> index_str(CoTaskMemFree);
        if (SUCCEEDED(attr->GetStringValue(L"DTVoiceIndex", index_str.address()))
            && index_str.get()) {
            const int i = _wtoi(index_str.get());
            if (i >= 0 && i < VOICE_COUNT) {
                voice_index_ = i;
            }
        } else {
            // Fall back to the name, for a token built by something other than
            // our own enumerator.
            utils::out_ptr<wchar_t> name(CoTaskMemFree);
            if (SUCCEEDED(attr->GetStringValue(L"Name", name.address())) && name.get()) {
                const std::string n = utils::wstring_to_string(name.get());
                if (const VoiceInfo* v = find_voice(n.c_str())) {
                    voice_index_ = static_cast<int>(v - &VOICES[0]);
                }
            }
        }

        token_ = pToken;
        DT_INFO("token: voice %d (%s)", voice_index_, VOICES[voice_index_].name);
        return S_OK;
    }
    catch (const std::bad_alloc&) {
        return E_OUTOFMEMORY;
    }
    catch (...) {
        return E_UNEXPECTED;
    }
}

STDMETHODIMP ISpTTSEngineImpl::GetObjectToken(ISpObjectToken** ppToken)
{
    if (!ppToken) {
        return E_POINTER;
    }
    *ppToken = nullptr;
    if (!token_) {
        return E_UNEXPECTED;
    }
    token_.AddRef();
    *ppToken = token_.GetInterfacePtr();
    return S_OK;
}

STDMETHODIMP ISpTTSEngineImpl::GetOutputFormat(
    const GUID* /*pTargetFmtId*/,
    const WAVEFORMATEX* /*pTargetWaveFormatEx*/,
    GUID* pOutputFormatId,
    WAVEFORMATEX** ppCoMemOutputWaveFormatEx)
{
    if (!pOutputFormatId || !ppCoMemOutputWaveFormatEx) {
        return E_POINTER;
    }
    *pOutputFormatId = SPDFID_WaveFormatEx;
    *ppCoMemOutputWaveFormatEx = nullptr;

    // Report whatever the settings say we will actually produce. The target
    // format the host suggests is ignored on purpose: SAPI converts for us,
    // and claiming a rate we do not emit would desynchronise event offsets
    // from the audio.
    std::lock_guard<std::mutex> lock(mutex_);
    shared().reload_if_changed();

    DWORD rate = 22050;
    if (ensure_engine()) {
        engine_->apply_global(shared().global());
        rate = engine_->output_rate();
    } else {
        const GlobalSettings g = shared().global();
        rate = g.output_rate == 0 ? 10504 : static_cast<DWORD>(g.output_rate);
    }

    auto* wfex = static_cast<WAVEFORMATEX*>(CoTaskMemAlloc(sizeof(WAVEFORMATEX)));
    if (!wfex) {
        return E_OUTOFMEMORY;
    }
    wfex->wFormatTag      = WAVE_FORMAT_PCM;
    wfex->nChannels       = 1;
    wfex->nSamplesPerSec  = rate;
    wfex->wBitsPerSample  = 16;
    wfex->nBlockAlign     = static_cast<WORD>(wfex->nChannels * wfex->wBitsPerSample / 8);
    wfex->nAvgBytesPerSec = wfex->nSamplesPerSec * wfex->nBlockAlign;
    wfex->cbSize          = 0;

    *ppCoMemOutputWaveFormatEx = wfex;
    DT_DEBUG("format: %lu Hz, 16-bit mono", rate);
    return S_OK;
}

STDMETHODIMP ISpTTSEngineImpl::Speak(
    DWORD dwSpeakFlags,
    REFGUID /*rguidFormatId*/,
    const WAVEFORMATEX* /*pWaveFormatEx*/,
    const SPVTEXTFRAG* pTextFragList,
    ISpTTSEngineSite* pOutputSite)
{
    if (!pTextFragList || !pOutputSite) {
        return E_INVALIDARG;
    }

    std::lock_guard<std::mutex> lock(mutex_);

    // Picking settings up here is what makes a change in the config utility
    // audible on the very next utterance, with no restart and no watcher
    // thread inside the host process.
    if (shared().reload_if_changed() && engine_) {
        engine_->apply_global(shared().global());
    }

    if (!ensure_engine()) {
        return E_FAIL;
    }

    try {
        long   sapi_rate = 0;
        USHORT sapi_volume = 100;
        pOutputSite->GetRate(&sapi_rate);
        pOutputSite->GetVolume(&sapi_volume);

        ULONGLONG interest = 0;
        pOutputSite->GetEventInterest(&interest);
        const bool want_word     = (interest & SPFEI(SPEI_WORD_BOUNDARY)) != 0;
        const bool want_sentence = (interest & SPFEI(SPEI_SENTENCE_BOUNDARY)) != 0;
        const bool want_bookmark = (interest & SPFEI(SPEI_TTS_BOOKMARK)) != 0;

        DT_DEBUG("speak: flags=0x%08lX rate=%ld vol=%u events(word=%d sent=%d bm=%d)",
                 dwSpeakFlags, sapi_rate, sapi_volume,
                 want_word, want_sentence, want_bookmark);

        // ---- build one command-plus-text stream for the whole utterance ----
        //
        // Doing this in a single pass, rather than one card round-trip per
        // fragment, is what lets the firmware's own index markers time the
        // events: a marker's reported sample position is only meaningful
        // within the utterance it was embedded in.

        const VoiceSettings base = shared().voice(voice_index_);
        std::string stream;
        std::map<int, PendingEvent> marks;
        int next_mark = 0;

        auto take_mark = [&]() -> int {
            return next_mark < MAX_MARKS ? next_mark++ : -1;
        };

        // The first fragment's pitch adjustment sets the utterance's base
        // pitch; SAPI raises pitch for capitals via per-fragment PitchAdj.
        int frag_count = 0;
        for (const SPVTEXTFRAG* f = pTextFragList; f; f = f->pNext) {
            ++frag_count;
        }

        bool prefix_written = false;
        int  current_pitch_adj = 0x7fffffff;   // force a prefix on the first fragment

        for (const SPVTEXTFRAG* frag = pTextFragList; frag; frag = frag->pNext) {
            if (frag->State.eAction == SPVA_Bookmark) {
                if (!want_bookmark) {
                    continue;
                }
                const std::wstring text = (frag->ulTextLen && frag->pTextStart)
                    ? std::wstring(frag->pTextStart, frag->ulTextLen)
                    : std::wstring();

                const int m = take_mark();
                PendingEvent ev;
                ev.type = SPEI_TTS_BOOKMARK;
                ev.bookmark_text = text;
                ev.bookmark_id = text.empty() ? 0 : _wtol(text.c_str());
                if (m >= 0) {
                    marks[m] = ev;
                    append_mark(stream, m);
                }
                continue;
            }

            if (frag->State.eAction != SPVA_Speak &&
                frag->State.eAction != SPVA_SpellOut) {
                continue;
            }
            if (frag->ulTextLen == 0 || !frag->pTextStart) {
                continue;
            }

            // Re-emit the command prefix whenever the effective pitch changes,
            // since that is the only per-fragment parameter SAPI varies.
            const int pitch_adj = frag->State.PitchAdj.MiddleAdj;
            if (!prefix_written || pitch_adj != current_pitch_adj) {
                const VoiceSettings s = apply_sapi_modifiers(
                    base, static_cast<int>(sapi_rate), pitch_adj,
                    static_cast<int>(sapi_volume));
                stream += build_prefix(VOICES[voice_index_], s);
                prefix_written = true;
                current_pitch_adj = pitch_adj;
            }

            if (want_sentence) {
                const int m = take_mark();
                if (m >= 0) {
                    PendingEvent ev;
                    ev.type = SPEI_SENTENCE_BOUNDARY;
                    ev.text_offset = frag->ulTextSrcOffset;
                    ev.text_length = frag->ulTextLen;
                    marks[m] = ev;
                    append_mark(stream, m);
                }
            }

            // Word boundaries get a marker each, so highlighting follows the
            // audio instead of being interpolated from character offsets.
            if (want_word) {
                const wchar_t* t = frag->pTextStart;
                const ULONG    n = frag->ulTextLen;
                ULONG i = 0;
                while (i < n) {
                    while (i < n && !is_word_char(t[i])) {
                        stream += sanitize(std::wstring(1, t[i]));
                        ++i;
                    }
                    if (i >= n) {
                        break;
                    }
                    const ULONG start = i;
                    while (i < n && is_word_char(t[i])) {
                        ++i;
                    }
                    const int m = take_mark();
                    if (m >= 0) {
                        PendingEvent ev;
                        ev.type = SPEI_WORD_BOUNDARY;
                        ev.text_offset = frag->ulTextSrcOffset + start;
                        ev.text_length = i - start;
                        marks[m] = ev;
                        append_mark(stream, m);
                    }
                    stream += sanitize(std::wstring(t + start, i - start));
                }
            } else {
                stream += sanitize(std::wstring(frag->pTextStart, frag->ulTextLen));
            }
        }

        if (!prefix_written) {
            // Nothing speakable - bookmarks only, or an empty fragment list.
            // Fire any bookmarks at offset 0 so the host is not left waiting.
            for (const auto& entry : marks) {
                const PendingEvent& ev = entry.second;
                if (ev.type != SPEI_TTS_BOOKMARK) {
                    continue;
                }
                SPEVENT e = {};
                e.eEventId = SPEI_TTS_BOOKMARK;
                e.elParamType = SPET_LPARAM_IS_STRING;
                e.ullAudioStreamOffset = 0;
                e.lParam = reinterpret_cast<LPARAM>(ev.bookmark_text.c_str());
                e.wParam = static_cast<WPARAM>(ev.bookmark_id);
                pOutputSite->AddEvents(&e, 1);
            }
            DT_DEBUG("speak: nothing to say");
            return S_OK;
        }

        stream += '\r';   // CR starts speech in the card's default text mode

        DT_DEBUG("speak: %d fragments -> %zu bytes, %zu markers",
                 frag_count, stream.size(), marks.size());

        // ---------------------------- render ----------------------------

        engine_->begin(stream);

        std::vector<int16_t>  pcm;
        Engine::Mark          marks_out[32];
        ULONGLONG             bytes_written = 0;

        // Markers whose audio position is known but which have not yet been
        // passed by the audio actually handed to SAPI.
        std::vector<std::pair<ULONGLONG, PendingEvent>> queued;

        for (;;) {
            const DWORD actions = pOutputSite->GetActions();
            if (actions & SPVES_ABORT) {
                DT_DEBUG("speak: abort");
                engine_->stop();
                break;
            }
            if (actions & SPVES_SKIP) {
                pOutputSite->CompleteSkip(0);
                engine_->stop();
                break;
            }

            const size_t produced = engine_->render(pcm);
            if (produced == 0) {
                break;
            }

            // Collect any markers the firmware reached inside this chunk.
            const size_t got = engine_->read_marks(marks_out, 32);
            if (got) {
                DT_DEBUG("speak: %zu marker(s) reported", got);
            }
            for (size_t i = 0; i < got; ++i) {
                auto it = marks.find(marks_out[i].value);
                if (it == marks.end()) {
                    continue;
                }
                queued.emplace_back(marks_out[i].output_sample * 2, it->second);
                marks.erase(it);
            }

            // Fire everything the audio we are about to write has reached.
            // Events must be added BEFORE the audio they refer to is written,
            // or SAPI will report them late.
            const ULONGLONG chunk_end = bytes_written + produced * 2;
            for (auto it = queued.begin(); it != queued.end();) {
                if (it->first > chunk_end) {
                    ++it;
                    continue;
                }
                const PendingEvent& ev = it->second;
                SPEVENT e = {};
                e.eEventId = ev.type;
                e.ullAudioStreamOffset = it->first;
                e.ulStreamNum = 0;

                if (ev.type == SPEI_TTS_BOOKMARK) {
                    e.elParamType = SPET_LPARAM_IS_STRING;
                    e.lParam = reinterpret_cast<LPARAM>(ev.bookmark_text.c_str());
                    e.wParam = static_cast<WPARAM>(ev.bookmark_id);
                } else {
                    e.elParamType = SPET_LPARAM_IS_UNDEFINED;
                    e.lParam = static_cast<LPARAM>(ev.text_offset);
                    e.wParam = static_cast<WPARAM>(ev.text_length);
                }
                const HRESULT ehr = pOutputSite->AddEvents(&e, 1);
                DT_DEBUG("speak: event id=%d at byte %llu -> 0x%08lX",
                         static_cast<int>(ev.type),
                         static_cast<unsigned long long>(it->first), ehr);
                it = queued.erase(it);
            }

            // Write the chunk, honouring partial writes.
            const BYTE* p = reinterpret_cast<const BYTE*>(pcm.data());
            ULONG remaining = static_cast<ULONG>(produced * 2);
            while (remaining > 0) {
                ULONG written = 0;
                const HRESULT hr = pOutputSite->Write(p, remaining, &written);
                if (FAILED(hr)) {
                    DT_ERROR("speak: Write failed 0x%08lX", hr);
                    engine_->stop();
                    return hr;
                }
                if (written == 0 || written > remaining) {
                    DT_ERROR("speak: bad Write result %lu of %lu", written, remaining);
                    engine_->stop();
                    return E_FAIL;
                }
                bytes_written += written;
                remaining -= written;
                p += written;
            }
        }

        DT_DEBUG("speak: done, %llu bytes", bytes_written);
        return S_OK;
    }
    catch (const std::bad_alloc&) {
        DT_ERROR("speak: out of memory");
        return E_OUTOFMEMORY;
    }
    catch (const std::exception& e) {
        DT_ERROR("speak: %s", e.what());
        return E_UNEXPECTED;
    }
    catch (...) {
        DT_ERROR("speak: unknown exception");
        return E_UNEXPECTED;
    }
}

}
}
