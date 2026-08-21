// SPDX-License-Identifier: BSD-3-Clause
//
// dt_sapitest.cpp - end-to-end test of the SAPI5 engine.
//
// Drives the real SAPI stack - SpVoice, format negotiation, ISpTTSEngine::
// Speak, the event queue - but creates the voice token from our own
// enumerator rather than relying on SAPI's registry discovery. That matters
// because SAPI only reads voice enumerators from HKLM, so without this the
// engine could not be exercised at all without an elevated install.
//
//   dt_sapitest --list
//   dt_sapitest --voice 2 --out out.wav "Hello"
//   dt_sapitest --all --outdir sapi_samples
//   dt_sapitest --events "One two three"

#include <windows.h>
#include <sapi.h>
#include <sapiddk.h>
#include <sperror.h>
#include <comdef.h>
#include <comip.h>

#include <cstdio>
#include <string>
#include <vector>

namespace {

_COM_SMARTPTR_TYPEDEF(ISpVoice, __uuidof(ISpVoice));
_COM_SMARTPTR_TYPEDEF(ISpObjectToken, __uuidof(ISpObjectToken));
_COM_SMARTPTR_TYPEDEF(IEnumSpObjectTokens, __uuidof(IEnumSpObjectTokens));
_COM_SMARTPTR_TYPEDEF(ISpStream, __uuidof(ISpStream));
_COM_SMARTPTR_TYPEDEF(ISpDataKey, __uuidof(ISpDataKey));

// Must match IEnumSpObjectTokensImpl's uuid.
const CLSID CLSID_DoubleTalkEnum =
    { 0xc54b5b3f, 0xc33d, 0x44ef, { 0x88, 0x9c, 0x54, 0xd0, 0xfe, 0xa3, 0x4e, 0x3a } };

bool check(HRESULT hr, const wchar_t* what)
{
    if (FAILED(hr)) {
        fwprintf(stderr, L"%s failed: 0x%08lX\n", what, hr);
        return false;
    }
    return true;
}

std::wstring token_name(ISpObjectToken* token)
{
    ISpDataKeyPtr attr;
    if (FAILED(token->OpenKey(L"Attributes", &attr))) {
        return L"(no attributes)";
    }
    LPWSTR name = nullptr;
    if (FAILED(attr->GetStringValue(L"Name", &name)) || !name) {
        return L"(no name)";
    }
    std::wstring out(name);
    CoTaskMemFree(name);
    return out;
}

int usage()
{
    wprintf(L"dt_sapitest - end-to-end SAPI5 test\n\n"
            L"  --list             enumerate voices via our token enumerator\n"
            L"  --voice N          voice index (default 0)\n"
            L"  --all              speak once per voice\n"
            L"  --out FILE.wav     render to a file instead of the speakers\n"
            L"  --outdir DIR       output directory for --all\n"
            L"  --rate N           SAPI rate -10..10\n"
            L"  --volume N         SAPI volume 0..100\n"
            L"  --events           report word-boundary and bookmark events\n");
    return 2;
}

}

