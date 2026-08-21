// SPDX-License-Identifier: BSD-3-Clause
//
// dtconfig.cpp - the DoubleTalk PC configuration utility.
//
// Design notes that matter for screen-reader users:
//
//  * Every control is a drop-down list, and every one is preceded in the
//    dialog resource by its own label. That ordering is what makes MSAA/UIA
//    report the label as the control's accessible name, so NVDA/JAWS announce
//    "Articulation, combo box, 5" instead of an unnamed combo box.
//  * Nothing is mouse-only and nothing is off the tab order.
//  * Changing any value applies it and speaks a preview immediately, so the
//    effect of a setting is heard rather than read.
//  * Every change is also written to disk immediately, so the SAPI5 engine -
//    which re-reads the settings file whenever it changes - picks it up on the
//    very next thing it speaks, with no restart.
//
// The utility owns its own Store instance rather than sharing the engine's, so
// what it writes is always a deliberate, complete state.

#include <windows.h>
#include <commctrl.h>
#include <mmsystem.h>
#include <shellapi.h>

#include <memory>
#include <string>
#include <vector>

#include "resource.h"

#include "dt_engine.h"
#include "dt_log.h"
#include "dt_settings.h"
#include "dt_voices.h"
#include "dtalk_api.h"

#pragma comment(lib, "comctl32.lib")
#pragma comment(lib, "winmm.lib")

// Common Controls v6, for themed controls and correct UIA behaviour.
#pragma comment(linker, "/manifestdependency:\"type='win32' \
name='Microsoft.Windows.Common-Controls' version='6.0.0.0' \
processorArchitecture='*' publicKeyToken='6595b64144ccf1df' language='*'\"")

using namespace DoubleTalk;

