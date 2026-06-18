#include "ui/HelpDialogs.h"

#include "ui/Resource.h"

namespace {

// Generic procedure for static information dialogs: closes on OK/Cancel.
INT_PTR CALLBACK InfoDialogProc(HWND dialog, UINT msg, WPARAM wParam, LPARAM lParam) {
    switch (msg) {
    case WM_INITDIALOG:
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

// Runs the about dialog modally.
void ShowAboutDialog(HWND owner, HINSTANCE instance) {
    DialogBoxParamW(instance, MAKEINTRESOURCEW(IDD_ABOUT), owner, InfoDialogProc, 0);
}

// Runs the shortcuts dialog modally.
void ShowShortcutsDialog(HWND owner, HINSTANCE instance) {
    DialogBoxParamW(instance, MAKEINTRESOURCEW(IDD_SHORTCUTS), owner, InfoDialogProc, 0);
}
