#include "ui/Toolbar.h"

#include <commctrl.h>
#include <uxtheme.h>

#include <algorithm>

#include "ui/Commands.h"
#include "ui/IconFactory.h"
#include "ui/Strings.h"
#include "ui/Theme.h"

namespace {
constexpr UINT_PTR kSpriteTimer = 1;
constexpr int kIconSize = 24;
constexpr int kPaddingX = 18;
constexpr int kPaddingY = 10;

struct ButtonSpec {
    int command;
    int icon;
    StringId text;
    const wchar_t* sprite;  // file name looked up in resources\sprites
};

constexpr ButtonSpec kButtons[] = {
    {ID_TASK_ADD, ICON_ADD_URL, STR_TB_ADD, L"add.bmp"},
    {ID_FILE_START, ICON_RESUME, STR_TB_RESUME, L"resume.bmp"},
    {ID_FILE_STOP, ICON_STOP, STR_TB_STOP, L"stop.bmp"},
    {ID_DOWNLOAD_STOP_ALL, ICON_STOP_ALL, STR_TB_STOP_ALL, L"stop-all.bmp"},
    {ID_FILE_REMOVE, ICON_REMOVE, STR_TB_REMOVE, L"remove.bmp"},
    {ID_DOWNLOAD_DELETE_ALL, ICON_REMOVE_ALL, STR_TB_REMOVE_ALL, L"remove-all.bmp"},
    {ID_VIEW_SETTINGS, ICON_OPTIONS, STR_TB_OPTIONS, L"options.bmp"},
    {ID_DOWNLOAD_SCHEDULE, ICON_SCHEDULE, STR_TB_SCHEDULE, L"schedule.bmp"},
    {ID_VIEW_ADDONS, ICON_ADDONS, STR_TB_ADDONS, L"addons.bmp"},
    {ID_DOWNLOAD_SEARCH, ICON_SEARCH, STR_TB_SEARCH, L"search.bmp"},
};

// The scale of the display the toolbar is on, 1.0 at 96 dpi.
double ScaleOf(HWND window) {
    HDC dc = GetDC(window);
    if (dc == nullptr) {
        return 1.0;
    }
    int dpi = GetDeviceCaps(dc, LOGPIXELSX);
    ReleaseDC(window, dc);
    return dpi <= 0 ? 1.0 : static_cast<double>(dpi) / 96.0;
}

// Drives the sprites of the buttons from the timer of the toolbar.
LRESULT CALLBACK SpriteSubclass(HWND window, UINT msg, WPARAM wParam, LPARAM lParam,
                                UINT_PTR id, DWORD_PTR data) {
    if (msg == WM_TIMER && wParam == kSpriteTimer) {
        reinterpret_cast<Toolbar*>(data)->StepSprite();
        return 0;
    }
    if (msg == WM_NCDESTROY) {
        KillTimer(window, kSpriteTimer);
        RemoveWindowSubclass(window, SpriteSubclass, id);
    }
    return DefSubclassProc(window, msg, wParam, lParam);
}
}  // namespace

// Releases the image lists owned by the toolbar.
Toolbar::~Toolbar() {
    DropLists();
    DropSpriteFrames();
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
    SendMessageW(hwnd_, TB_SETPADDING, 0, MAKELPARAM(kPaddingX, kPaddingY));

    for (const ButtonSpec& spec : kButtons) {
        auto button = std::make_unique<ButtonSprite>();
        button->command = spec.command;
        button->icon = spec.icon;
        if (button->sprite.Load(FindSprite(spec.sprite))) {
            sprites_.push_back(std::move(button));
        }
    }
    SetWindowSubclass(hwnd_, SpriteSubclass, 1, reinterpret_cast<DWORD_PTR>(this));

    RebuildImages(ActiveTheme());
    RebuildButtons();
    return true;
}

// Frees every image list the toolbar holds.
void Toolbar::DropLists() {
    if (imageList_ != nullptr) {
        ImageList_Destroy(imageList_);
        imageList_ = nullptr;
    }
    if (disabledList_ != nullptr) {
        ImageList_Destroy(disabledList_);
        disabledList_ = nullptr;
    }
    skins::Release(&strips_);
}

