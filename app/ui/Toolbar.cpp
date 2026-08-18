#include "ui/Toolbar.h"

#include <commctrl.h>
#include <uxtheme.h>

#include "ui/Commands.h"
#include "ui/IconFactory.h"
#include "ui/Strings.h"
#include "ui/Theme.h"

namespace {
constexpr int kIconSize = 24;

struct ButtonSpec {
    int command;
    int icon;
    StringId text;
};

constexpr ButtonSpec kButtons[] = {
    {ID_TASK_ADD, ICON_ADD_URL, STR_TB_ADD},
    {ID_FILE_START, ICON_RESUME, STR_TB_RESUME},
    {ID_FILE_STOP, ICON_STOP, STR_TB_STOP},
    {ID_DOWNLOAD_STOP_ALL, ICON_STOP_ALL, STR_TB_STOP_ALL},
    {ID_FILE_REMOVE, ICON_REMOVE, STR_TB_REMOVE},
    {ID_DOWNLOAD_DELETE_ALL, ICON_REMOVE_ALL, STR_TB_REMOVE_ALL},
    {ID_VIEW_SETTINGS, ICON_OPTIONS, STR_TB_OPTIONS},
    {ID_DOWNLOAD_SCHEDULE, ICON_SCHEDULE, STR_TB_SCHEDULE},
    {ID_VIEW_ADDONS, ICON_ADDONS, STR_TB_ADDONS},
    {ID_DOWNLOAD_SEARCH, ICON_SEARCH, STR_TB_SEARCH},
};
}  // namespace

// Releases the GDI image list owned by the toolbar.
Toolbar::~Toolbar() {
    if (imageList_ != nullptr) {
        ImageList_Destroy(imageList_);
        imageList_ = nullptr;
    }
}

// Creates a flat toolbar of captioned icons pinned to the top of the parent.
bool Toolbar::Create(HWND parent, HINSTANCE instance) {
    hwnd_ = CreateWindowExW(
        0, TOOLBARCLASSNAMEW, nullptr,
        WS_CHILD | WS_VISIBLE | TBSTYLE_FLAT | CCS_TOP | CCS_NODIVIDER,
        0, 0, 0, 0, parent, nullptr, instance, nullptr);
    if (hwnd_ == nullptr) {
        return false;
    }

    SendMessageW(hwnd_, TB_BUTTONSTRUCTSIZE, sizeof(TBBUTTON), 0);
    SendMessageW(hwnd_, TB_SETEXTENDEDSTYLE, 0, TBSTYLE_EX_MIXEDBUTTONS);
    SendMessageW(hwnd_, TB_SETBITMAPSIZE, 0, MAKELPARAM(kIconSize, kIconSize));

    imageList_ = CreateToolbarImageList(GetSysColor(COLOR_BTNTEXT));
    SendMessageW(hwnd_, TB_SETIMAGELIST, 0, reinterpret_cast<LPARAM>(imageList_));

    TBBUTTON buttons[ARRAYSIZE(kButtons)] = {};
    for (size_t i = 0; i < ARRAYSIZE(kButtons); ++i) {
        if (kButtons[i].command == 0) {
            buttons[i].fsStyle = BTNS_SEP;
            continue;
        }
        buttons[i].iBitmap = kButtons[i].icon;
        buttons[i].idCommand = kButtons[i].command;
        buttons[i].fsState = TBSTATE_ENABLED;
        buttons[i].fsStyle = BTNS_AUTOSIZE | BTNS_SHOWTEXT;
        buttons[i].iString = reinterpret_cast<INT_PTR>(Str(kButtons[i].text));
    }

    SendMessageW(hwnd_, TB_ADDBUTTONS, ARRAYSIZE(buttons), reinterpret_cast<LPARAM>(buttons));
    SendMessageW(hwnd_, TB_AUTOSIZE, 0, 0);
    return true;
}

// Refreshes the button captions after a language change.
void Toolbar::Retranslate() {
    for (const ButtonSpec& spec : kButtons) {
        if (spec.command == 0) {
            continue;
        }
        TBBUTTONINFOW info = {};
        info.cbSize = sizeof(info);
        info.dwMask = TBIF_TEXT;
        info.pszText = const_cast<wchar_t*>(Str(spec.text));
        SendMessageW(hwnd_, TB_SETBUTTONINFOW, spec.command, reinterpret_cast<LPARAM>(&info));
    }
    SendMessageW(hwnd_, TB_AUTOSIZE, 0, 0);
}

// Redraws the glyphs in the colour the active palette uses for text.
void Toolbar::ApplyTheme(const Theme& theme) {
    // A themed toolbar paints its own background over the custom draw pass, so
    // visual styles have to step aside for the dark palette to show through.
    if (theme.IsDark()) {
        SetWindowTheme(hwnd_, L"", L"");
    } else {
        SetWindowTheme(hwnd_, nullptr, nullptr);
    }

    HIMAGELIST previous = imageList_;
    imageList_ = CreateToolbarImageList(theme.Colors().text);
    SendMessageW(hwnd_, TB_SETIMAGELIST, 0, reinterpret_cast<LPARAM>(imageList_));
    if (previous != nullptr) {
        ImageList_Destroy(previous);
    }
    InvalidateRect(hwnd_, nullptr, TRUE);
}

// Re-runs auto-sizing so the toolbar tracks the parent width.
void Toolbar::Resize() {
    SendMessageW(hwnd_, TB_AUTOSIZE, 0, 0);
}

// Returns the toolbar height in pixels for layout calculations.
int Toolbar::Height() const {
    RECT rect = {};
    GetWindowRect(hwnd_, &rect);
    return rect.bottom - rect.top;
}
