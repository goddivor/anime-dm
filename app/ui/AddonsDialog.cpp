#include "ui/AddonsDialog.h"

#include <commctrl.h>
#include <windowsx.h>

#include <algorithm>
#include <iterator>
#include <memory>
#include <string>
#include <thread>
#include <vector>

#include "core/AddonStore.h"
#include "core/Http.h"
#include "core/Image.h"
#include "core/Text.h"
#include "ui/AddonConfigDialog.h"
#include "ui/IconFactory.h"
#include "ui/Paint.h"
#include "ui/Resource.h"
#include "ui/Strings.h"
#include "ui/Theme.h"

namespace {

constexpr UINT kFetched = WM_APP + 1;
constexpr UINT kInstalled = WM_APP + 2;
constexpr UINT_PTR kSpinTimer = 1;
constexpr int kIconSize = 36;    // the picture of a source, on a row twice as tall as text
constexpr int kRowPadding = 8;   // above and below the picture
constexpr float kRadius = 5.0f;

// What a fetch brings back: the index, and the icon of each entry.
struct Catalogue {
    std::vector<StoreEntry> entries;
    std::vector<std::vector<uint8_t>> icons;
};

// One icon button of the top row: which glyph, and its picture per state.
struct Action {
    int control;
    ActionIcon icon;
};

constexpr Action kActions[] = {
    {IDC_ADDONS_REFRESH, ACTION_REFRESH},
    {IDC_ADDONS_INSTALL, ACTION_INSTALL},
    {IDC_ADDONS_REMOVE, ACTION_REMOVE},
    {IDC_ADDONS_CONFIGURE, ACTION_CONFIGURE},
};

// What the window keeps for the whole of its life.
struct Screen {
    const AddonStore* store = nullptr;
    Http* http = nullptr;
    std::vector<StoreEntry> entries;
    HIMAGELIST icons = nullptr;
    HFONT bold = nullptr;   // the name of a source
    HFONT small = nullptr;  // its language and version
    bool busy = false;
    float spin = 0.0f;      // the angle of the refresh arrows while a fetch runs
};

// Turns the downloaded icons into the image list the rows draw from.
void AdoptIcons(HWND dialog, Screen& screen, const std::vector<std::vector<uint8_t>>& icons) {
    HIMAGELIST previous = screen.icons;
    screen.icons = ImageList_Create(kIconSize, kIconSize + kRowPadding, ILC_COLOR32,
                                    static_cast<int>(icons.size()), 0);
    if (screen.icons == nullptr) {
        return;
    }

    for (const std::vector<uint8_t>& bytes : icons) {
        // The pictures sit on a cell taller than themselves, which sets the
        // height of the rows; a source without one keeps the indices in step.
        HBITMAP bitmap = image::DecodeSquare(bytes, kIconSize);
        HBITMAP cell = image::Transparent(kIconSize + kRowPadding);
        if (bitmap != nullptr && cell != nullptr) {
            HDC target = CreateCompatibleDC(nullptr);
            HDC source = CreateCompatibleDC(nullptr);
            HBITMAP oldTarget = static_cast<HBITMAP>(SelectObject(target, cell));
            HBITMAP oldSource = static_cast<HBITMAP>(SelectObject(source, bitmap));
            BLENDFUNCTION blend = {AC_SRC_OVER, 0, 255, AC_SRC_ALPHA};
            AlphaBlend(target, 0, kRowPadding / 2, kIconSize, kIconSize, source, 0, 0, kIconSize,
                       kIconSize, blend);
            SelectObject(source, oldSource);
            SelectObject(target, oldTarget);
            DeleteDC(source);
            DeleteDC(target);
        }
        if (cell != nullptr) {
            ImageList_Add(screen.icons, cell, nullptr);
            DeleteObject(cell);
        }
        if (bitmap != nullptr) {
            DeleteObject(bitmap);
        }
    }

    ListView_SetImageList(GetDlgItem(dialog, IDC_ADDONS_LIST), screen.icons, LVSIL_SMALL);
    if (previous != nullptr) {
        ImageList_Destroy(previous);
    }
}

// Compares two dot-separated version numbers.
bool IsNewer(const std::string& candidate, const std::string& reference) {
    size_t left = 0;
    size_t right = 0;
    while (left < candidate.size() || right < reference.size()) {
        int first = 0;
        int second = 0;
        while (left < candidate.size() && candidate[left] != '.') {
            if (candidate[left] >= '0' && candidate[left] <= '9') {
                first = first * 10 + (candidate[left] - '0');
            }
            ++left;
        }
        while (right < reference.size() && reference[right] != '.') {
            if (reference[right] >= '0' && reference[right] <= '9') {
                second = second * 10 + (reference[right] - '0');
            }
            ++right;
        }
        if (first != second) {
            return first > second;
        }
        ++left;
        ++right;
    }
    return false;
}

// Whether the store offers a version newer than the installed one.
bool Outdated(const StoreEntry& entry) {
    return entry.installed && IsNewer(entry.version, entry.installedVersion);
}

void SetStatus(HWND dialog, StringId message) {
    SetDlgItemTextW(dialog, IDC_ADDONS_STATUS, Str(message));
}

// The entry under the selection, or null.
const StoreEntry* Chosen(HWND dialog, const Screen& screen) {
    int selected = ListView_GetNextItem(GetDlgItem(dialog, IDC_ADDONS_LIST), -1, LVNI_SELECTED);
    bool has = selected >= 0 && static_cast<size_t>(selected) < screen.entries.size();
    return has ? &screen.entries[static_cast<size_t>(selected)] : nullptr;
}

// Greys the actions out while a job runs, and matches them to the selection.
void SyncButtons(HWND dialog, const Screen& screen) {
    const StoreEntry* entry = Chosen(dialog, screen);
    bool idle = !screen.busy;
    EnableWindow(GetDlgItem(dialog, IDC_ADDONS_REFRESH), idle);
    EnableWindow(GetDlgItem(dialog, IDC_ADDONS_INSTALL),
                 idle && entry != nullptr && (!entry->installed || Outdated(*entry)));
    EnableWindow(GetDlgItem(dialog, IDC_ADDONS_REMOVE),
                 idle && entry != nullptr && entry->installed);
    EnableWindow(GetDlgItem(dialog, IDC_ADDONS_CONFIGURE),
                 idle && entry != nullptr && entry->installed);
    for (const Action& action : kActions) {
        InvalidateRect(GetDlgItem(dialog, action.control), nullptr, TRUE);
    }
}

// Fills the list from what the store answered; the rows are painted by the
// window, the text of the items only serves the keyboard.
void FillList(HWND dialog, const Screen& screen) {
    HWND list = GetDlgItem(dialog, IDC_ADDONS_LIST);
    ListView_DeleteAllItems(list);

    int row = 0;
    for (const StoreEntry& entry : screen.entries) {
        std::wstring name = Widen(entry.name);
        LVITEMW item = {};
        item.mask = LVIF_TEXT | LVIF_IMAGE;
        item.iItem = row;
        item.iImage = row;
        item.pszText = name.data();
        ListView_InsertItem(list, &item);
        ++row;
    }

    if (row > 0) {
        ListView_SetItemState(list, 0, LVIS_SELECTED | LVIS_FOCUSED,
                              LVIS_SELECTED | LVIS_FOCUSED);
    }
}

// What a row says of its entry on the right: the state, in its colour.
std::pair<StringId, COLORREF> StateOf(const StoreEntry& entry) {
    const ThemeColors& colours = ActiveTheme().Colors();
    if (Outdated(entry)) {
        return {STR_ADDONS_STATE_OUTDATED, colours.accent};
    }
    if (entry.installed) {
        return {STR_ADDONS_STATE_INSTALLED, colours.ok};
    }
    return {STR_ADDONS_STATE_AVAILABLE, colours.muted};
}

// Paints one row: the picture of the source, its name above its language and
// version, and its state against the right edge.
void DrawRow(HWND dialog, const Screen& screen, NMLVCUSTOMDRAW* draw) {
    HWND list = draw->nmcd.hdr.hwndFrom;
    int row = static_cast<int>(draw->nmcd.dwItemSpec);
    if (row < 0 || static_cast<size_t>(row) >= screen.entries.size()) {
        return;
    }
    const StoreEntry& entry = screen.entries[static_cast<size_t>(row)];
    const ThemeColors& colours = ActiveTheme().Colors();
    HDC dc = draw->nmcd.hdc;

    RECT bounds = {};
    ListView_GetItemRect(list, row, &bounds, LVIR_BOUNDS);
    RECT client = {};
    GetClientRect(list, &client);
    bounds.left = client.left;
    bounds.right = client.right;

    bool selected = (ListView_GetItemState(list, row, LVIS_SELECTED) & LVIS_SELECTED) != 0;
    HBRUSH fill = CreateSolidBrush(selected ? colours.accent : colours.window);
    FillRect(dc, &bounds, fill);
    DeleteObject(fill);

    int x = bounds.left + kRowPadding;
    int y = (bounds.top + bounds.bottom - kIconSize - kRowPadding) / 2;
    if (screen.icons != nullptr) {
        ImageList_Draw(screen.icons, row, dc, x, y, ILD_TRANSPARENT);
    }
    x += kIconSize + kRowPadding + 2;

    SetBkMode(dc, TRANSPARENT);
    auto [stateId, stateColour] = StateOf(entry);
    std::wstring state = Str(stateId);
    HFONT previous = static_cast<HFONT>(SelectObject(dc, screen.small));
    RECT stateBox = {x, bounds.top, bounds.right - kRowPadding - 2, bounds.bottom};
    SetTextColor(dc, selected ? colours.accentText : stateColour);
    DrawTextW(dc, state.c_str(), -1, &stateBox,
              DT_RIGHT | DT_VCENTER | DT_SINGLELINE | DT_NOPREFIX);
    RECT stateExtent = stateBox;
    DrawTextW(dc, state.c_str(), -1, &stateExtent, DT_SINGLELINE | DT_CALCRECT | DT_NOPREFIX);
    int textRight = stateBox.right - (stateExtent.right - stateExtent.left) - kRowPadding * 2;

    int middle = (bounds.top + bounds.bottom) / 2;
    SelectObject(dc, screen.bold);
    SetTextColor(dc, selected ? colours.accentText : colours.text);
    RECT nameBox = {x, bounds.top + kRowPadding / 2, textRight, middle};
    DrawTextW(dc, Widen(entry.name).c_str(), -1, &nameBox,
              DT_LEFT | DT_BOTTOM | DT_SINGLELINE | DT_END_ELLIPSIS | DT_NOPREFIX);

    SelectObject(dc, screen.small);
    SetTextColor(dc, selected ? colours.accentText : colours.muted);
    std::wstring detail = Widen(entry.lang);
    std::transform(detail.begin(), detail.end(), detail.begin(), towupper);
    detail += L"  \u00B7  " + Widen(entry.version);
    if (entry.installed && Outdated(entry)) {
        detail += L"  (" + Widen(entry.installedVersion) + L")";
    }
    RECT detailBox = {x, middle, textRight, bounds.bottom - kRowPadding / 2};
    DrawTextW(dc, detail.c_str(), -1, &detailBox,
              DT_LEFT | DT_TOP | DT_SINGLELINE | DT_END_ELLIPSIS | DT_NOPREFIX);
    SelectObject(dc, previous);
}

// Paints one icon button: bare on the surface, a quiet rounded plate while
// pressed, its glyph in the colour of the text, or muted when the action is
// out of reach.
void DrawAction(const DRAWITEMSTRUCT& draw, const Screen& screen) {
    const ThemeColors& colours = ActiveTheme().Colors();
    bool disabled = (draw.itemState & ODS_DISABLED) != 0;
    bool pressed = (draw.itemState & ODS_SELECTED) != 0;

    HBRUSH back = CreateSolidBrush(colours.surface);
    FillRect(draw.hDC, &draw.rcItem, back);
    DeleteObject(back);
    if (pressed) {
        paint::RoundedRect(draw.hDC, draw.rcItem, kRadius, colours.hover, colours.line);
    }

    ActionIcon icon = ACTION_REFRESH;
    for (const Action& action : kActions) {
        if (action.control == static_cast<int>(draw.CtlID)) {
            icon = action.icon;
        }
    }
    float angle = icon == ACTION_REFRESH && screen.busy ? screen.spin : 0.0f;
    int side = std::min(draw.rcItem.right - draw.rcItem.left, draw.rcItem.bottom - draw.rcItem.top);
    HBITMAP glyph = CreateActionGlyph(icon, side, disabled ? colours.muted : colours.text, angle);
    if (glyph == nullptr) {
        return;
    }
    HDC memory = CreateCompatibleDC(draw.hDC);
    HBITMAP old = static_cast<HBITMAP>(SelectObject(memory, glyph));
    BLENDFUNCTION blend = {AC_SRC_OVER, 0, 255, AC_SRC_ALPHA};
    int left = (draw.rcItem.left + draw.rcItem.right - side) / 2;
    int top = (draw.rcItem.top + draw.rcItem.bottom - side) / 2;
    AlphaBlend(draw.hDC, left, top, side, side, memory, 0, 0, side, side, blend);
    SelectObject(memory, old);
    DeleteDC(memory);
    DeleteObject(glyph);
}

// Asks the store for its index, off the interface thread.
void StartFetch(HWND dialog, Screen& screen) {
    screen.busy = true;
    screen.spin = 0.0f;
    SetTimer(dialog, kSpinTimer, 40, nullptr);
    SetStatus(dialog, STR_ADDONS_LOADING);
    SyncButtons(dialog, screen);

    std::thread([dialog, &screen] {
        std::string error;
        auto* catalogue = new Catalogue();
        catalogue->entries = screen.store->Fetch(&error);
        catalogue->icons.reserve(catalogue->entries.size());
        for (const StoreEntry& entry : catalogue->entries) {
            catalogue->icons.push_back(screen.store->IconBytes(entry));
        }
        if (!PostMessageW(dialog, kFetched, 0, reinterpret_cast<LPARAM>(catalogue))) {
            delete catalogue;
        }
    }).detach();
}

// Downloads and installs the selected source, off the interface thread.
void StartInstall(HWND dialog, Screen& screen) {
    int selected = ListView_GetNextItem(GetDlgItem(dialog, IDC_ADDONS_LIST), -1, LVNI_SELECTED);
    if (selected < 0 || static_cast<size_t>(selected) >= screen.entries.size()) {
        return;
    }

    screen.busy = true;
    SetStatus(dialog, STR_ADDONS_LOADING);
    SyncButtons(dialog, screen);

    StoreEntry entry = screen.entries[static_cast<size_t>(selected)];
    std::thread([dialog, &screen, entry] {
        std::string error;
        bool ok = screen.store->Install(entry, &error).has_value();
        if (!PostMessageW(dialog, kInstalled, ok ? 1 : 0, 0)) {
            return;
        }
    }).detach();
}

// One column as wide as the list, no header: the rows are painted whole. The
// image list sets the height of the rows.
void InitList(HWND dialog, Screen& screen) {
    HWND list = GetDlgItem(dialog, IDC_ADDONS_LIST);
    ListView_SetExtendedListViewStyle(list, LVS_EX_FULLROWSELECT | LVS_EX_DOUBLEBUFFER);

    RECT bounds = {};
    GetClientRect(list, &bounds);
    LVCOLUMNW col = {};
    col.mask = LVCF_WIDTH;
    col.cx = bounds.right - bounds.left;
    ListView_InsertColumn(list, 0, &col);

    HFONT base = reinterpret_cast<HFONT>(SendMessageW(dialog, WM_GETFONT, 0, 0));
    LOGFONTW description = {};
    GetObjectW(base, sizeof(description), &description);
    description.lfWeight = FW_SEMIBOLD;
    screen.bold = CreateFontIndirectW(&description);
    description.lfWeight = FW_NORMAL;
    description.lfHeight = description.lfHeight * 9 / 10;
    screen.small = CreateFontIndirectW(&description);

    // An empty image list of the right height until the icons arrive.
    screen.icons = ImageList_Create(kIconSize, kIconSize + kRowPadding, ILC_COLOR32, 0, 0);
    ListView_SetImageList(list, screen.icons, LVSIL_SMALL);
}

// Names the icon buttons in a tooltip, since they carry no caption.
void InitTips(HWND dialog) {
    HINSTANCE instance = reinterpret_cast<HINSTANCE>(GetWindowLongPtrW(dialog, GWLP_HINSTANCE));
    HWND tips = CreateWindowExW(WS_EX_TOPMOST, TOOLTIPS_CLASSW, nullptr,
                                WS_POPUP | TTS_ALWAYSTIP | TTS_NOPREFIX, 0, 0, 0, 0, dialog,
                                nullptr, instance, nullptr);
    if (tips == nullptr) {
        return;
    }
    const StringId captions[] = {STR_ADDONS_REFRESH, STR_ADDONS_INSTALL, STR_ADDONS_REMOVE,
                                 STR_ADDONS_CONFIGURE};
    for (size_t i = 0; i < std::size(kActions); ++i) {
        TOOLINFOW tool = {};
        tool.cbSize = sizeof(tool);
        tool.uFlags = TTF_IDISHWND | TTF_SUBCLASS;
        tool.hwnd = dialog;
        tool.uId = reinterpret_cast<UINT_PTR>(GetDlgItem(dialog, kActions[i].control));
        tool.lpszText = const_cast<wchar_t*>(Str(captions[i]));
        SendMessageW(tips, TTM_ADDTOOLW, 0, reinterpret_cast<LPARAM>(&tool));
    }
}

void Retranslate(HWND dialog) {
    SetDialogTitle(dialog, STR_VIEW_ADDONS);
}

INT_PTR CALLBACK AddonsDialogProc(HWND dialog, UINT msg, WPARAM wParam, LPARAM lParam) {
    INT_PTR colour = 0;
    if (ThemeDialogMessage(msg, wParam, &colour)) {
        return colour;
    }

    auto* screen = reinterpret_cast<Screen*>(GetWindowLongPtrW(dialog, GWLP_USERDATA));

    switch (msg) {
    case WM_INITDIALOG:
        SetWindowLongPtrW(dialog, GWLP_USERDATA, lParam);
        screen = reinterpret_cast<Screen*>(lParam);
        Retranslate(dialog);
        InitList(dialog, *screen);
        InitTips(dialog);
        ActiveTheme().ApplyToDialog(dialog);
        StartFetch(dialog, *screen);
        return TRUE;

    case WM_TIMER:
        if (wParam == kSpinTimer && screen != nullptr) {
            screen->spin += 12.0f;
            if (screen->spin >= 360.0f) {
                screen->spin -= 360.0f;
            }
            InvalidateRect(GetDlgItem(dialog, IDC_ADDONS_REFRESH), nullptr, FALSE);
        }
        return TRUE;

    case WM_DRAWITEM: {
        auto* draw = reinterpret_cast<DRAWITEMSTRUCT*>(lParam);
        if (screen == nullptr || draw->CtlType != ODT_BUTTON) {
            return FALSE;
        }
        DrawAction(*draw, *screen);
        return TRUE;
    }

    case kFetched: {
        std::unique_ptr<Catalogue> catalogue(reinterpret_cast<Catalogue*>(lParam));
        screen->busy = false;
        KillTimer(dialog, kSpinTimer);
        screen->spin = 0.0f;
        screen->entries = std::move(catalogue->entries);
        AdoptIcons(dialog, *screen, catalogue->icons);
        FillList(dialog, *screen);
        SetStatus(dialog, screen->entries.empty() ? STR_ADDONS_FETCH_FAILED : STR_ADDONS_EMPTY);
        if (!screen->entries.empty()) {
            SetDlgItemTextW(dialog, IDC_ADDONS_STATUS, L"");
        }
        SyncButtons(dialog, *screen);
        return TRUE;
    }

    case kInstalled:
        screen->busy = false;
        if (wParam == 0) {
            SetStatus(dialog, STR_ADDONS_INSTALL_FAILED);
            SyncButtons(dialog, *screen);
        } else {
            StartFetch(dialog, *screen);
        }
        return TRUE;

    case WM_NOTIFY: {
        auto* notify = reinterpret_cast<NMHDR*>(lParam);
        if (screen == nullptr || notify->idFrom != IDC_ADDONS_LIST) {
            return FALSE;
        }
        if (notify->code == LVN_ITEMCHANGED) {
            SyncButtons(dialog, *screen);
        } else if (notify->code == NM_CUSTOMDRAW) {
            auto* draw = reinterpret_cast<NMLVCUSTOMDRAW*>(lParam);
            LRESULT answer = CDRF_DODEFAULT;
            if (draw->nmcd.dwDrawStage == CDDS_PREPAINT) {
                answer = CDRF_NOTIFYITEMDRAW;
            } else if (draw->nmcd.dwDrawStage == CDDS_ITEMPREPAINT) {
                DrawRow(dialog, *screen, draw);
                answer = CDRF_SKIPDEFAULT;
            }
            SetWindowLongPtrW(dialog, DWLP_MSGRESULT, answer);
            return TRUE;
        }
        return FALSE;
    }

    case WM_COMMAND:
        if (screen == nullptr || screen->busy) {
            return TRUE;
        }
        switch (LOWORD(wParam)) {
        case IDC_ADDONS_REFRESH:
            StartFetch(dialog, *screen);
            return TRUE;
        case IDC_ADDONS_INSTALL:
            StartInstall(dialog, *screen);
            return TRUE;
        case IDC_ADDONS_REMOVE: {
            int selected =
                ListView_GetNextItem(GetDlgItem(dialog, IDC_ADDONS_LIST), -1, LVNI_SELECTED);
            if (selected >= 0 && static_cast<size_t>(selected) < screen->entries.size()) {
                if (screen->store->Remove(screen->entries[static_cast<size_t>(selected)].id)) {
                    StartFetch(dialog, *screen);
                } else {
                    SetStatus(dialog, STR_ADDONS_REMOVE_FAILED);
                }
            }
            return TRUE;
        }
        case IDC_ADDONS_CONFIGURE: {
            int selected =
                ListView_GetNextItem(GetDlgItem(dialog, IDC_ADDONS_LIST), -1, LVNI_SELECTED);
            if (selected >= 0 && static_cast<size_t>(selected) < screen->entries.size()) {
                HINSTANCE instance =
                    reinterpret_cast<HINSTANCE>(GetWindowLongPtrW(dialog, GWLP_HINSTANCE));
                ShowAddonConfigDialog(dialog, instance, *screen->store, *screen->http,
                                      screen->entries[static_cast<size_t>(selected)].id);
            }
            return TRUE;
        }
        case IDCANCEL:
            EndDialog(dialog, IDCANCEL);
            return TRUE;
        default:
            return FALSE;
        }

    case WM_DESTROY:
        if (screen != nullptr) {
            KillTimer(dialog, kSpinTimer);
            if (screen->icons != nullptr) {
                ImageList_Destroy(screen->icons);
                screen->icons = nullptr;
            }
            for (HFONT* font : {&screen->bold, &screen->small}) {
                if (*font != nullptr) {
                    DeleteObject(*font);
                    *font = nullptr;
                }
            }
        }
        return FALSE;

    case WM_CLOSE:
        if (screen == nullptr || !screen->busy) {
            EndDialog(dialog, IDCANCEL);
        }
        return TRUE;

    default:
        return FALSE;
    }
}

}  // namespace

// Runs the addon store modally against its owner window.
INT_PTR ShowAddonsDialog(HWND owner, HINSTANCE instance, const AddonStore& store, Http& http) {
    Screen screen;
    screen.store = &store;
    screen.http = &http;
    return DialogBoxParamW(instance, MAKEINTRESOURCEW(IDD_ADDONS), owner, AddonsDialogProc,
                           reinterpret_cast<LPARAM>(&screen));
}
