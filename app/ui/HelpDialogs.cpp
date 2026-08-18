#include "ui/HelpDialogs.h"

#include "ui/Resource.h"
#include "ui/Strings.h"

namespace {

// Applies the active language to the about dialog.
void RetranslateAbout(HWND dialog) {
    SetDialogTitle(dialog, STR_DLG_ABOUT_TITLE);
    SetDialogText(dialog, IDC_ABOUT_TAGLINE, STR_DLG_ABOUT_TAGLINE);
    SetDialogText(dialog, IDOK, STR_DLG_OK);
}

// Applies the active language to the shortcuts dialog.
void RetranslateShortcuts(HWND dialog) {
    SetDialogTitle(dialog, STR_DLG_SHORTCUTS_TITLE);
    SetDialogText(dialog, IDC_SC_KEY_DELETE, STR_KEY_DELETE);
    SetDialogText(dialog, IDC_SC_KEY_BATCH, STR_KEY_BATCH);
    SetDialogText(dialog, IDC_SC_KEY_ESCAPE, STR_KEY_ESCAPE);
    SetDialogText(dialog, IDC_SC_TXT_ADD, STR_SC_ADD);
    SetDialogText(dialog, IDC_SC_TXT_REMOVE, STR_SC_REMOVE);
    SetDialogText(dialog, IDC_SC_TXT_SEARCH, STR_SC_SEARCH);
    SetDialogText(dialog, IDC_SC_TXT_BATCH, STR_SC_BATCH);
    SetDialogText(dialog, IDC_SC_TXT_HELP, STR_SC_HELP);
    SetDialogText(dialog, IDC_SC_TXT_CLOSE, STR_SC_CLOSE);
    SetDialogText(dialog, IDOK, STR_DLG_OK);
}

// Generic procedure for static information dialogs: closes on OK/Cancel.
INT_PTR CALLBACK InfoDialogProc(HWND dialog, UINT msg, WPARAM wParam, LPARAM lParam) {
    switch (msg) {
    case WM_INITDIALOG:
        if (lParam == IDD_SHORTCUTS) {
            RetranslateShortcuts(dialog);
        } else {
            RetranslateAbout(dialog);
        }
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
    DialogBoxParamW(instance, MAKEINTRESOURCEW(IDD_ABOUT), owner, InfoDialogProc, IDD_ABOUT);
}

// Runs the shortcuts dialog modally.
void ShowShortcutsDialog(HWND owner, HINSTANCE instance) {
    DialogBoxParamW(instance, MAKEINTRESOURCEW(IDD_SHORTCUTS), owner, InfoDialogProc, IDD_SHORTCUTS);
}
