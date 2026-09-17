#include "ui/SearchDialog.h"

#include <commctrl.h>

#include <algorithm>
#include <string>

#include "core/Text.h"
#include "ui/DownloadsView.h"
#include "ui/FileIcons.h"
#include "ui/Resource.h"
#include "ui/Strings.h"
#include "ui/Theme.h"

namespace {

constexpr int kRowPadding = 6;   // above and below the picture of a row
constexpr int kRowHeight = 36;

// What the window keeps for the whole of its life.
struct Screen {
    const std::vector<DownloadItem>* items = nullptr;
    const std::function<void(uint64_t)>* reveal = nullptr;
    std::vector<const DownloadItem*> matches;  // the rows, in the order of the list
    HFONT bold = nullptr;
    HFONT small = nullptr;
};

// Lowercases a text for a case-blind comparison.
std::wstring Fold(std::wstring text) {
    CharLowerBuffW(text.data(), static_cast<DWORD>(text.size()));
    return text;
}

std::wstring ReadQuery(HWND dialog) {
    HWND field = GetDlgItem(dialog, IDC_SEARCH_QUERY);
    int length = GetWindowTextLengthW(field);
    if (length <= 0) {
        return std::wstring();
    }
    std::wstring text(static_cast<size_t>(length), L'\0');
    GetWindowTextW(field, text.data(), length + 1);
    return text;
}

// Whether an item answers the query within the chosen scope: 0 anywhere,
// 1 the file name, 2 the anime, 3 the address of the video.
bool Matches(const DownloadItem& item, const std::wstring& needle, int scope) {
    if (needle.empty()) {
        return true;
    }
    auto has = [&](const std::wstring& text) { return Fold(text).find(needle) != std::wstring::npos; };
    switch (scope) {
    case 1:
        return has(FileNameOf(item.outPath));
    case 2:
        return has(Widen(item.animeTitle));
    case 3:
        return has(Widen(item.address)) || has(Widen(item.pageUrl));
    default:
        return has(FileNameOf(item.outPath)) || has(Widen(item.animeTitle)) ||
               has(Widen(item.address));
    }
}

// Runs the query over the queue and rebuilds the rows.
void Refilter(HWND dialog, Screen& screen) {
    std::wstring needle = Fold(ReadQuery(dialog));
    int scope = static_cast<int>(SendDlgItemMessageW(dialog, IDC_SEARCH_SCOPE, CB_GETCURSEL, 0, 0));

    HWND list = GetDlgItem(dialog, IDC_SEARCH_RESULTS);
    screen.matches.clear();
    ListView_DeleteAllItems(list);
    for (const DownloadItem& item : *screen.items) {
        if (!Matches(item, needle, scope)) {
            continue;
        }
        std::wstring name = FileNameOf(item.outPath);
        LVITEMW row = {};
        row.mask = LVIF_TEXT | LVIF_PARAM;
        row.iItem = static_cast<int>(screen.matches.size());
        row.pszText = name.data();
        row.lParam = static_cast<LPARAM>(item.id);
        ListView_InsertItem(list, &row);
        screen.matches.push_back(&item);
    }
    if (!screen.matches.empty()) {
        ListView_SetItemState(list, 0, LVIS_SELECTED | LVIS_FOCUSED, LVIS_SELECTED | LVIS_FOCUSED);
    }

    wchar_t count[64] = {};
    wsprintfW(count, Str(STR_SEARCH_COUNT), static_cast<int>(screen.matches.size()));
    std::wstring foot = count;
    if (!screen.matches.empty()) {
        foot += L"  ·  ";
        foot += Str(STR_SEARCH_HINT);
    }
    SetDlgItemTextW(dialog, IDC_SEARCH_LBL_SCOPE, foot.c_str());
}

// Hands the chosen row to the owner and closes.
void Reveal(HWND dialog, Screen& screen) {
    HWND list = GetDlgItem(dialog, IDC_SEARCH_RESULTS);
    int row = ListView_GetNextItem(list, -1, LVNI_SELECTED);
    if (row < 0 || static_cast<size_t>(row) >= screen.matches.size()) {
        return;
    }
    (*screen.reveal)(screen.matches[static_cast<size_t>(row)]->id);
    EndDialog(dialog, IDOK);
}

// Paints one row: the icon of the file type, the file name above the anime,
// and the state of the transfer against the right edge.
void DrawRow(const Screen& screen, NMLVCUSTOMDRAW* draw) {
    HWND list = draw->nmcd.hdr.hwndFrom;
    int row = static_cast<int>(draw->nmcd.dwItemSpec);
    if (row < 0 || static_cast<size_t>(row) >= screen.matches.size()) {
        return;
    }
    const DownloadItem& item = *screen.matches[static_cast<size_t>(row)];
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

    int x = bounds.left + kRowPadding + 2;
    int icon = fileicons::IndexOf(item.outPath);
    int size = fileicons::Size();
    if (icon >= 0) {
        ImageList_Draw(fileicons::SmallList(), icon, dc, x,
                       (bounds.top + bounds.bottom - size) / 2, ILD_TRANSPARENT);
    }
    x += size + kRowPadding + 2;

    SetBkMode(dc, TRANSPARENT);
    HFONT previous = static_cast<HFONT>(SelectObject(dc, screen.small));
    std::wstring state = StatusText(item);
    RECT stateBox = {x, bounds.top, bounds.right - kRowPadding - 2, bounds.bottom};
    COLORREF stateColour = item.status == DownloadStatus::Completed ? colours.ok
                           : item.status == DownloadStatus::Failed  ? colours.bad
                                                                     : colours.muted;
    SetTextColor(dc, selected ? colours.accentText : stateColour);
    DrawTextW(dc, state.c_str(), -1, &stateBox, DT_RIGHT | DT_VCENTER | DT_SINGLELINE | DT_NOPREFIX);
    RECT stateExtent = stateBox;
    DrawTextW(dc, state.c_str(), -1, &stateExtent, DT_SINGLELINE | DT_CALCRECT | DT_NOPREFIX);
    int textRight = stateBox.right - (stateExtent.right - stateExtent.left) - kRowPadding * 2;

    int middle = (bounds.top + bounds.bottom) / 2;
    SelectObject(dc, screen.bold);
    SetTextColor(dc, selected ? colours.accentText : colours.text);
    RECT nameBox = {x, bounds.top + kRowPadding / 2, textRight, middle};
    DrawTextW(dc, FileNameOf(item.outPath).c_str(), -1, &nameBox,
              DT_LEFT | DT_BOTTOM | DT_SINGLELINE | DT_END_ELLIPSIS | DT_NOPREFIX);

    SelectObject(dc, screen.small);
    SetTextColor(dc, selected ? colours.accentText : colours.muted);
    RECT animeBox = {x, middle, textRight, bounds.bottom - kRowPadding / 2};
    DrawTextW(dc, Widen(item.animeTitle).c_str(), -1, &animeBox,
              DT_LEFT | DT_TOP | DT_SINGLELINE | DT_END_ELLIPSIS | DT_NOPREFIX);
    SelectObject(dc, previous);
}

// One column as wide as the list, rows as tall as two lines; the fonts of
// the two lines derive from the one of the dialog.
void InitResults(HWND dialog, Screen& screen) {
    HWND list = GetDlgItem(dialog, IDC_SEARCH_RESULTS);
    ListView_SetExtendedListViewStyle(list, LVS_EX_FULLROWSELECT | LVS_EX_DOUBLEBUFFER);

    RECT bounds = {};
    GetClientRect(list, &bounds);
    LVCOLUMNW col = {};
    col.mask = LVCF_WIDTH;
    col.cx = bounds.right - bounds.left;
    ListView_InsertColumn(list, 0, &col);

    // An image list of the right height sets the height of the rows.
    HIMAGELIST spacer = ImageList_Create(1, kRowHeight, ILC_COLOR32, 0, 0);
    ListView_SetImageList(list, spacer, LVSIL_SMALL);

    HFONT base = reinterpret_cast<HFONT>(SendMessageW(dialog, WM_GETFONT, 0, 0));
    LOGFONTW description = {};
    GetObjectW(base, sizeof(description), &description);
    description.lfWeight = FW_SEMIBOLD;
    screen.bold = CreateFontIndirectW(&description);
    description.lfWeight = FW_NORMAL;
    description.lfHeight = description.lfHeight * 9 / 10;
    screen.small = CreateFontIndirectW(&description);
}

// Fills the scope combo and puts the caret in the query field.
void InitControls(HWND dialog, Screen& screen) {
    SetDialogTitle(dialog, STR_DLG_SEARCH_TITLE);
    SetDialogText(dialog, IDC_SEARCH_LBL_QUERY, STR_DLG_SEARCH_QUERY);

    const StringId scopes[] = {STR_SCOPE_ALL, STR_SCOPE_FILENAME, STR_SCOPE_ANIME,
                               STR_SCOPE_ADDRESS};
    for (StringId scope : scopes) {
        SendDlgItemMessageW(dialog, IDC_SEARCH_SCOPE, CB_ADDSTRING, 0,
                            reinterpret_cast<LPARAM>(Str(scope)));
    }
    SendDlgItemMessageW(dialog, IDC_SEARCH_SCOPE, CB_SETCURSEL, 0, 0);

    InitResults(dialog, screen);
    Refilter(dialog, screen);
    SetFocus(GetDlgItem(dialog, IDC_SEARCH_QUERY));
}

INT_PTR CALLBACK SearchDialogProc(HWND dialog, UINT msg, WPARAM wParam, LPARAM lParam) {
    INT_PTR colour = 0;
    if (ThemeDialogMessage(msg, wParam, &colour)) {
        return colour;
    }
    auto* screen = reinterpret_cast<Screen*>(GetWindowLongPtrW(dialog, GWLP_USERDATA));

    switch (msg) {
    case WM_INITDIALOG:
        SetWindowLongPtrW(dialog, GWLP_USERDATA, lParam);
        screen = reinterpret_cast<Screen*>(lParam);
        ActiveTheme().ApplyToDialog(dialog);
        InitControls(dialog, *screen);
        return FALSE;

    case WM_NOTIFY: {
        auto* notify = reinterpret_cast<NMHDR*>(lParam);
        if (screen == nullptr || notify->idFrom != IDC_SEARCH_RESULTS) {
            return FALSE;
        }
        if (notify->code == NM_DBLCLK) {
            Reveal(dialog, *screen);
            return TRUE;
        }
        if (notify->code == NM_CUSTOMDRAW) {
            auto* draw = reinterpret_cast<NMLVCUSTOMDRAW*>(lParam);
            LRESULT answer = CDRF_DODEFAULT;
            if (draw->nmcd.dwDrawStage == CDDS_PREPAINT) {
                answer = CDRF_NOTIFYITEMDRAW;
            } else if (draw->nmcd.dwDrawStage == CDDS_ITEMPREPAINT) {
                DrawRow(*screen, draw);
                answer = CDRF_SKIPDEFAULT;
            }
            SetWindowLongPtrW(dialog, DWLP_MSGRESULT, answer);
            return TRUE;
        }
        return FALSE;
    }

    case WM_COMMAND:
        if (screen == nullptr) {
            return FALSE;
        }
        switch (LOWORD(wParam)) {
        case IDC_SEARCH_QUERY:
            if (HIWORD(wParam) == EN_CHANGE) {
                Refilter(dialog, *screen);
            }
            return TRUE;
        case IDC_SEARCH_SCOPE:
            if (HIWORD(wParam) == CBN_SELCHANGE) {
                Refilter(dialog, *screen);
            }
            return TRUE;
        case IDOK:
            Reveal(dialog, *screen);
            return TRUE;
        case IDCANCEL:
            EndDialog(dialog, IDCANCEL);
            return TRUE;
        default:
            return FALSE;
        }

    case WM_DESTROY:
        if (screen != nullptr) {
            for (HFONT* font : {&screen->bold, &screen->small}) {
                if (*font != nullptr) {
                    DeleteObject(*font);
                    *font = nullptr;
                }
            }
            HIMAGELIST spacer = ListView_GetImageList(GetDlgItem(dialog, IDC_SEARCH_RESULTS),
                                                      LVSIL_SMALL);
            if (spacer != nullptr) {
                ImageList_Destroy(spacer);
            }
        }
        return FALSE;

    case WM_CLOSE:
        EndDialog(dialog, IDCANCEL);
        return TRUE;

    default:
        return FALSE;
    }
}

}  // namespace

// Runs the search window modally against its owner window.
INT_PTR ShowSearchDialog(HWND owner, HINSTANCE instance, const std::vector<DownloadItem>& items,
                         const std::function<void(uint64_t)>& reveal) {
    Screen screen;
    screen.items = &items;
    screen.reveal = &reveal;
    return DialogBoxParamW(instance, MAKEINTRESOURCEW(IDD_SEARCH), owner, SearchDialogProc,
                           reinterpret_cast<LPARAM>(&screen));
}