// Builds the pictures of the buttons for the active palette: the strips of
// the skin when one is chosen and readable, the icon font otherwise.
void Toolbar::RebuildImages(const Theme& theme) {
    const ThemeColors& colors = theme.Colors();
    DropLists();

    if (skin_ != nullptr &&
        skins::Load(*skin_, ScaleOf(hwnd_), colors.text, colors.muted, &strips_)) {
        SendMessageW(hwnd_, TB_SETBITMAPSIZE, 0, MAKELPARAM(strips_.width, strips_.height));
        SendMessageW(hwnd_, TB_SETIMAGELIST, 0, reinterpret_cast<LPARAM>(strips_.normal));
        SendMessageW(hwnd_, TB_SETHOTIMAGELIST, 0, reinterpret_cast<LPARAM>(strips_.hot));
        SendMessageW(hwnd_, TB_SETDISABLEDIMAGELIST, 0,
                     reinterpret_cast<LPARAM>(strips_.disabled));
        RenderSprites(strips_.width, strips_.height);
        return;
    }

    imageList_ = CreateToolbarImageList(colors.text);
    disabledList_ = CreateToolbarImageList(colors.muted);
    SendMessageW(hwnd_, TB_SETBITMAPSIZE, 0, MAKELPARAM(kIconSize, kIconSize));
    SendMessageW(hwnd_, TB_SETIMAGELIST, 0, reinterpret_cast<LPARAM>(imageList_));
    SendMessageW(hwnd_, TB_SETHOTIMAGELIST, 0, 0);
    SendMessageW(hwnd_, TB_SETDISABLEDIMAGELIST, 0, reinterpret_cast<LPARAM>(disabledList_));
    RenderSprites(kIconSize, kIconSize);
}

// Frees the rendered frames of every sprite.
void Toolbar::DropSpriteFrames() {
    for (const std::unique_ptr<ButtonSprite>& button : sprites_) {
        for (HBITMAP frame : button->frames) {
            DeleteObject(frame);
        }
        button->frames.clear();
    }
}

// Renders every frame of every sprite at the size of a button picture, then
// puts the current frame of each in place of its glyph.
void Toolbar::RenderSprites(int width, int height) {
    DropSpriteFrames();
    for (const std::unique_ptr<ButtonSprite>& button : sprites_) {
        for (int frame = 0; frame < button->sprite.FrameCount(); ++frame) {
            button->frames.push_back(button->sprite.Render(frame, width, height));
        }
        ShowSpriteFrame(*button);
    }
}

// Swaps the picture of a button, in every list, for its current frame.
void Toolbar::ShowSpriteFrame(const ButtonSprite& button) {
    if (button.frames.empty()) {
        return;
    }
    HBITMAP frame = button.frames[static_cast<size_t>(button.frame)];
    for (HIMAGELIST list : {imageList_, disabledList_, strips_.normal, strips_.hot,
                            strips_.disabled}) {
        if (list != nullptr && frame != nullptr) {
            ImageList_Replace(list, button.icon, frame, nullptr);
        }
    }
    RECT rect = {};
    if (SendMessageW(hwnd_, TB_GETRECT, button.command, reinterpret_cast<LPARAM>(&rect))) {
        InvalidateRect(hwnd_, &rect, TRUE);
    }
}

// Heads the sprite under the pointer for its last frame and every other one
// back to its first.
void Toolbar::OnHotItem(const NMTBHOTITEM* hot) {
    int hovered = (hot->dwFlags & HICF_LEAVING) == 0 ? hot->idNew : -1;
    bool moving = false;
    UINT interval = 0;
    for (const std::unique_ptr<ButtonSprite>& button : sprites_) {
        if (button->frames.empty()) {
            continue;
        }
        button->target =
            button->command == hovered ? static_cast<int>(button->frames.size()) - 1 : 0;
        if (button->target != button->frame) {
            moving = true;
            UINT duration = static_cast<UINT>(button->sprite.DurationMs());
            interval = interval == 0 ? duration : std::min(interval, duration);
        }
    }
    if (moving) {
        SetTimer(hwnd_, kSpriteTimer, interval, nullptr);
    }
}

