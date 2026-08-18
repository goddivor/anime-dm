#include "ui/AddonsDialog.h"

#include <commctrl.h>

#include <memory>
#include <string>
#include <thread>
#include <vector>

#include "core/AddonStore.h"
#include "core/Http.h"
#include "core/Image.h"
#include "core/Text.h"
#include "ui/AddonConfigDialog.h"
#include "ui/Resource.h"
#include "ui/Strings.h"
#include "ui/Theme.h"

namespace {

constexpr UINT kFetched = WM_APP + 1;
constexpr UINT kInstalled = WM_APP + 2;
constexpr int kIconSize = 20;

struct Column {
    StringId title;
    int width;
};

constexpr Column kColumns[] = {
    {STR_EXT_NAME, 200},
    {STR_EXT_LANG, 70},
    {STR_EXT_VERSION, 80},
    {STR_EXT_STATUS, 160},
};

// What a fetch brings back: the index, and the icon of each entry.
struct Catalogue {
    std::vector<StoreEntry> entries;
    std::vector<std::vector<uint8_t>> icons;
};

// What the window keeps for the whole of its life.
struct Screen {
    Http http;
    AddonStore store{http};
    std::vector<StoreEntry> entries;
    HIMAGELIST icons = nullptr;
    bool busy = false;
};

// Turns the downloaded icons into the image list the rows draw from.
void AdoptIcons(HWND dialog, Screen& screen, const std::vector<std::vector<uint8_t>>& icons) {
    HIMAGELIST previous = screen.icons;
    screen.icons = ImageList_Create(kIconSize, kIconSize, ILC_COLOR32, 
                                    static_cast<int>(icons.size()), 0);
    if (screen.icons == nullptr) {
        return;
    }

    for (const std::vector<uint8_t>& bytes : icons) {
        HBITMAP bitmap = image::DecodeSquare(bytes, kIconSize);
        if (bitmap != nullptr) {
            ImageList_Add(screen.icons, bitmap, nullptr);
            DeleteObject(bitmap);
        } else {
            // Keeps the indices in step with the rows when a source has none.
            HBITMAP blank = image::Transparent(kIconSize);
            ImageList_Add(screen.icons, blank, nullptr);
            DeleteObject(blank);
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

// Greys the actions out while a job runs, and matches them to the selection.
void SyncButtons(HWND dialog, const Screen& screen) {
    int selected = ListView_GetNextItem(GetDlgItem(dialog, IDC_ADDONS_LIST), -1, LVNI_SELECTED);
    bool has = selected >= 0 && static_cast<size_t>(selected) < screen.entries.size();
    const StoreEntry* entry = has ? &screen.entries[static_cast<size_t>(selected)] : nullptr;

    bool idle = !screen.busy;
    EnableWindow(GetDlgItem(dialog, IDC_ADDONS_REFRESH), idle);
    EnableWindow(GetDlgItem(dialog, IDCANCEL), idle);
    EnableWindow(GetDlgItem(dialog, IDC_ADDONS_INSTALL),
                 idle && entry != nullptr && (!entry->installed || Outdated(*entry)));
    EnableWindow(GetDlgItem(dialog, IDC_ADDONS_REMOVE),
                 idle && entry != nullptr && entry->installed);
    EnableWindow(GetDlgItem(dialog, IDC_ADDONS_CONFIGURE),
                 idle && entry != nullptr && entry->installed);

    SetDlgItemTextW(dialog, IDC_ADDONS_INSTALL,
                    Str(entry != nullptr && Outdated(*entry) ? STR_ADDONS_UPDATE
                                                             : STR_ADDONS_INSTALL));
}

// Fills the list from what the store answered.
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

        std::wstring lang = Widen(entry.lang);
        std::wstring version = Widen(entry.version);
        ListView_SetItemText(list, row, 1, lang.data());
        ListView_SetItemText(list, row, 2, version.data());

        StringId state = STR_ADDONS_STATE_AVAILABLE;
        if (Outdated(entry)) {
            state = STR_ADDONS_STATE_OUTDATED;
        } else if (entry.installed) {
            state = STR_ADDONS_STATE_INSTALLED;
        }
        ListView_SetItemText(list, row, 3, const_cast<wchar_t*>(Str(state)));
        ++row;
    }

    if (row > 0) {
        ListView_SetItemState(list, 0, LVIS_SELECTED | LVIS_FOCUSED,
                              LVIS_SELECTED | LVIS_FOCUSED);
    }
}

// Asks the store for its index, off the interface thread.
void StartFetch(HWND dialog, Screen& screen) {
    screen.busy = true;
    SetStatus(dialog, STR_ADDONS_LOADING);
    SyncButtons(dialog, screen);

    std::thread([dialog, &screen] {
        std::string error;
        auto* catalogue = new Catalogue();
        catalogue->entries = screen.store.Fetch(&error);
        catalogue->icons.reserve(catalogue->entries.size());
        for (const StoreEntry& entry : catalogue->entries) {
            catalogue->icons.push_back(screen.store.IconBytes(entry));
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
        bool ok = screen.store.Install(entry, &error).has_value();
        if (!PostMessageW(dialog, kInstalled, ok ? 1 : 0, 0)) {
            return;
        }
    }).detach();
}

void InitList(HWND dialog) {
    HWND list = GetDlgItem(dialog, IDC_ADDONS_LIST);
    ListView_SetExtendedListViewStyle(list, LVS_EX_FULLROWSELECT | LVS_EX_DOUBLEBUFFER);

    LVCOLUMNW col = {};
    col.mask = LVCF_TEXT | LVCF_WIDTH | LVCF_SUBITEM;
    int index = 0;
    for (const Column& column : kColumns) {
        col.iSubItem = index;
        col.cx = column.width;
        col.pszText = const_cast<wchar_t*>(Str(column.title));
        ListView_InsertColumn(list, index, &col);
        ++index;
    }
}

void Retranslate(HWND dialog) {
    SetDialogTitle(dialog, STR_VIEW_ADDONS);
    SetDialogText(dialog, IDC_ADDONS_REFRESH, STR_ADDONS_REFRESH);
    SetDialogText(dialog, IDC_ADDONS_INSTALL, STR_ADDONS_INSTALL);
    SetDialogText(dialog, IDC_ADDONS_REMOVE, STR_ADDONS_REMOVE);
    SetDialogText(dialog, IDC_ADDONS_CONFIGURE, STR_ADDONS_CONFIGURE);
    SetDialogText(dialog, IDCANCEL, STR_DLG_CLOSE);
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
        InitList(dialog);
        ActiveTheme().ApplyToDialog(dialog);
        StartFetch(dialog, *screen);
        return TRUE;

    case kFetched: {
        std::unique_ptr<Catalogue> catalogue(reinterpret_cast<Catalogue*>(lParam));
        screen->busy = false;
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
        if (notify->idFrom == IDC_ADDONS_LIST && notify->code == LVN_ITEMCHANGED) {
            SyncButtons(dialog, *screen);
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
                if (screen->store.Remove(screen->entries[static_cast<size_t>(selected)].id)) {
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
                ShowAddonConfigDialog(dialog, instance, screen->store, screen->http,
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
        if (screen != nullptr && screen->icons != nullptr) {
            ImageList_Destroy(screen->icons);
            screen->icons = nullptr;
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
INT_PTR ShowAddonsDialog(HWND owner, HINSTANCE instance) {
    Screen screen;
    return DialogBoxParamW(instance, MAKEINTRESOURCEW(IDD_ADDONS), owner, AddonsDialogProc,
                           reinterpret_cast<LPARAM>(&screen));
}
