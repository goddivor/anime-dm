#include "ui/Theme.h"

#include <commctrl.h>
#include <dwmapi.h>
#include <uxtheme.h>

namespace {

constexpr DWORD kImmersiveDarkMode = 20;
constexpr DWORD kImmersiveDarkModeLegacy = 19;

// Undocumented uxtheme entry points; the menus and dialogs of every dark-mode
// Win32 application go through them, and they are absent before Windows 10 1809.
using SetPreferredAppModeFn = int(WINAPI*)(int);
using FlushMenuThemesFn = void(WINAPI*)();

// Loads uxtheme.dll once and keeps it for the process lifetime.
HMODULE UxTheme() {
    static HMODULE module = LoadLibraryExW(L"uxtheme.dll", nullptr, LOAD_LIBRARY_SEARCH_SYSTEM32);
    return module;
}

// Asks the shell to draw menus and dialogs in the requested mode.
void SetApplicationMode(bool dark) {
    HMODULE module = UxTheme();
    if (module == nullptr) {
        return;
    }

    auto setMode = reinterpret_cast<SetPreferredAppModeFn>(
        GetProcAddress(module, MAKEINTRESOURCEA(135)));
    auto flush = reinterpret_cast<FlushMenuThemesFn>(
        GetProcAddress(module, MAKEINTRESOURCEA(136)));
    if (setMode == nullptr) {
        return;
    }

    setMode(dark ? 2 : 3);
    if (flush != nullptr) {
        flush();
    }
}

// Reads the user preference the system exposes for applications.
bool SystemPrefersDark() {
    HKEY key = nullptr;
    if (RegOpenKeyExW(HKEY_CURRENT_USER,
                      L"Software\\Microsoft\\Windows\\CurrentVersion\\Themes\\Personalize", 0,
                      KEY_QUERY_VALUE, &key) != ERROR_SUCCESS) {
        return false;
    }

    DWORD value = 1;
    DWORD size = sizeof(value);
    DWORD type = 0;
    LSTATUS status = RegQueryValueExW(key, L"AppsUseLightTheme", nullptr, &type,
                                      reinterpret_cast<LPBYTE>(&value), &size);
    RegCloseKey(key);

    return status == ERROR_SUCCESS && type == REG_DWORD && value == 0;
}

// A themed header keeps its dark background but draws its captions with the
// light-mode text colour, so the colour is forced through custom draw.
LRESULT CALLBACK ListSubclass(HWND window, UINT msg, WPARAM wParam, LPARAM lParam,
                              UINT_PTR id, DWORD_PTR data) {
    if (msg == WM_NOTIFY) {
        auto* notify = reinterpret_cast<NMHDR*>(lParam);
        if (notify->code == NM_CUSTOMDRAW) {
            auto* draw = reinterpret_cast<NMCUSTOMDRAW*>(lParam);
            if (draw->dwDrawStage == CDDS_PREPAINT) {
                return CDRF_NOTIFYITEMDRAW;
            }
            if (draw->dwDrawStage == CDDS_ITEMPREPAINT) {
                SetTextColor(draw->hdc, static_cast<COLORREF>(data));
                return CDRF_NEWFONT;
            }
        }
    }
    if (msg == WM_NCDESTROY) {
        RemoveWindowSubclass(window, ListSubclass, id);
    }
    return DefSubclassProc(window, msg, wParam, lParam);
}

}  // namespace

// Releases the cached background brushes.
Theme::~Theme() {
    if (window_ != nullptr) {
        DeleteObject(window_);
    }
    if (surface_ != nullptr) {
        DeleteObject(surface_);
    }
}

// Switches the mode and rebuilds the palette.
void Theme::SetMode(ThemeMode mode) {
    mode_ = mode;
    Refresh();
}

// Recomputes the palette and the cached brushes for the active mode.
void Theme::Refresh() {
    bool dark = mode_ == ThemeMode::Dark ||
                (mode_ == ThemeMode::System && SystemPrefersDark());

    if (dark) {
        colors_ = {RGB(0x1E, 0x1F, 0x22), RGB(0x26, 0x28, 0x2C), RGB(0xE6, 0xE6, 0xE6),
                   RGB(0x3A, 0x3D, 0x41), RGB(0x3B, 0x82, 0xF6), RGB(0xFF, 0xFF, 0xFF),
                   RGB(0x32, 0x35, 0x3A), RGB(0x80, 0x82, 0x86), RGB(0x4A, 0xDE, 0x80),
                   RGB(0xF8, 0x71, 0x71), true};
    } else {
        colors_ = {GetSysColor(COLOR_WINDOW),     GetSysColor(COLOR_BTNFACE),
                   GetSysColor(COLOR_WINDOWTEXT), GetSysColor(COLOR_BTNSHADOW),
                   RGB(0x1D, 0x6F, 0xD6),         RGB(0xFF, 0xFF, 0xFF),
                   RGB(0xE4, 0xEC, 0xF7),         GetSysColor(COLOR_GRAYTEXT),
                   RGB(0x16, 0xA3, 0x4A),         RGB(0xDC, 0x26, 0x26),
                   false};
    }

    if (window_ != nullptr) {
        DeleteObject(window_);
    }
    if (surface_ != nullptr) {
        DeleteObject(surface_);
    }
    window_ = CreateSolidBrush(colors_.window);
    surface_ = CreateSolidBrush(colors_.surface);

    SetApplicationMode(colors_.dark);
}

