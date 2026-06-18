#include "ui/SettingsDialog.h"

#include "ui/Resource.h"

namespace {

// Appends an option to a combo box owned by the dialog.
void AddOption(HWND dialog, int controlId, const wchar_t* text) {
    SendDlgItemMessageW(dialog, controlId, CB_ADDSTRING, 0, reinterpret_cast<LPARAM>(text));
}

// Enables the template combo only when custom folder icons are checked.
void SyncTemplateState(HWND dialog) {
    bool enabled = IsDlgButtonChecked(dialog, IDC_SET_FOLDER_ICONS) == BST_CHECKED;
    EnableWindow(GetDlgItem(dialog, IDC_SET_TEMPLATE), enabled);
}

// Fills the combos with their default options and initial selection.
void InitControls(HWND dialog) {
    AddOption(dialog, IDC_SET_LANG, L"Français");
    AddOption(dialog, IDC_SET_LANG, L"English");
    SendDlgItemMessageW(dialog, IDC_SET_LANG, CB_SETCURSEL, 0, 0);

    AddOption(dialog, IDC_SET_THEME, L"Système");
    AddOption(dialog, IDC_SET_THEME, L"Sombre");
    AddOption(dialog, IDC_SET_THEME, L"Clair");
    SendDlgItemMessageW(dialog, IDC_SET_THEME, CB_SETCURSEL, 0, 0);

    AddOption(dialog, IDC_SET_TEMPLATE, L"(par défaut)");
    SendDlgItemMessageW(dialog, IDC_SET_TEMPLATE, CB_SETCURSEL, 0, 0);

    SyncTemplateState(dialog);
}

// Dialog procedure: combos and toggles are live; persistence is a stub.
INT_PTR CALLBACK SettingsDialogProc(HWND dialog, UINT msg, WPARAM wParam, LPARAM lParam) {
    switch (msg) {
    case WM_INITDIALOG:
        InitControls(dialog);
        return TRUE;
    case WM_COMMAND:
        switch (LOWORD(wParam)) {
        case IDC_SET_FOLDER_ICONS:
            SyncTemplateState(dialog);
            return TRUE;
        case IDOK:
        case IDCANCEL:
            EndDialog(dialog, LOWORD(wParam));
            return TRUE;
        case IDC_SET_BROWSE:
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

// Runs the settings dialog modally against its owner window.
INT_PTR ShowSettingsDialog(HWND owner, HINSTANCE instance) {
    return DialogBoxParamW(
        instance, MAKEINTRESOURCEW(IDD_SETTINGS), owner, SettingsDialogProc, 0);
}
