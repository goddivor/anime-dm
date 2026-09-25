#include "ui/HelpDialogs.h"

#include <shellapi.h>

#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <string>

#include "ui/Resource.h"
#include "ui/Theme.h"
#include "ui/Strings.h"

namespace {

// Applies the active language to the shortcuts dialog.
void RetranslateShortcuts(HWND dialog) {
    SetDialogTitle(dialog, STR_DLG_SHORTCUTS_TITLE);
    SetDialogText(dialog, IDC_SC_KEY_DELETE, STR_KEY_DELETE);
    SetDialogText(dialog, IDC_SC_KEY_BATCH, STR_KEY_BATCH);
    SetDialogText(dialog, IDC_SC_KEY_ESCAPE, STR_KEY_ESCAPE);
    SetDialogText(dialog, IDC_SC_TXT_ADD, STR_SC_ADD);
    SetDialogText(dialog, IDC_SC_TXT_REMOVE, STR_SC_REMOVE);
    SetDialogText(dialog, IDC_SC_TXT_SEARCH, STR_SC_SEARCH);
    SetDialogText(dialog, IDC_SC_TXT_BATCH, STR_SC_BATCH);
    SetDialogText(dialog, IDC_SC_TXT_HELP, STR_SC_HELP);
    SetDialogText(dialog, IDC_SC_TXT_CLOSE, STR_SC_CLOSE);
    SetDialogText(dialog, IDOK, STR_DLG_OK);
}

// Generic procedure for static information dialogs: closes on OK/Cancel.
INT_PTR CALLBACK InfoDialogProc(HWND dialog, UINT msg, WPARAM wParam, LPARAM lParam) {
    INT_PTR colour = 0;
    if (ThemeDialogMessage(msg, wParam, &colour)) {
        return colour;
    }

    switch (msg) {
    case WM_INITDIALOG:
        ActiveTheme().ApplyToDialog(dialog);
        RetranslateShortcuts(dialog);
        return TRUE;
    case WM_COMMAND:
        if (LOWORD(wParam) == IDOK || LOWORD(wParam) == IDCANCEL) {
            EndDialog(dialog, LOWORD(wParam));
            return TRUE;
        }
        return FALSE;
    case WM_CLOSE:
        EndDialog(dialog, IDCANCEL);
        return TRUE;
    default:
        return FALSE;
    }
}

// --- about -------------------------------------------------------------------

#ifndef ADM_VERSION
#define ADM_VERSION L"0.0.0"
#endif

constexpr wchar_t kWebsite[] = L"https://goddivor.github.io/anime-dm/";
constexpr wchar_t kSupport[] = L"https://goddivor.github.io/anime-dm/support/";
constexpr wchar_t kSource[] = L"https://github.com/goddivor/anime-dm";

// What the about dialog holds on to while it is open.
struct AboutState {
    HFONT title = nullptr;
    HFONT link = nullptr;
};

// The day the executable was built, as the date of a version: "25/09/2026".
std::wstring BuildDate() {
    static const char* kMonths[] = {"Jan", "Feb", "Mar", "Apr", "May", "Jun",
                                    "Jul", "Aug", "Sep", "Oct", "Nov", "Dec"};
    const char* date = __DATE__;
    int month = 0;
    for (int index = 0; index < 12; ++index) {
        if (std::strncmp(date, kMonths[index], 3) == 0) {
            month = index + 1;
        }
    }
    wchar_t text[16] = {};
    swprintf(text, 16, L"%02d/%02d/", std::atoi(date + 4), month);
    return std::wstring(text) + std::wstring(date + 7, date + 11);
}

// A copy of the font of a control, larger and bold, or underlined.
HFONT DerivedFont(HWND control, int scale, bool bold, bool underline) {
    LOGFONTW font = {};
    GetObjectW(reinterpret_cast<HFONT>(SendMessageW(control, WM_GETFONT, 0, 0)), sizeof(font), &font);
    font.lfHeight = font.lfHeight * scale / 100;
    if (bold) {
        font.lfWeight = FW_SEMIBOLD;
    }
    font.lfUnderline = underline ? TRUE : FALSE;
    return CreateFontIndirectW(&font);
}

bool IsLink(int id) {
    return id == IDC_ABOUT_WEBSITE || id == IDC_ABOUT_SUPPORT;
}

void Open(HWND dialog, const wchar_t* address) {
    ShellExecuteW(dialog, L"open", address, nullptr, nullptr, SW_SHOWNORMAL);
}

// Fills the about dialog in the active language.
void InitAbout(HWND dialog, AboutState& state) {
    SetDialogTitle(dialog, STR_DLG_ABOUT_TITLE);
    wchar_t version[96] = {};
    swprintf(version, 96, Str(STR_ABOUT_VERSION), ADM_VERSION, BuildDate().c_str());
    SetDlgItemTextW(dialog, IDC_ABOUT_VERSION, version);
    SetDialogText(dialog, IDC_ABOUT_UPDATE, STR_ABOUT_UPDATE);
    SetDialogText(dialog, IDC_ABOUT_LBL_PROJECT, STR_ABOUT_PROJECT);
    SetDialogText(dialog, IDC_ABOUT_TAGLINE, STR_DLG_ABOUT_TAGLINE);
    SetDialogText(dialog, IDC_ABOUT_ADDONS, STR_VIEW_ADDONS);
    SetDialogText(dialog, IDC_ABOUT_SOURCE, STR_ABOUT_SOURCE);
    SetDialogText(dialog, IDC_ABOUT_NOTICE, STR_ABOUT_NOTICE);
    SetDialogText(dialog, IDC_ABOUT_LBL_WEBSITE, STR_ABOUT_WEBSITE);
    SetDlgItemTextW(dialog, IDC_ABOUT_WEBSITE, kWebsite);
    SetDialogText(dialog, IDC_ABOUT_LBL_SUPPORT, STR_ABOUT_SUPPORT);
    SetDlgItemTextW(dialog, IDC_ABOUT_SUPPORT, kSupport);
    SetDialogText(dialog, IDC_ABOUT_COPYRIGHT, STR_ABOUT_COPYRIGHT);
    SetDialogText(dialog, IDOK, STR_DLG_CLOSE);

    // Checking for updates comes with the first published release.
    EnableWindow(GetDlgItem(dialog, IDC_ABOUT_UPDATE), FALSE);

    state.title = DerivedFont(GetDlgItem(dialog, IDC_ABOUT_NAME), 160, true, false);
    SendDlgItemMessageW(dialog, IDC_ABOUT_NAME, WM_SETFONT, reinterpret_cast<WPARAM>(state.title), TRUE);
    state.link = DerivedFont(GetDlgItem(dialog, IDC_ABOUT_WEBSITE), 100, false, true);
    for (int id : {IDC_ABOUT_WEBSITE, IDC_ABOUT_SUPPORT}) {
        SendDlgItemMessageW(dialog, id, WM_SETFONT, reinterpret_cast<WPARAM>(state.link), TRUE);
    }
}

// The about dialog: the name and version, the community, the notice, the
// links, and the Addon Store handed back to the caller.
INT_PTR CALLBACK AboutDialogProc(HWND dialog, UINT msg, WPARAM wParam, LPARAM lParam) {
    auto* state = reinterpret_cast<AboutState*>(GetWindowLongPtrW(dialog, GWLP_USERDATA));
    if (msg == WM_CTLCOLORSTATIC && IsLink(GetDlgCtrlID(reinterpret_cast<HWND>(lParam)))) {
        // The links wear the accent of the palette over the usual background.
        INT_PTR brush = 0;
        HDC dc = reinterpret_cast<HDC>(wParam);
        if (!ThemeDialogMessage(msg, wParam, &brush)) {
            SetBkColor(dc, GetSysColor(COLOR_3DFACE));
            brush = reinterpret_cast<INT_PTR>(GetSysColorBrush(COLOR_3DFACE));
        }
        SetTextColor(dc, ActiveTheme().Colors().accent);
        return brush;
    }
    INT_PTR colour = 0;
    if (ThemeDialogMessage(msg, wParam, &colour)) {
        return colour;
    }

    switch (msg) {
    case WM_INITDIALOG:
        state = new AboutState();
        SetWindowLongPtrW(dialog, GWLP_USERDATA, reinterpret_cast<LONG_PTR>(state));
        ActiveTheme().ApplyToDialog(dialog);
        InitAbout(dialog, *state);
        return TRUE;
    case WM_SETCURSOR:
        if (IsLink(GetDlgCtrlID(reinterpret_cast<HWND>(wParam)))) {
            SetCursor(LoadCursorW(nullptr, IDC_HAND));
            SetWindowLongPtrW(dialog, DWLP_MSGRESULT, TRUE);
            return TRUE;
        }
        return FALSE;
    case WM_COMMAND:
        switch (LOWORD(wParam)) {
        case IDC_ABOUT_WEBSITE:
            Open(dialog, kWebsite);
            return TRUE;
        case IDC_ABOUT_SUPPORT:
            Open(dialog, kSupport);
            return TRUE;
        case IDC_ABOUT_SOURCE:
            Open(dialog, kSource);
            return TRUE;
        case IDC_ABOUT_ADDONS:
            EndDialog(dialog, IDC_ABOUT_ADDONS);
            return TRUE;
        case IDOK:
        case IDCANCEL:
            EndDialog(dialog, IDOK);
            return TRUE;
        default:
            return FALSE;
        }
    case WM_CLOSE:
        EndDialog(dialog, IDOK);
        return TRUE;
    case WM_NCDESTROY:
        if (state != nullptr) {
            DeleteObject(state->title);
            DeleteObject(state->link);
            delete state;
            SetWindowLongPtrW(dialog, GWLP_USERDATA, 0);
        }
        return FALSE;
    default:
        return FALSE;
    }
}

}  // namespace

// Runs the about dialog modally.
INT_PTR ShowAboutDialog(HWND owner, HINSTANCE instance) {
    return DialogBoxParamW(instance, MAKEINTRESOURCEW(IDD_ABOUT), owner, AboutDialogProc, 0);
}

// Runs the shortcuts dialog modally.
void ShowShortcutsDialog(HWND owner, HINSTANCE instance) {
    DialogBoxParamW(instance, MAKEINTRESOURCEW(IDD_SHORTCUTS), owner, InfoDialogProc, IDD_SHORTCUTS);
}
