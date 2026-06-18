#include "ui/AddDialog.h"

#include <commctrl.h>

#include "ui/Resource.h"

namespace {

// Configures the episodes ListView with checkboxes and its columns.
void InitEpisodesList(HWND dialog) {
    HWND list = GetDlgItem(dialog, IDC_ADD_EPISODES);
    ListView_SetExtendedListViewStyle(
        list, LVS_EX_CHECKBOXES | LVS_EX_FULLROWSELECT | LVS_EX_GRIDLINES);

    LVCOLUMNW col = {};
    col.mask = LVCF_TEXT | LVCF_WIDTH | LVCF_SUBITEM;

    col.iSubItem = 0;
    col.cx = 70;
    col.pszText = const_cast<wchar_t*>(L"Épisode");
    ListView_InsertColumn(list, 0, &col);

    col.iSubItem = 1;
    col.cx = 520;
    col.pszText = const_cast<wchar_t*>(L"Titre");
    ListView_InsertColumn(list, 1, &col);
}

// Dialog procedure: wires the standard buttons; data actions are stubs.
INT_PTR CALLBACK AddDialogProc(HWND dialog, UINT msg, WPARAM wParam, LPARAM lParam) {
    switch (msg) {
    case WM_INITDIALOG:
        InitEpisodesList(dialog);
        return TRUE;
    case WM_COMMAND:
        switch (LOWORD(wParam)) {
        case IDOK:
        case IDCANCEL:
            EndDialog(dialog, LOWORD(wParam));
            return TRUE;
        case IDC_ADD_FETCH:
        case IDC_ADD_BROWSE:
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

// Runs the dialog modally against its owner window.
INT_PTR ShowAddDialog(HWND owner, HINSTANCE instance) {
    return DialogBoxParamW(
        instance, MAKEINTRESOURCEW(IDD_ADD_DOWNLOAD), owner, AddDialogProc, 0);
}
