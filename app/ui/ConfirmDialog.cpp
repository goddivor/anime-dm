#include "ui/ConfirmDialog.h"

#include "ui/FitDialog.h"
#include "ui/Resource.h"
#include "ui/Theme.h"

namespace {

// Closes the gap the hidden check box would leave: the buttons and the
// window come up by its row.
void HideCheckRow(HWND dialog, HWND check) {
    RECT row = {};
    GetWindowRect(check, &row);
    MapWindowPoints(nullptr, dialog, reinterpret_cast<POINT*>(&row), 2);
    RECT text = {};
    GetWindowRect(GetDlgItem(dialog, IDC_CONFIRM_TEXT), &text);
    MapWindowPoints(nullptr, dialog, reinterpret_cast<POINT*>(&text), 2);
    int by = row.bottom - text.bottom;
    for (int id : {IDOK, IDCANCEL}) {
        HWND button = GetDlgItem(dialog, id);
        RECT rect = {};
        GetWindowRect(button, &rect);
        MapWindowPoints(nullptr, dialog, reinterpret_cast<POINT*>(&rect), 2);
        SetWindowPos(button, nullptr, rect.left, rect.top - by, 0, 0,
                     SWP_NOSIZE | SWP_NOZORDER | SWP_NOACTIVATE);
    }
    RECT window = {};
    GetWindowRect(dialog, &window);
    SetWindowPos(dialog, nullptr, 0, 0, window.right - window.left,
                 window.bottom - window.top - by, SWP_NOMOVE | SWP_NOZORDER | SWP_NOACTIVATE);
}

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
            HideCheckRow(dialog, check);
        } else {
            SetDialogText(dialog, IDC_CONFIRM_CHECK, confirm->checkLabel);
            CheckDlgButton(dialog, IDC_CONFIRM_CHECK,
                           confirm->checked ? BST_CHECKED : BST_UNCHECKED);
        }
        FitDialogToText(dialog, IDC_CONFIRM_TEXT);
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