// Moves every sprite one frame toward where it is heading.
void Toolbar::StepSprite() {
    bool moving = false;
    for (const std::unique_ptr<ButtonSprite>& button : sprites_) {
        if (button->frames.empty() || button->frame == button->target) {
            continue;
        }
        button->frame += button->target > button->frame ? 1 : -1;
        ShowSpriteFrame(*button);
        moving = moving || button->frame != button->target;
    }
    if (!moving) {
        KillTimer(hwnd_, kSpriteTimer);
    }
}

// Recreates the buttons, which is what makes the toolbar take a new picture
// size into account.
void Toolbar::RebuildButtons() {
    struct Enabled {
        int command;
        bool enabled;
    };
    Enabled kept[ARRAYSIZE(kButtons)] = {};
    int count = static_cast<int>(SendMessageW(hwnd_, TB_BUTTONCOUNT, 0, 0));
    for (size_t i = 0; i < ARRAYSIZE(kButtons); ++i) {
        kept[i].command = kButtons[i].command;
        kept[i].enabled = count == 0 || (SendMessageW(hwnd_, TB_ISBUTTONENABLED,
                                                      kButtons[i].command, 0) != 0);
    }
    while (SendMessageW(hwnd_, TB_BUTTONCOUNT, 0, 0) > 0) {
        SendMessageW(hwnd_, TB_DELETEBUTTON, 0, 0);
    }

    TBBUTTON buttons[ARRAYSIZE(kButtons)] = {};
    for (size_t i = 0; i < ARRAYSIZE(kButtons); ++i) {
        buttons[i].iBitmap = kButtons[i].icon;
        buttons[i].idCommand = kButtons[i].command;
        buttons[i].fsState = kept[i].enabled ? TBSTATE_ENABLED : 0;
        buttons[i].fsStyle = BTNS_AUTOSIZE | BTNS_SHOWTEXT;
        buttons[i].iString = reinterpret_cast<INT_PTR>(Str(kButtons[i].text));
    }
    SendMessageW(hwnd_, TB_ADDBUTTONS, ARRAYSIZE(buttons), reinterpret_cast<LPARAM>(buttons));
    SendMessageW(hwnd_, TB_AUTOSIZE, 0, 0);
}

// Refreshes the button captions after a language change.
void Toolbar::Retranslate() {
    for (const ButtonSpec& spec : kButtons) {
        TBBUTTONINFOW info = {};
        info.cbSize = sizeof(info);
        info.dwMask = TBIF_TEXT;
        info.pszText = const_cast<wchar_t*>(Str(spec.text));
        SendMessageW(hwnd_, TB_SETBUTTONINFOW, spec.command, reinterpret_cast<LPARAM>(&info));
    }
    SendMessageW(hwnd_, TB_AUTOSIZE, 0, 0);
}

// Redraws the pictures in the colours of the active palette.
void Toolbar::ApplyTheme(const Theme& theme) {
    // A themed toolbar paints its own background over the custom draw pass, so
    // visual styles have to step aside for the dark palette to show through.
    if (theme.IsDark()) {
        SetWindowTheme(hwnd_, L"", L"");
    } else {
        SetWindowTheme(hwnd_, nullptr, nullptr);
    }
    RebuildImages(theme);
    RebuildButtons();
    InvalidateRect(hwnd_, nullptr, TRUE);
}

// Dresses the buttons with a skin, or with the icon font when null.
void Toolbar::SetSkin(const ToolbarSkin* skin, const Theme& theme) {
    skin_ = skin != nullptr ? std::make_unique<ToolbarSkin>(*skin) : nullptr;
    RebuildImages(theme);
    RebuildButtons();
    InvalidateRect(hwnd_, nullptr, TRUE);
}

// Greys a button out, or lights it up again.
void Toolbar::Enable(int command, bool enabled) {
    SendMessageW(hwnd_, TB_ENABLEBUTTON, command, MAKELPARAM(enabled ? TRUE : FALSE, 0));
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
