#include "ui/AddDialog.h"

#include <commctrl.h>

#include "ui/Resource.h"
#include "ui/Strings.h"

namespace {

// Applies the active language to every caption of the dialog.
void Retranslate(HWND dialog) {
    SetDialogTitle(dialog, STR_DLG_ADD_TITLE);
    SetDialogText(dialog, IDC_ADD_LBL_SOURCE, STR_DLG_ADD_SOURCE);
    SetDialogText(dialog, IDC_ADD_LBL_URL, STR_DLG_ADD_URL);
    SetDialogText(dialog, IDC_ADD_FETCH, STR_DLG_ADD_FETCH);
    SetDialogText(dialog, IDC_ADD_LBL_EPISODES, STR_DLG_ADD_EPISODES);
    SetDialogText(dialog, IDC_ADD_LBL_DEST, STR_DLG_ADD_DEST);
    SetDialogText(dialog, IDC_ADD_BROWSE, STR_DLG_BROWSE);
    SetDialogText(dialog, IDOK, STR_DLG_ADD_START);
    SetDialogText(dialog, IDCANCEL, STR_DLG_CANCEL);
}

// Configures the episodes ListView with checkboxes and its columns.
void InitEpisodesList(HWND dialog) {
    HWND list = GetDlgItem(dialog, IDC_ADD_EPISODES);
    ListView_SetExtendedListViewStyle(
        list, LVS_EX_CHECKBOXES | LVS_EX_FULLROWSELECT | LVS_EX_GRIDLINES);

    LVCOLUMNW col = {};
    col.mask = LVCF_TEXT | LVCF_WIDTH | LVCF_SUBITEM;

    col.iSubItem = 0;
    col.cx = 70;
    col.pszText = const_cast<wchar_t*>(Str(STR_DLG_ADD_EPISODES));
    ListView_InsertColumn(list, 0, &col);

    col.iSubItem = 1;
    col.cx = 520;
    col.pszText = const_cast<wchar_t*>(Str(STR_COL_FILENAME));
    ListView_InsertColumn(list, 1, &col);
}

// Dialog procedure: wires the standard buttons; data actions are stubs.
INT_PTR CALLBACK AddDialogProc(HWND dialog, UINT msg, WPARAM wParam, LPARAM lParam) {
    switch (msg) {
    case WM_INITDIALOG:
        Retranslate(dialog);
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
