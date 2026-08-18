#include "ui/SearchDialog.h"

#include <commctrl.h>

#include "ui/Resource.h"
#include "ui/Strings.h"

namespace {

struct Column {
    StringId title;
    int width;
};

constexpr Column kColumns[] = {
    {STR_COL_FILENAME, 220},
    {STR_DLG_SEARCH_ANIME_COL, 150},
    {STR_COL_STATUS, 90},
};

// Applies the active language to every caption of the dialog.
void Retranslate(HWND dialog) {
    SetDialogTitle(dialog, STR_DLG_SEARCH_TITLE);
    SetDialogText(dialog, IDC_SEARCH_LBL_QUERY, STR_DLG_SEARCH_QUERY);
    SetDialogText(dialog, IDC_SEARCH_LBL_SCOPE, STR_DLG_SEARCH_SCOPE);
    SetDialogText(dialog, IDOK, STR_DLG_SEARCH_RUN);
    SetDialogText(dialog, IDCANCEL, STR_DLG_CLOSE);
}

// Inserts the report columns of the results list.
void InitResults(HWND dialog) {
    HWND results = GetDlgItem(dialog, IDC_SEARCH_RESULTS);
    ListView_SetExtendedListViewStyle(results, LVS_EX_FULLROWSELECT | LVS_EX_DOUBLEBUFFER);

    LVCOLUMNW col = {};
    col.mask = LVCF_TEXT | LVCF_WIDTH | LVCF_SUBITEM;
    int index = 0;
    for (const Column& column : kColumns) {
        col.iSubItem = index;
        col.cx = column.width;
        col.pszText = const_cast<wchar_t*>(Str(column.title));
        ListView_InsertColumn(results, index, &col);
        ++index;
    }
}

// Fills the scope combo and puts the caret in the query field.
void InitControls(HWND dialog) {
    Retranslate(dialog);

    const StringId scopes[] = {STR_SCOPE_ALL, STR_SCOPE_FILENAME, STR_SCOPE_ANIME,
                               STR_SCOPE_ADDRESS};
    for (StringId scope : scopes) {
        SendDlgItemMessageW(dialog, IDC_SEARCH_SCOPE, CB_ADDSTRING, 0,
                            reinterpret_cast<LPARAM>(Str(scope)));
    }
    SendDlgItemMessageW(dialog, IDC_SEARCH_SCOPE, CB_SETCURSEL, 0, 0);

    InitResults(dialog);
    SetFocus(GetDlgItem(dialog, IDC_SEARCH_QUERY));
}

// Clears the results; matching runs once the download list is wired up.
void RunSearch(HWND dialog) {
    ListView_DeleteAllItems(GetDlgItem(dialog, IDC_SEARCH_RESULTS));
}

// Dialog procedure: the query runs on demand, matching is a stub.
INT_PTR CALLBACK SearchDialogProc(HWND dialog, UINT msg, WPARAM wParam, LPARAM lParam) {
    switch (msg) {
    case WM_INITDIALOG:
        InitControls(dialog);
        return FALSE;
    case WM_COMMAND:
        switch (LOWORD(wParam)) {
        case IDOK:
            RunSearch(dialog);
            return TRUE;
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

// Runs the search dialog modally against its owner window.
INT_PTR ShowSearchDialog(HWND owner, HINSTANCE instance) {
    return DialogBoxParamW(instance, MAKEINTRESOURCEW(IDD_SEARCH), owner, SearchDialogProc, 0);
}
