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
                   RGB(0x3A, 0x3D, 0x41), true};
    } else {
        colors_ = {GetSysColor(COLOR_WINDOW), GetSysColor(COLOR_BTNFACE),
                   GetSysColor(COLOR_WINDOWTEXT), GetSysColor(COLOR_BTNSHADOW), false};
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

// Pushes the palette onto a report-mode list view.
void Theme::ApplyToList(HWND list) const {
    SetWindowTheme(list, colors_.dark ? L"DarkMode_Explorer" : L"Explorer", nullptr);
    ListView_SetBkColor(list, colors_.window);
    ListView_SetTextBkColor(list, colors_.window);
    ListView_SetTextColor(list, colors_.text);
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
