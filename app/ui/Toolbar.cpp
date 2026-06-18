#include "ui/Toolbar.h"

#include <commctrl.h>

#include "ui/Commands.h"
#include "ui/IconFactory.h"

namespace {
struct ButtonSpec {
    int command;
    int icon;
    const wchar_t* text;
};

constexpr ButtonSpec kButtons[] = {
    {ID_TASK_ADD, ICON_ADD, L"Ajouter"},
    {ID_DOWNLOAD_RESUME, ICON_RESUME, L"Reprendre"},
    {ID_DOWNLOAD_STOP, ICON_STOP, L"Arrêter"},
    {ID_FILE_REMOVE, ICON_REMOVE, L"Supprimer"},
    {ID_VIEW_SETTINGS, ICON_SETTINGS, L"Paramètres"},
};
}  // namespace

// Releases the GDI image list owned by the toolbar.
Toolbar::~Toolbar() {
    if (imageList_ != nullptr) {
        ImageList_Destroy(imageList_);
        imageList_ = nullptr;
    }
}

// Creates a flat, text-labelled toolbar pinned to the top of the parent.
bool Toolbar::Create(HWND parent, HINSTANCE instance) {
    hwnd_ = CreateWindowExW(
        0, TOOLBARCLASSNAMEW, nullptr,
        WS_CHILD | WS_VISIBLE | TBSTYLE_FLAT | TBSTYLE_LIST | CCS_TOP,
        0, 0, 0, 0, parent, nullptr, instance, nullptr);
    if (hwnd_ == nullptr) {
        return false;
    }

    SendMessageW(hwnd_, TB_BUTTONSTRUCTSIZE, sizeof(TBBUTTON), 0);
    SendMessageW(hwnd_, TB_SETEXTENDEDSTYLE, 0, TBSTYLE_EX_MIXEDBUTTONS);

    imageList_ = CreateToolbarImageList();
    SendMessageW(hwnd_, TB_SETIMAGELIST, 0, reinterpret_cast<LPARAM>(imageList_));

    TBBUTTON buttons[ARRAYSIZE(kButtons)] = {};
    for (size_t i = 0; i < ARRAYSIZE(kButtons); ++i) {
        buttons[i].iBitmap = kButtons[i].icon;
        buttons[i].idCommand = kButtons[i].command;
        buttons[i].fsState = TBSTATE_ENABLED;
        buttons[i].fsStyle = BTNS_AUTOSIZE | BTNS_SHOWTEXT;
        buttons[i].iString = reinterpret_cast<INT_PTR>(kButtons[i].text);
    }

    SendMessageW(hwnd_, TB_ADDBUTTONS, ARRAYSIZE(buttons), reinterpret_cast<LPARAM>(buttons));
    SendMessageW(hwnd_, TB_AUTOSIZE, 0, 0);
    return true;
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