int wmain(int argc, wchar_t** argv)
{
    // Unbuffered: if the engine faults, buffered progress output would be lost
    // and the last line printed is exactly the diagnostic that matters.
    setvbuf(stdout, nullptr, _IONBF, 0);
    setvbuf(stderr, nullptr, _IONBF, 0);

    std::wstring text = L"Hello. This is the Double Talk P C speaking through SAPI 5.";
    std::wstring out_path;
    std::wstring out_dir = L".";
    int  voice = 0;
    int  rate = 0;
    int  volume = 100;
    bool list = false, all = false, events = false;

    for (int i = 1; i < argc; ++i) {
        const std::wstring a = argv[i];
        if      (a == L"--list")   list = true;
        else if (a == L"--all")    all = true;
        else if (a == L"--events") events = true;
        else if (a == L"--voice"  && i + 1 < argc) voice  = _wtoi(argv[++i]);
        else if (a == L"--rate"   && i + 1 < argc) rate   = _wtoi(argv[++i]);
        else if (a == L"--volume" && i + 1 < argc) volume = _wtoi(argv[++i]);
        else if (a == L"--out"    && i + 1 < argc) out_path = argv[++i];
        else if (a == L"--outdir" && i + 1 < argc) out_dir  = argv[++i];
        else if (a == L"--help" || a == L"-h") return usage();
        else if (!a.empty() && a[0] == L'-') return usage();
        else text = a;
    }

    if (FAILED(CoInitializeEx(nullptr, COINIT_APARTMENTTHREADED))) {
        fwprintf(stderr, L"CoInitializeEx failed\n");
        return 1;
    }

    // Every COM pointer must be released BEFORE CoUninitialize: once COM shuts
    // down it unloads the in-process servers, and a smart pointer released
    // afterwards calls Release() into an unmapped DLL. Running the whole body
    // inside a lambda makes each `return` destroy the pointers first.
    const int rc = [&]() -> int {
        // Our enumerator, created directly. This is the piece SAPI would
        // normally reach through the TokenEnums registry key.
        IEnumSpObjectTokensPtr tokens;
        if (!check(tokens.CreateInstance(CLSID_DoubleTalkEnum),
                   L"creating the DoubleTalk token enumerator")) {
            fwprintf(stderr, L"is DoubleTalkSAPI.dll registered "
                             L"(regsvr32 DoubleTalkSAPI.dll)?\n");
            return 1;
        }

        ULONG count = 0;
        if (!check(tokens->GetCount(&count), L"GetCount")) {
            return 1;
        }
        wprintf(L"enumerator reports %lu voices\n", count);

        if (list) {
            for (ULONG i = 0; i < count; ++i) {
                ISpObjectTokenPtr t;
                if (SUCCEEDED(tokens->Item(i, &t))) {
                    wprintf(L"  [%lu] %s\n", i, token_name(t).c_str());
                }
            }
            return 0;
        }

        ISpVoicePtr sp;
        if (!check(sp.CreateInstance(CLSID_SpVoice), L"creating SpVoice")) {
            return 1;
        }

        auto speak_one = [&](ULONG index, const std::wstring& file) -> bool {
            ISpObjectTokenPtr token;
            if (!check(tokens->Item(index, &token), L"Item")) {
                return false;
            }
            const std::wstring name = token_name(token);

            if (!check(sp->SetVoice(token), L"SetVoice")) {
                return false;
            }
            sp->SetRate(rate);
            sp->SetVolume(static_cast<USHORT>(volume));

            // Interest must be declared before output is bound and before
            // Speak, or SAPI discards the events instead of queueing them.
            if (events) {
                const ULONGLONG mask =
                    SPFEI(SPEI_WORD_BOUNDARY) | SPFEI(SPEI_SENTENCE_BOUNDARY) |
                    SPFEI(SPEI_TTS_BOOKMARK)  | SPFEI(SPEI_END_INPUT_STREAM);
                check(sp->SetInterest(mask, mask), L"SetInterest");
            }

            ISpStreamPtr stream;
            if (!file.empty()) {
                // Write the file in the engine's own output format so nothing
                // is resampled behind our back on the way to disk.
                ISpStreamPtr s;
                if (!check(s.CreateInstance(CLSID_SpStream), L"creating SpStream")) {
                    return false;
                }
                WAVEFORMATEX wfex = {};
                wfex.wFormatTag = WAVE_FORMAT_PCM;
                wfex.nChannels = 1;
                wfex.nSamplesPerSec = 22050;
                wfex.wBitsPerSample = 16;
                wfex.nBlockAlign = 2;
                wfex.nAvgBytesPerSec = wfex.nSamplesPerSec * wfex.nBlockAlign;
                wfex.cbSize = 0;

                if (!check(s->BindToFile(file.c_str(), SPFM_CREATE_ALWAYS,
                                         &SPDFID_WaveFormatEx, &wfex,
                                         SPFEI_ALL_EVENTS),
                           L"BindToFile")) {
                    return false;
                }
                if (!check(sp->SetOutput(s, TRUE), L"SetOutput")) {
                    return false;
                }
                stream = s;
            }

            // Async, so events can be drained as the audio is produced. A
            // synchronous Speak returns with the queue already emptied.
            const DWORD flags = events ? (SPF_ASYNC | SPF_PURGEBEFORESPEAK)
                                       : SPF_DEFAULT;
            const HRESULT hr = sp->Speak(text.c_str(), flags, nullptr);
            if (!check(hr, L"Speak")) {
                return false;
            }

            if (events) {
                int  n = 0;
                bool finished = false;

                while (!finished) {
                    // Returns as soon as an event is queued; the timeout is
                    // only a guard against a hang.
                    if (sp->WaitForNotifyEvent(5000) == S_FALSE) {
                        wprintf(L"    (timed out waiting for events)\n");
                        break;
                    }

                    SPEVENT ev = {};
                    ULONG fetched = 0;
                    while (sp->GetEvents(1, &ev, &fetched) == S_OK && fetched == 1) {
                        const double sec = ev.ullAudioStreamOffset / (22050.0 * 2.0);
                        switch (ev.eEventId) {
                            case SPEI_WORD_BOUNDARY:
                                wprintf(L"    %7.3fs  word      offset=%2llu len=%llu\n",
                                        sec, (unsigned long long)ev.lParam,
                                        (unsigned long long)ev.wParam);
                                break;
                            case SPEI_SENTENCE_BOUNDARY:
                                wprintf(L"    %7.3fs  sentence  offset=%2llu len=%llu\n",
                                        sec, (unsigned long long)ev.lParam,
                                        (unsigned long long)ev.wParam);
                                break;
                            case SPEI_TTS_BOOKMARK:
                                wprintf(L"    %7.3fs  bookmark  id=%llu\n",
                                        sec, (unsigned long long)ev.wParam);
                                break;
                            case SPEI_END_INPUT_STREAM:
                                wprintf(L"    %7.3fs  end of stream\n", sec);
                                finished = true;
                                break;
                            default:
                                break;
                        }
                        ++n;
                        ev = SPEVENT{};
                    }
                }
                wprintf(L"    (%d events)\n", n);
            }

            sp->WaitUntilDone(INFINITE);

            if (stream) {
                sp->SetOutput(nullptr, FALSE);
                stream->Close();
                wprintf(L"  [%lu] %-14s -> %s\n", index, name.c_str(), file.c_str());
            } else {
                wprintf(L"  [%lu] %-14s spoken\n", index, name.c_str());
            }
            return true;
        };

        bool ok = true;
        if (all) {
            CreateDirectoryW(out_dir.c_str(), nullptr);
            for (ULONG i = 0; i < count; ++i) {
                std::wstring f = out_dir + L"\\sapi_voice" + std::to_wstring(i) + L".wav";
                ok = speak_one(i, f) && ok;
            }
        } else {
            ok = speak_one(static_cast<ULONG>(voice), out_path);
        }
        return ok ? 0 : 1;
    }();

    CoUninitialize();
    return rc;
}