namespace {

Store*                  g_store = nullptr;
std::unique_ptr<Library> g_lib;
std::unique_ptr<Engine>  g_engine;
std::vector<BYTE>        g_wav;      // must outlive the async PlaySound
bool                     g_loading = false;  // suppress change handlers while populating

std::wstring widen(const char* s)
{
    if (!s || !*s) {
        return {};
    }
    const int n = MultiByteToWideChar(CP_UTF8, 0, s, -1, nullptr, 0);
    std::wstring w(static_cast<size_t>(n ? n - 1 : 0), L'\0');
    MultiByteToWideChar(CP_UTF8, 0, s, -1, w.data(), n);
    return w;
}

void set_status(HWND dlg, const wchar_t* text)
{
    SetDlgItemTextW(dlg, IDC_STATUS, text);
}

void combo_add(HWND dlg, int id, const std::wstring& text, int data)
{
    const int i = static_cast<int>(
        SendDlgItemMessageW(dlg, id, CB_ADDSTRING, 0,
                            reinterpret_cast<LPARAM>(text.c_str())));
    SendDlgItemMessageW(dlg, id, CB_SETITEMDATA, static_cast<WPARAM>(i),
                        static_cast<LPARAM>(data));
}

void combo_select_data(HWND dlg, int id, int data)
{
    const int n = static_cast<int>(SendDlgItemMessageW(dlg, id, CB_GETCOUNT, 0, 0));
    for (int i = 0; i < n; ++i) {
        const int d = static_cast<int>(
            SendDlgItemMessageW(dlg, id, CB_GETITEMDATA, static_cast<WPARAM>(i), 0));
        if (d == data) {
            SendDlgItemMessageW(dlg, id, CB_SETCURSEL, static_cast<WPARAM>(i), 0);
            return;
        }
    }
    SendDlgItemMessageW(dlg, id, CB_SETCURSEL, 0, 0);
}

int combo_selected_data(HWND dlg, int id, int fallback)
{
    const int i = static_cast<int>(SendDlgItemMessageW(dlg, id, CB_GETCURSEL, 0, 0));
    if (i == CB_ERR) {
        return fallback;
    }
    return static_cast<int>(
        SendDlgItemMessageW(dlg, id, CB_GETITEMDATA, static_cast<WPARAM>(i), 0));
}

// Fills a 0..max numeric combo, tagging the value this voice's firmware preset
// uses. Knowing which setting is "the voice as designed" is genuinely useful
// and costs nothing to announce.
void fill_numeric(HWND dlg, int id, int lo, int hi, int preset,
                  const wchar_t* lo_hint = nullptr,
                  const wchar_t* hi_hint = nullptr)
{
    SendDlgItemMessageW(dlg, id, CB_RESETCONTENT, 0, 0);
    for (int v = lo; v <= hi; ++v) {
        std::wstring s = std::to_wstring(v);
        if (v == lo && lo_hint) {
            s += L" (";
            s += lo_hint;
            s += L")";
        } else if (v == hi && hi_hint) {
            s += L" (";
            s += hi_hint;
            s += L")";
        }
        if (v == preset) {
            s += L" - voice default";
        }
        combo_add(dlg, id, s, v);
    }
}

int current_voice(HWND dlg)
{
    const int v = combo_selected_data(dlg, IDC_VOICE, 0);
    return (v >= 0 && v < VOICE_COUNT) ? v : 0;
}

// ---------------------------------------------------------------- preview

bool ensure_engine(HWND dlg)
{
    if (g_engine) {
        return true;
    }
    try {
        g_lib = std::make_unique<Library>(GetModuleHandleW(nullptr));
        g_engine = std::make_unique<Engine>(*g_lib);
        return true;
    }
    catch (const std::exception& e) {
        g_lib.reset();
        g_engine.reset();
        DT_ERROR("config: engine unavailable - %s", e.what());
        set_status(dlg, L"Speech engine unavailable - see the log folder.");
        return false;
    }
}

void build_wav(const std::vector<int16_t>& pcm, uint32_t rate, std::vector<BYTE>& out)
{
    const uint32_t data_bytes = static_cast<uint32_t>(pcm.size() * sizeof(int16_t));
    out.clear();
    out.reserve(44 + data_bytes);

    auto put = [&](const void* p, size_t n) {
        const BYTE* b = static_cast<const BYTE*>(p);
        out.insert(out.end(), b, b + n);
    };
    auto u32 = [&](uint32_t v) { put(&v, 4); };
    auto u16 = [&](uint16_t v) { put(&v, 2); };

    put("RIFF", 4);
    u32(36 + data_bytes);
    put("WAVEfmt ", 8);
    u32(16);
    u16(1);                      // WAVE_FORMAT_PCM
    u16(1);                      // mono
    u32(rate);
    u32(rate * 2);               // byte rate
    u16(2);                      // block align
    u16(16);                     // bits
    put("data", 4);
    u32(data_bytes);
    put(pcm.data(), data_bytes);
}

void speak_preview(HWND dlg)
{
    if (!ensure_engine(dlg)) {
        return;
    }

    wchar_t text[512]{};
    GetDlgItemTextW(dlg, IDC_PREVIEWTEXT, text, 512);
    if (text[0] == L'\0') {
        wcscpy_s(text, L"The quick brown fox jumps over the lazy dog.");
    }

    const int voice = current_voice(dlg);
    const VoiceSettings vs = g_store->voice(voice);

    try {
        g_engine->apply_global(g_store->global());

        const std::string bytes =
            build_prefix(VOICES[voice], vs) + sanitize(text) + "\r";
        g_engine->begin(bytes);

        std::vector<int16_t> pcm, chunk;
        const size_t cap = static_cast<size_t>(g_engine->output_rate()) * 30;
        while (g_engine->render(chunk) > 0) {
            pcm.insert(pcm.end(), chunk.begin(), chunk.end());
            if (pcm.size() > cap) {
                break;
            }
        }

        // Stop whatever is playing before the buffer behind it is replaced -
        // PlaySound reads g_wav asynchronously, so freeing it while a previous
        // preview is still playing would read freed memory.
        PlaySoundW(nullptr, nullptr, 0);
        build_wav(pcm, g_engine->output_rate(), g_wav);
        PlaySoundW(reinterpret_cast<LPCWSTR>(g_wav.data()), nullptr,
                   SND_MEMORY | SND_ASYNC | SND_NODEFAULT);
    }
    catch (const std::exception& e) {
        DT_ERROR("config: preview failed - %s", e.what());
        set_status(dlg, L"Could not render the preview - see the log folder.");
    }
}

// ---------------------------------------------------------------- state

// Pushes the dialog's current values into the store and writes them out.
void collect_and_save(HWND dlg)
{
    if (g_loading) {
        return;
    }

    const int voice = current_voice(dlg);

    VoiceSettings vs{};
    vs.articulation = combo_selected_data(dlg, IDC_ARTIC,   5);
    vs.expression   = combo_selected_data(dlg, IDC_EXPR,    5);
    vs.formant      = combo_selected_data(dlg, IDC_FORMANT, 5);
    vs.reverb       = combo_selected_data(dlg, IDC_REVERB,  0);
    vs.tone         = combo_selected_data(dlg, IDC_TONE,    1);
    vs.rate         = combo_selected_data(dlg, IDC_RATE,    RATE_DEFAULT);
    vs.pitch        = combo_selected_data(dlg, IDC_PITCH,   50);
    vs.volume       = combo_selected_data(dlg, IDC_VOLUME,  VOLUME_DEFAULT);
    g_store->set_voice(voice, vs);

    GlobalSettings g = g_store->global();
    g.filter_hz   = combo_selected_data(dlg, IDC_FILTER,   FILTER_DEFAULT_HZ);
    g.rate_boost  = combo_selected_data(dlg, IDC_BOOST,    0);
    g.output_rate = combo_selected_data(dlg, IDC_OUTRATE,  22050);
    g.log_level   = combo_selected_data(dlg, IDC_LOGLEVEL, 3);
    g_store->set_global(g);

    log::set_level(static_cast<log::Level>(g.log_level));

    set_status(dlg, g_store->save() ? L"Saved." : L"Could not save settings.");
}

// Repopulates every value control from the store for the selected voice.
void load_voice_into_dialog(HWND dlg)
{
    const bool was = g_loading;
    g_loading = true;

    const int voice = current_voice(dlg);
    const VoicePreset& p = VOICES[voice].preset;
    const VoiceSettings vs = g_store->voice(voice);

    fill_numeric(dlg, IDC_ARTIC,   ARTIC_MIN,   ARTIC_MAX,   p.articulation, L"slurred", L"clipped");
    fill_numeric(dlg, IDC_EXPR,    EXPR_MIN,    EXPR_MAX,    p.expression,   L"monotone", L"most expressive");
    fill_numeric(dlg, IDC_FORMANT, FORMANT_MIN, FORMANT_MAX, p.formant,      L"lowest", L"highest");
    fill_numeric(dlg, IDC_REVERB,  REVERB_MIN,  REVERB_MAX,  p.reverb,       L"none", L"most");
    fill_numeric(dlg, IDC_RATE,    RATE_MIN,    RATE_MAX,    RATE_DEFAULT,   L"slowest", L"fastest");
    fill_numeric(dlg, IDC_VOLUME,  VOLUME_MIN,  VOLUME_MAX,  VOLUME_DEFAULT, L"quietest", L"loudest");
    fill_numeric(dlg, IDC_PITCH,   PITCH_MIN,   PITCH_MAX,   p.pitch,        L"lowest", L"highest");

    SendDlgItemMessageW(dlg, IDC_TONE, CB_RESETCONTENT, 0, 0);
    for (int i = TONE_MIN; i <= TONE_MAX; ++i) {
        std::wstring s = widen(TONE_LABELS[i]);
        if (i == p.tone) {
            s += L" - voice default";
        }
        combo_add(dlg, IDC_TONE, s, i);
    }

    combo_select_data(dlg, IDC_ARTIC,   vs.articulation);
    combo_select_data(dlg, IDC_EXPR,    vs.expression);
    combo_select_data(dlg, IDC_FORMANT, vs.formant);
    combo_select_data(dlg, IDC_REVERB,  vs.reverb);
    combo_select_data(dlg, IDC_TONE,    vs.tone);
    combo_select_data(dlg, IDC_RATE,    vs.rate);
    combo_select_data(dlg, IDC_PITCH,   vs.pitch);
    combo_select_data(dlg, IDC_VOLUME,  vs.volume);

    g_loading = was;
}

void load_globals_into_dialog(HWND dlg)
{
    const bool was = g_loading;
    g_loading = true;

    const GlobalSettings g = g_store->global();

    SendDlgItemMessageW(dlg, IDC_FILTER, CB_RESETCONTENT, 0, 0);
    for (int i = 0; i < FILTER_COUNT; ++i) {
        combo_add(dlg, IDC_FILTER, widen(FILTERS[i].label), FILTERS[i].hz);
    }
    combo_select_data(dlg, IDC_FILTER, g.filter_hz);

    // The engine reports how far the rate-table rescale has been verified as
    // safe; offering levels beyond that would be offering a broken voice.
    int max_boost = 2;
    if (g_engine) {
        max_boost = g_engine->native_rate() ? g_lib->rate_boost_max() : 2;
    } else if (g_lib) {
        max_boost = g_lib->rate_boost_max();
    }
    SendDlgItemMessageW(dlg, IDC_BOOST, CB_RESETCONTENT, 0, 0);
    for (int i = 0; i <= max_boost; ++i) {
        std::wstring s = std::to_wstring(i);
        if (i == 0) {
            s += L" (off - authentic card speed)";
        } else if (i == max_boost) {
            s += L" (fastest)";
        }
        combo_add(dlg, IDC_BOOST, s, i);
    }
    combo_select_data(dlg, IDC_BOOST, g.rate_boost);

    SendDlgItemMessageW(dlg, IDC_OUTRATE, CB_RESETCONTENT, 0, 0);
    combo_add(dlg, IDC_OUTRATE, L"Card native (10504 Hz)", 0);
    combo_add(dlg, IDC_OUTRATE, L"11025 Hz", 11025);
    combo_add(dlg, IDC_OUTRATE, L"22050 Hz (recommended)", 22050);
    combo_add(dlg, IDC_OUTRATE, L"44100 Hz", 44100);
    combo_select_data(dlg, IDC_OUTRATE, g.output_rate);

    SendDlgItemMessageW(dlg, IDC_LOGLEVEL, CB_RESETCONTENT, 0, 0);
    combo_add(dlg, IDC_LOGLEVEL, L"Off",                 0);
    combo_add(dlg, IDC_LOGLEVEL, L"Errors only",         1);
    combo_add(dlg, IDC_LOGLEVEL, L"Errors and warnings", 2);
    combo_add(dlg, IDC_LOGLEVEL, L"Normal",              3);
    combo_add(dlg, IDC_LOGLEVEL, L"Verbose (debug)",     4);
    combo_select_data(dlg, IDC_LOGLEVEL, g.log_level);

    g_loading = was;
}

void open_log_folder(HWND dlg)
{
    std::wstring p = log::path();
    const size_t slash = p.find_last_of(L'\\');
    if (slash != std::wstring::npos) {
        p.resize(slash);
    }
    if (p.empty()) {
        set_status(dlg, L"No log folder yet.");
        return;
    }
    ShellExecuteW(dlg, L"open", p.c_str(), nullptr, nullptr, SW_SHOWNORMAL);
    set_status(dlg, L"Opened the log folder.");
}

INT_PTR CALLBACK dlg_proc(HWND dlg, UINT msg, WPARAM wp, LPARAM /*lp*/)
{
    switch (msg) {
        case WM_INITDIALOG: {
            const HICON icon = LoadIconW(GetModuleHandleW(nullptr),
                                         MAKEINTRESOURCEW(IDI_APP));
            SendMessageW(dlg, WM_SETICON, ICON_BIG,   reinterpret_cast<LPARAM>(icon));
            SendMessageW(dlg, WM_SETICON, ICON_SMALL, reinterpret_cast<LPARAM>(icon));

            g_loading = true;
            SendDlgItemMessageW(dlg, IDC_VOICE, CB_RESETCONTENT, 0, 0);
            for (int i = 0; i < VOICE_COUNT; ++i) {
                std::wstring s = widen(VOICES[i].name);
                // Being told up front that three of the eight are the same
                // voice saves a user hunting for a difference that is not there.
                if (VOICES[i].preset_index != i) {
                    s += L" (same voice as ";
                    s += widen(VOICES[VOICES[i].preset_index].name);
                    s += L")";
                }
                combo_add(dlg, IDC_VOICE, s, i);
            }
            SendDlgItemMessageW(dlg, IDC_VOICE, CB_SETCURSEL, 0, 0);

            SetDlgItemTextW(dlg, IDC_PREVIEWTEXT,
                            L"The quick brown fox jumps over the lazy dog.");

            // Bring the engine up now so the rate-boost list reflects what the
            // DLL actually supports rather than a guess.
            ensure_engine(dlg);

            load_globals_into_dialog(dlg);
            load_voice_into_dialog(dlg);
            g_loading = false;

            set_status(dlg, L"Ready. Changes apply and save immediately.");
            return TRUE;
        }

        case WM_COMMAND: {
            const int id   = LOWORD(wp);
            const int code = HIWORD(wp);

            if (code == CBN_SELCHANGE && !g_loading) {
                if (id == IDC_VOICE) {
                    // A different voice has a different preset, so the value
                    // lists are rebuilt before the new values are read back.
                    load_voice_into_dialog(dlg);
                    collect_and_save(dlg);
                    speak_preview(dlg);
                    return TRUE;
                }
                if (id == IDC_ARTIC || id == IDC_EXPR || id == IDC_FORMANT ||
                    id == IDC_REVERB || id == IDC_TONE || id == IDC_RATE ||
                    id == IDC_PITCH  || id == IDC_VOLUME || id == IDC_FILTER ||
                    id == IDC_BOOST  || id == IDC_OUTRATE) {
                    collect_and_save(dlg);
                    speak_preview(dlg);
                    return TRUE;
                }
                if (id == IDC_LOGLEVEL) {
                    // Not audible, so no preview - just persist it.
                    collect_and_save(dlg);
                    return TRUE;
                }
            }

            switch (id) {
                case IDC_SPEAK:
                    collect_and_save(dlg);
                    speak_preview(dlg);
                    return TRUE;

                case IDC_RESETVOICE:
                    g_store->reset_voice(current_voice(dlg));
                    load_voice_into_dialog(dlg);
                    collect_and_save(dlg);
                    set_status(dlg, L"This voice was reset to its firmware defaults.");
                    speak_preview(dlg);
                    return TRUE;

                case IDC_RESETALL:
                    g_store->reset_all();
                    load_globals_into_dialog(dlg);
                    load_voice_into_dialog(dlg);
                    collect_and_save(dlg);
                    set_status(dlg, L"All voices and settings were restored to defaults.");
                    speak_preview(dlg);
                    return TRUE;

                case IDC_OPENLOGS:
                    open_log_folder(dlg);
                    return TRUE;

                case IDOK:
                case IDCANCEL:
                    // Persist on the way out as well as on every change, so a
                    // value edited and never re-focused is still saved.
                    collect_and_save(dlg);
                    PlaySoundW(nullptr, nullptr, 0);
                    EndDialog(dlg, 0);
                    return TRUE;

                default:
                    break;
            }
            return FALSE;
        }

        case WM_CLOSE:
            collect_and_save(dlg);
            PlaySoundW(nullptr, nullptr, 0);
            EndDialog(dlg, 0);
            return TRUE;

        default:
            return FALSE;
    }
}

}

int APIENTRY wWinMain(HINSTANCE hInstance, HINSTANCE, LPWSTR, int)
{
    log::init(L"config");

    Store store;
    store.load();
    g_store = &store;
    log::set_level(static_cast<log::Level>(store.global().log_level));
    DT_INFO("config: started, settings at %ls", store.path().c_str());

    INITCOMMONCONTROLSEX icc{ sizeof(icc), ICC_STANDARD_CLASSES | ICC_BAR_CLASSES };
    InitCommonControlsEx(&icc);

    DialogBoxParamW(hInstance, MAKEINTRESOURCEW(IDD_CONFIG), nullptr, dlg_proc, 0);

    PlaySoundW(nullptr, nullptr, 0);
    g_engine.reset();
    g_lib.reset();
    DT_INFO("config: exiting");
    log::shutdown();
    return 0;
}