// Repaints the title bar to match the palette.
void Theme::ApplyToFrame(HWND window) const {
    BOOL dark = colors_.dark ? TRUE : FALSE;
    if (FAILED(DwmSetWindowAttribute(window, kImmersiveDarkMode, &dark, sizeof(dark)))) {
        DwmSetWindowAttribute(window, kImmersiveDarkModeLegacy, &dark, sizeof(dark));
    }
    SetWindowPos(window, nullptr, 0, 0, 0, 0,
                 SWP_NOMOVE | SWP_NOSIZE | SWP_NOZORDER | SWP_FRAMECHANGED);
}

// Pushes the palette onto a report-mode list view and its column header.
void Theme::ApplyToList(HWND list) const {
    SetWindowTheme(list, colors_.dark ? L"DarkMode_Explorer" : L"Explorer", nullptr);

    HWND header = ListView_GetHeader(list);
    if (header != nullptr) {
        SetWindowTheme(header, colors_.dark ? L"DarkMode_ItemsView" : L"ItemsView", nullptr);
    }

    SetWindowSubclass(list, ListSubclass, 1, static_cast<DWORD_PTR>(colors_.text));

    ListView_SetBkColor(list, colors_.window);
    ListView_SetTextBkColor(list, colors_.window);
    ListView_SetTextColor(list, colors_.text);

    // The grid lines are drawn with a fixed light colour that glares on a dark
    // background, so they only stay on in the light palette.
    DWORD style = ListView_GetExtendedListViewStyle(list);
    if (colors_.dark) {
        style &= ~static_cast<DWORD>(LVS_EX_GRIDLINES);
    } else {
        style |= LVS_EX_GRIDLINES;
    }
    ListView_SetExtendedListViewStyle(list, style);

    InvalidateRect(list, nullptr, TRUE);
}

// Pushes the palette onto a tree view.
void Theme::ApplyToTree(HWND tree) const {
    SetWindowTheme(tree, colors_.dark ? L"DarkMode_Explorer" : L"Explorer", nullptr);
    TreeView_SetBkColor(tree, colors_.window);
    TreeView_SetTextColor(tree, colors_.text);
    TreeView_SetLineColor(tree, colors_.line);
    InvalidateRect(tree, nullptr, TRUE);
}

namespace {

// Pushes the palette onto one child control of a dialog.
BOOL CALLBACK ThemeChild(HWND child, LPARAM data) {
    const Theme& theme = *reinterpret_cast<const Theme*>(data);
    bool dark = theme.IsDark();

    wchar_t name[64] = {};
    GetClassNameW(child, name, ARRAYSIZE(name));

    if (lstrcmpiW(name, WC_LISTVIEWW) == 0) {
        theme.ApplyToList(child);
    } else if (lstrcmpiW(name, WC_TREEVIEWW) == 0) {
        theme.ApplyToTree(child);
    } else if (lstrcmpiW(name, WC_EDITW) == 0 || lstrcmpiW(name, WC_COMBOBOXW) == 0) {
        SetWindowTheme(child, dark ? L"DarkMode_CFD" : L"CFD", nullptr);
    } else if (lstrcmpiW(name, WC_BUTTONW) == 0) {
        // A themed check box or radio button draws its caption in the colour
        // of the visual style, black whatever the dialog answers; without the
        // style, the caption follows WM_CTLCOLORSTATIC like any label.
        LONG style = GetWindowLongW(child, GWL_STYLE) & BS_TYPEMASK;
        bool ticks = style == BS_CHECKBOX || style == BS_AUTOCHECKBOX ||
                     style == BS_RADIOBUTTON || style == BS_AUTORADIOBUTTON ||
                     style == BS_3STATE || style == BS_AUTO3STATE;
        if (dark && ticks) {
            SetWindowTheme(child, L"", L"");
        } else {
            SetWindowTheme(child, dark ? L"DarkMode_Explorer" : L"Explorer", nullptr);
        }
    }
    return TRUE;
}

}  // namespace

// Repaints a dialog and its controls with the active palette.
void Theme::ApplyToDialog(HWND dialog) const {
    BOOL dark = colors_.dark ? TRUE : FALSE;
    if (FAILED(DwmSetWindowAttribute(dialog, kImmersiveDarkMode, &dark, sizeof(dark)))) {
        DwmSetWindowAttribute(dialog, kImmersiveDarkModeLegacy, &dark, sizeof(dark));
    }
    EnumChildWindows(dialog, ThemeChild, reinterpret_cast<LPARAM>(this));
}

// Returns the brush a control should paint its background with, or zero to
// leave the default handling in charge.
INT_PTR Theme::ControlColor(HDC dc, bool input) const {
    if (!colors_.dark) {
        return 0;
    }
    SetTextColor(dc, colors_.text);
    SetBkColor(dc, input ? colors_.window : colors_.surface);
    return reinterpret_cast<INT_PTR>(input ? window_ : surface_);
}

// The palette every window of the application shares.
Theme& ActiveTheme() {
    static Theme theme;
    return theme;
}

// Answers the colour messages common to every dialog; true when handled.
bool ThemeDialogMessage(UINT msg, WPARAM wParam, INT_PTR* result) {
    bool input = msg == WM_CTLCOLOREDIT || msg == WM_CTLCOLORLISTBOX;
    bool surface = msg == WM_CTLCOLORDLG || msg == WM_CTLCOLORSTATIC || msg == WM_CTLCOLORBTN;
    if (!input && !surface) {
        return false;
    }

    INT_PTR brush = ActiveTheme().ControlColor(reinterpret_cast<HDC>(wParam), input);
    if (brush == 0) {
        return false;
    }
    *result = brush;
    return true;
}
