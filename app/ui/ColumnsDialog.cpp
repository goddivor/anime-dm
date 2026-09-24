#include "ui/ColumnsDialog.h"

#include <commctrl.h>

#include <algorithm>
#include <utility>

#include "ui/DownloadsView.h"
#include "ui/FitDialog.h"
#include "ui/Resource.h"
#include "ui/Strings.h"
#include "ui/Theme.h"

namespace {

// One row of the list: a column and whether it is shown.
struct Row {
    int column;
    bool shown;
};

// What the dialog works on while it is open.
struct Screen {
    std::vector<int>* shown = nullptr;
    bool filling = false;  // the list is being written: its notices are ours
};

// The rows as the list holds them, top to bottom.
std::vector<Row> ReadRows(HWND list) {
    std::vector<Row> rows;
    int count = ListView_GetItemCount(list);
    for (int index = 0; index < count; ++index) {
        LVITEMW item = {};
        item.mask = LVIF_PARAM;
        item.iItem = index;
        ListView_GetItem(list, &item);
        rows.push_back({static_cast<int>(item.lParam), ListView_GetCheckState(list, index) != 0});
    }
    return rows;
}

// Writes the rows into the list and selects one of them.
void WriteRows(HWND list, Screen& screen, const std::vector<Row>& rows, int selected) {
    screen.filling = true;
    ListView_DeleteAllItems(list);
    for (size_t index = 0; index < rows.size(); ++index) {
        LVITEMW item = {};
        item.mask = LVIF_TEXT | LVIF_PARAM;
        item.iItem = static_cast<int>(index);
        item.pszText = const_cast<wchar_t*>(Str(DownloadsView::ColumnTitle(rows[index].column)));
        item.lParam = rows[index].column;
        ListView_InsertItem(list, &item);
        ListView_SetCheckState(list, static_cast<int>(index), rows[index].shown ? TRUE : FALSE);
    }
    if (selected >= 0) {
        ListView_SetItemState(list, selected, LVIS_SELECTED | LVIS_FOCUSED,
                              LVIS_SELECTED | LVIS_FOCUSED);
        ListView_EnsureVisible(list, selected, FALSE);
    }
    screen.filling = false;
}

// The shown columns first, in their order, then the hidden ones as declared.
std::vector<Row> RowsOf(const std::vector<int>& shown) {
    std::vector<Row> rows;
    for (int column : shown) {
        rows.push_back({column, true});
    }
    for (int column = 0; column < DownloadsView::ColumnCount(); ++column) {
        if (std::find(shown.begin(), shown.end(), column) == shown.end()) {
            rows.push_back({column, false});
        }
    }
    return rows;
}

// The file name stays on top: nothing moves above it, and it does not move.
void UpdateButtons(HWND dialog) {
    HWND list = GetDlgItem(dialog, IDC_COLUMNS_LIST);
    int selected = ListView_GetNextItem(list, -1, LVNI_SELECTED);
    int count = ListView_GetItemCount(list);
    EnableWindow(GetDlgItem(dialog, IDC_COLUMNS_UP), selected > 1);
    EnableWindow(GetDlgItem(dialog, IDC_COLUMNS_DOWN), selected >= 1 && selected < count - 1);
}

// Moves the selected row one step up or down.
void MoveSelected(HWND dialog, Screen& screen, int step) {
    HWND list = GetDlgItem(dialog, IDC_COLUMNS_LIST);
    int selected = ListView_GetNextItem(list, -1, LVNI_SELECTED);
    int target = selected + step;
    std::vector<Row> rows = ReadRows(list);
    if (selected < 1 || target < 1 || target >= static_cast<int>(rows.size())) {
        return;
    }
    std::swap(rows[static_cast<size_t>(selected)], rows[static_cast<size_t>(target)]);
    WriteRows(list, screen, rows, target);
    UpdateButtons(dialog);
    SetFocus(list);
}

// Dialog procedure: fills the list, moves its rows, reads it back on OK.
INT_PTR CALLBACK ColumnsDialogProc(HWND dialog, UINT msg, WPARAM wParam, LPARAM lParam) {
    INT_PTR colour = 0;
    if (ThemeDialogMessage(msg, wParam, &colour)) {
        return colour;
    }

    auto* screen = reinterpret_cast<Screen*>(GetWindowLongPtrW(dialog, GWLP_USERDATA));
    switch (msg) {
    case WM_INITDIALOG: {
        screen = reinterpret_cast<Screen*>(lParam);
        SetWindowLongPtrW(dialog, GWLP_USERDATA, lParam);
        SetDialogTitle(dialog, STR_VIEW_COLUMNS);
        SetDialogText(dialog, IDC_COLUMNS_HINT, STR_COLUMNS_HINT);
        SetDialogText(dialog, IDC_COLUMNS_UP, STR_COLUMNS_UP);
        SetDialogText(dialog, IDC_COLUMNS_DOWN, STR_COLUMNS_DOWN);
        SetDialogText(dialog, IDC_COLUMNS_RESET, STR_COLUMNS_RESET);
        SetDialogText(dialog, IDOK, STR_DLG_OK);
        SetDialogText(dialog, IDCANCEL, STR_DLG_CANCEL);

        HWND list = GetDlgItem(dialog, IDC_COLUMNS_LIST);
        ListView_SetExtendedListViewStyle(list, LVS_EX_CHECKBOXES | LVS_EX_FULLROWSELECT);
        LVCOLUMNW column = {};
        column.mask = LVCF_WIDTH;
        ListView_InsertColumn(list, 0, &column);
        ActiveTheme().ApplyToDialog(dialog);
        WriteRows(list, *screen, RowsOf(*screen->shown), 0);
        ListView_SetColumnWidth(list, 0, LVSCW_AUTOSIZE_USEHEADER);
        UpdateButtons(dialog);
        return TRUE;
    }
    case WM_NOTIFY: {
        auto* header = reinterpret_cast<NMHDR*>(lParam);
        if (header->idFrom != IDC_COLUMNS_LIST || screen == nullptr) {
            return FALSE;
        }
        auto* change = reinterpret_cast<NMLISTVIEW*>(lParam);
        // The file name cannot be unticked: the list needs one column to
        // hold its rows.
        if (header->code == LVN_ITEMCHANGING && !screen->filling &&
            (change->uChanged & LVIF_STATE) != 0 && change->lParam == 0 &&
            (change->uNewState & LVIS_STATEIMAGEMASK) == INDEXTOSTATEIMAGEMASK(1)) {
            SetWindowLongPtrW(dialog, DWLP_MSGRESULT, TRUE);
            return TRUE;
        }
        if (header->code == LVN_ITEMCHANGED && (change->uChanged & LVIF_STATE) != 0) {
            UpdateButtons(dialog);
        }
        return FALSE;
    }
    case WM_COMMAND:
        switch (LOWORD(wParam)) {
        case IDC_COLUMNS_UP:
            MoveSelected(dialog, *screen, -1);
            return TRUE;
        case IDC_COLUMNS_DOWN:
            MoveSelected(dialog, *screen, 1);
            return TRUE;
        case IDC_COLUMNS_RESET: {
            std::vector<int> all;
            for (int column = 0; column < DownloadsView::ColumnCount(); ++column) {
                all.push_back(column);
            }
            WriteRows(GetDlgItem(dialog, IDC_COLUMNS_LIST), *screen, RowsOf(all), 0);
            UpdateButtons(dialog);
            return TRUE;
        }
        case IDOK: {
            screen->shown->clear();
            for (const Row& row : ReadRows(GetDlgItem(dialog, IDC_COLUMNS_LIST))) {
                if (row.shown) {
                    screen->shown->push_back(row.column);
                }
            }
            EndDialog(dialog, IDOK);
            return TRUE;
        }
        case IDCANCEL:
            EndDialog(dialog, IDCANCEL);
            return TRUE;
        default:
            return FALSE;
        }
    case WM_CLOSE:
        EndDialog(dialog, IDCANCEL);
        return TRUE;
    default:
        return FALSE;
    }
}

}  // namespace

// Shows the dialog over the owner.
bool ShowColumnsDialog(HWND owner, HINSTANCE instance, std::vector<int>* shown) {
    Screen screen;
    screen.shown = shown;
    return DialogBoxParamW(instance, MAKEINTRESOURCEW(IDD_COLUMNS), owner, ColumnsDialogProc,
                           reinterpret_cast<LPARAM>(&screen)) == IDOK;
}
