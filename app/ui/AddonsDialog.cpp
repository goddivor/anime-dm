#include "ui/AddonsDialog.h"

#include <commctrl.h>

#include "ui/Resource.h"
#include "ui/Theme.h"
#include "ui/Strings.h"

namespace {

struct Column {
    StringId title;
    int width;
};

constexpr Column kColumns[] = {
    {STR_EXT_NAME, 240},
    {STR_EXT_LANG, 90},
    {STR_EXT_VERSION, 90},
    {STR_EXT_STATUS, 120},
};

// Applies the active language to every caption of the window.
void Retranslate(HWND dialog) {
    SetDialogTitle(dialog, STR_VIEW_ADDONS);
    SetDialogText(dialog, IDCANCEL, STR_DLG_CLOSE);
}

// Inserts the report columns of the extension list.
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

// Dialog procedure: the list fills in once the addon registry is wired up.
INT_PTR CALLBACK AddonsDialogProc(HWND dialog, UINT msg, WPARAM wParam, LPARAM lParam) {
    INT_PTR colour = 0;
    if (ThemeDialogMessage(msg, wParam, &colour)) {
        return colour;
    }

    switch (msg) {
    case WM_INITDIALOG:
        ActiveTheme().ApplyToDialog(dialog);
        Retranslate(dialog);
        InitList(dialog);
        return TRUE;
    case WM_COMMAND:
        if (LOWORD(wParam) == IDOK || LOWORD(wParam) == IDCANCEL) {
            EndDialog(dialog, LOWORD(wParam));
            return TRUE;
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

// Runs the addon store modally against its owner window.
INT_PTR ShowAddonsDialog(HWND owner, HINSTANCE instance) {
    return DialogBoxParamW(instance, MAKEINTRESOURCEW(IDD_ADDONS), owner, AddonsDialogProc, 0);
}
