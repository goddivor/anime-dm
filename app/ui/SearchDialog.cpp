#include "ui/SearchDialog.h"

#include <commctrl.h>

#include "ui/Resource.h"

namespace {

struct Column {
    const wchar_t* title;
    int width;
};

constexpr Column kColumns[] = {
    {L"Nom du fichier", 220},
    {L"Animé", 150},
    {L"Statut", 90},
};

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
        col.pszText = const_cast<wchar_t*>(column.title);
        ListView_InsertColumn(results, index, &col);
        ++index;
    }
}

// Fills the scope combo and puts the caret in the query field.
void InitControls(HWND dialog) {
    const wchar_t* scopes[] = {L"Tout", L"Nom du fichier", L"Animé", L"Adresse"};
    for (const wchar_t* scope : scopes) {
        SendDlgItemMessageW(dialog, IDC_SEARCH_SCOPE, CB_ADDSTRING, 0,
                            reinterpret_cast<LPARAM>(scope));
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
