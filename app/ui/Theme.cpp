#include "ui/Theme.h"

#include "ui/Resource.h"

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
// Paints one column caption of a header in the colours of the palette, so the
// header shares the tone of the rows instead of the grey of the visual style.
void DrawHeaderItem(HWND header, NMCUSTOMDRAW* draw) {
    const ThemeColors& colors = ActiveTheme().Colors();
    HDC dc = draw->hdc;
    RECT cell = draw->rc;
    HBRUSH background = CreateSolidBrush(colors.header);
    FillRect(dc, &cell, background);
    DeleteObject(background);

    wchar_t text[256] = {};
    HDITEMW item = {};
    item.mask = HDI_TEXT | HDI_FORMAT;
    item.pszText = text;
    item.cchTextMax = ARRAYSIZE(text);
    Header_GetItem(header, static_cast<int>(draw->dwItemSpec), &item);

    HFONT font = reinterpret_cast<HFONT>(SendMessageW(header, WM_GETFONT, 0, 0));
    HFONT previous = font != nullptr ? static_cast<HFONT>(SelectObject(dc, font)) : nullptr;
    SetBkMode(dc, TRANSPARENT);
    SetTextColor(dc, colors.text);
    cell.left += 6;
    cell.right -= 6;
    UINT align = (item.fmt & HDF_JUSTIFYMASK) == HDF_RIGHT ? DT_RIGHT : DT_LEFT;
    DrawTextW(dc, text, -1, &cell,
              align | DT_SINGLELINE | DT_VCENTER | DT_END_ELLIPSIS | DT_NOPREFIX);
    if (previous != nullptr) {
        SelectObject(dc, previous);
    }
}

// Draws the rule under the header and the dividers between its columns, in
// the colour of the palette, once the header painted itself. The stretch
// past the last column takes the tone of the rows too.
void RuleHeader(HWND header, HDC dc) {
    RECT client = {};
    GetClientRect(header, &client);
    const ThemeColors& colors = ActiveTheme().Colors();

    int columns = Header_GetItemCount(header);
    RECT tail = client;
    RECT last = {};
    if (columns > 0 && Header_GetItemRect(header, columns - 1, &last)) {
        tail.left = last.right;
    }
    HBRUSH background = CreateSolidBrush(colors.header);
    FillRect(dc, &tail, background);
    DeleteObject(background);

    HPEN pen = CreatePen(PS_SOLID, 1, colors.line);
    HPEN previous = static_cast<HPEN>(SelectObject(dc, pen));

    MoveToEx(dc, client.left, client.bottom - 1, nullptr);
    LineTo(dc, client.right, client.bottom - 1);

    for (int column = 0; column < columns; ++column) {
        RECT item = {};
        if (Header_GetItemRect(header, column, &item)) {
            MoveToEx(dc, item.right - 1, item.top, nullptr);
            LineTo(dc, item.right - 1, item.bottom);
        }
    }
    SelectObject(dc, previous);
    DeleteObject(pen);
}

// Paints the one-pixel frame of a bordered control in a colour of the
// palette, over whatever the system drew for the non-client area. Without
// `top`, the upper edge takes the window colour instead: the caption bar
// above it carries the frame there.
void FrameWindow(HWND window, bool top, COLORREF colour) {
    if ((GetWindowLongPtrW(window, GWL_STYLE) & WS_BORDER) == 0) {
        return;
    }
    HDC dc = GetWindowDC(window);
    if (dc == nullptr) {
        return;
    }
    const ThemeColors& colors = ActiveTheme().Colors();
    RECT frame = {};
    GetWindowRect(window, &frame);
    OffsetRect(&frame, -frame.left, -frame.top);
    HBRUSH brush = CreateSolidBrush(colour);
    FrameRect(dc, &frame, brush);
    DeleteObject(brush);
    if (!top) {
        RECT edge = {frame.left, frame.top, frame.right, frame.top + 1};
        HBRUSH blank = CreateSolidBrush(colors.window);
        FillRect(dc, &edge, blank);
        DeleteObject(blank);
    }
    ReleaseDC(window, dc);
}

// Frames a tree the way the lists are framed, minus the upper edge.
LRESULT CALLBACK TreeSubclass(HWND window, UINT msg, WPARAM wParam, LPARAM lParam,
                              UINT_PTR id, DWORD_PTR data) {
    if (msg == WM_NCPAINT) {
        LRESULT result = DefSubclassProc(window, msg, wParam, lParam);
        FrameWindow(window, false, static_cast<COLORREF>(data));
        return result;
    }
    if (msg == WM_NCDESTROY) {
        RemoveWindowSubclass(window, TreeSubclass, id);
    }
    return DefSubclassProc(window, msg, wParam, lParam);
}

