#include "ui/ConfirmDialog.h"

#include "ui/Resource.h"
#include "ui/Theme.h"

namespace {

// Dialog procedure: shows the question, reads the box back on OK.
INT_PTR CALLBACK ConfirmDialogProc(HWND dialog, UINT msg, WPARAM wParam, LPARAM lParam) {
    INT_PTR colour = 0;
    if (ThemeDialogMessage(msg, wParam, &colour)) {
        return colour;
    }

    auto* confirm = reinterpret_cast<Confirm*>(GetWindowLongPtrW(dialog, GWLP_USERDATA));
    switch (msg) {
    case WM_INITDIALOG: {
        confirm = reinterpret_cast<Confirm*>(lParam);
        SetWindowLongPtrW(dialog, GWLP_USERDATA, lParam);
        ActiveTheme().ApplyToDialog(dialog);
        SetDialogTitle(dialog, confirm->title);
        if (confirm->text.empty()) {
            SetDialogText(dialog, IDC_CONFIRM_TEXT, confirm->message);
        } else {
            SetDlgItemTextW(dialog, IDC_CONFIRM_TEXT, confirm->text.c_str());
        }
        SetDialogText(dialog, IDOK, confirm->okLabel);
        SetDialogText(dialog, IDCANCEL, STR_DLG_CANCEL);
        HWND check = GetDlgItem(dialog, IDC_CONFIRM_CHECK);
        if (confirm->checkLabel == STR_COUNT) {
            ShowWindow(check, SW_HIDE);
        } else {
            SetDialogText(dialog, IDC_CONFIRM_CHECK, confirm->checkLabel);
            CheckDlgButton(dialog, IDC_CONFIRM_CHECK,
                           confirm->checked ? BST_CHECKED : BST_UNCHECKED);
        }
        return TRUE;
    }
    case WM_COMMAND:
        if (LOWORD(wParam) == IDOK) {
            confirm->checked = IsDlgButtonChecked(dialog, IDC_CONFIRM_CHECK) == BST_CHECKED;
            EndDialog(dialog, IDOK);
            return TRUE;
        }
        if (LOWORD(wParam) == IDCANCEL) {
            EndDialog(dialog, IDCANCEL);
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

// Asks the question in its own small dialog.
bool ShowConfirm(HWND owner, HINSTANCE instance, Confirm* confirm) {
    return DialogBoxParamW(instance, MAKEINTRESOURCEW(IDD_CONFIRM), owner, ConfirmDialogProc,
                           reinterpret_cast<LPARAM>(confirm)) == IDOK;
}