LRESULT CALLBACK ListSubclass(HWND window, UINT msg, WPARAM wParam, LPARAM lParam,
                              UINT_PTR id, DWORD_PTR data) {
    if (msg == WM_NOTIFY) {
        auto* notify = reinterpret_cast<NMHDR*>(lParam);
        if (notify->code == NM_CUSTOMDRAW) {
            auto* draw = reinterpret_cast<NMCUSTOMDRAW*>(lParam);
            if (draw->dwDrawStage == CDDS_PREPAINT) {
                return CDRF_NOTIFYITEMDRAW | CDRF_NOTIFYPOSTPAINT;
            }
            if (draw->dwDrawStage == CDDS_ITEMPREPAINT) {
                DrawHeaderItem(notify->hwndFrom, draw);
                return CDRF_SKIPDEFAULT;
            }
            if (draw->dwDrawStage == CDDS_POSTPAINT) {
                RuleHeader(notify->hwndFrom, draw->hdc);
                return CDRF_DODEFAULT;
            }
        }
    }
    if (msg == WM_NCPAINT) {
        LRESULT result = DefSubclassProc(window, msg, wParam, lParam);
        FrameWindow(window, true, static_cast<COLORREF>(data));
        return result;
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
                   RGB(0xF8, 0x71, 0x71), RGB(0x18, 0x19, 0x1B), RGB(0x5E, 0x62, 0x68),
                   true};
    } else {
        colors_ = {GetSysColor(COLOR_WINDOW),     GetSysColor(COLOR_BTNFACE),
                   GetSysColor(COLOR_WINDOWTEXT), GetSysColor(COLOR_BTNSHADOW),
                   RGB(0x1D, 0x6F, 0xD6),         RGB(0xFF, 0xFF, 0xFF),
                   RGB(0xE4, 0xEC, 0xF7),         GetSysColor(COLOR_GRAYTEXT),
                   RGB(0x16, 0xA3, 0x4A),         RGB(0xDC, 0x26, 0x26),
                   RGB(0xF0, 0xF0, 0xF0),         GetSysColor(COLOR_BTNSHADOW),
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

    SetWindowSubclass(list, ListSubclass, 1, static_cast<DWORD_PTR>(colors_.frame));

    ListView_SetBkColor(list, colors_.window);
    ListView_SetTextBkColor(list, colors_.window);
    ListView_SetTextColor(list, colors_.text);

    // The built-in grid lines only come in a fixed light colour: the window
    // draws its own, in the colour of the palette, at the post-paint stage.
    DWORD style = ListView_GetExtendedListViewStyle(list);
    style &= ~static_cast<DWORD>(LVS_EX_GRIDLINES);
    ListView_SetExtendedListViewStyle(list, style);

    InvalidateRect(list, nullptr, TRUE);
    RedrawWindow(list, nullptr, nullptr, RDW_FRAME | RDW_INVALIDATE);
}

// Pushes the palette onto a tree view.
void Theme::ApplyToTree(HWND tree) const {
    SetWindowTheme(tree, colors_.dark ? L"DarkMode_Explorer" : L"Explorer", nullptr);
    TreeView_SetBkColor(tree, colors_.window);
    TreeView_SetTextColor(tree, colors_.text);
    TreeView_SetLineColor(tree, colors_.line);
    // The categories panel is outlined in the text colour, the way IDM
    // frames its own, the caption bar above the tree drawing the upper edge.
    SetWindowSubclass(tree, TreeSubclass, 1, static_cast<DWORD_PTR>(colors_.text));
    InvalidateRect(tree, nullptr, TRUE);
    RedrawWindow(tree, nullptr, nullptr, RDW_FRAME | RDW_INVALIDATE);
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

// Repaints a dialog and its controls with the active palette, and gives it
// the icon of the application in its caption.
void Theme::ApplyToDialog(HWND dialog) const {
    BOOL dark = colors_.dark ? TRUE : FALSE;
    if (FAILED(DwmSetWindowAttribute(dialog, kImmersiveDarkMode, &dark, sizeof(dark)))) {
        DwmSetWindowAttribute(dialog, kImmersiveDarkModeLegacy, &dark, sizeof(dark));
    }
    EnumChildWindows(dialog, ThemeChild, reinterpret_cast<LPARAM>(this));

    // A modal frame hides the caption icon; the dialogs keep their look
    // without it, and show the icon like every other window.
    LONG_PTR extended = GetWindowLongPtrW(dialog, GWL_EXSTYLE);
    if ((extended & WS_EX_DLGMODALFRAME) != 0) {
        SetWindowLongPtrW(dialog, GWL_EXSTYLE, extended & ~WS_EX_DLGMODALFRAME);
        SetWindowPos(dialog, nullptr, 0, 0, 0, 0,
                     SWP_NOMOVE | SWP_NOSIZE | SWP_NOZORDER | SWP_NOACTIVATE | SWP_FRAMECHANGED);
    }
    HINSTANCE instance = reinterpret_cast<HINSTANCE>(GetWindowLongPtrW(dialog, GWLP_HINSTANCE));
    HICON big = LoadIconW(instance, MAKEINTRESOURCEW(IDI_APP));
    HICON small = static_cast<HICON>(LoadImageW(instance, MAKEINTRESOURCEW(IDI_APP), IMAGE_ICON,
                                                GetSystemMetrics(SM_CXSMICON),
                                                GetSystemMetrics(SM_CYSMICON), LR_DEFAULTCOLOR));
    SendMessageW(dialog, WM_SETICON, ICON_BIG, reinterpret_cast<LPARAM>(big));
    SendMessageW(dialog, WM_SETICON, ICON_SMALL, reinterpret_cast<LPARAM>(small));
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
