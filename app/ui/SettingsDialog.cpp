#include "ui/SettingsDialog.h"

#include <string>
#include <vector>

#include "core/FolderIcon.h"
#include "ui/Resource.h"
#include "ui/Strings.h"
#include "ui/TemplateNames.h"
#include "ui/Theme.h"

namespace {

// What the dialog works on.
struct Screen {
    Settings* settings = nullptr;
    Settings original;
    std::vector<std::string> templates;
};

// Enables the template combo only while folder icons are on.
void SyncTemplateState(HWND dialog) {
    bool enabled = IsDlgButtonChecked(dialog, IDC_SET_FOLDER_ICONS) == BST_CHECKED;
    EnableWindow(GetDlgItem(dialog, IDC_SET_TEMPLATE), enabled);
    EnableWindow(GetDlgItem(dialog, IDC_SET_LBL_TEMPLATE), enabled);
}

// Applies the active language to every caption of the dialog.
void Retranslate(HWND dialog) {
    SetDialogTitle(dialog, STR_DLG_SETTINGS_TITLE);
    SetDialogText(dialog, IDC_SET_LBL_ICONS, STR_DLG_SET_ICONS);
    SetDialogText(dialog, IDC_SET_FOLDER_ICONS, STR_DLG_SET_ICONS_DESC);
    SetDialogText(dialog, IDC_SET_LBL_TEMPLATE, STR_DLG_SET_TEMPLATE);
    SetDialogText(dialog, IDC_SET_LBL_ANIYOMI, STR_DLG_SET_ANIYOMI);
    SetDialogText(dialog, IDC_SET_ANIYOMI, STR_DLG_SET_ANIYOMI_DESC);
    SetDialogText(dialog, IDC_SET_LBL_CLIPBOARD, STR_SET_CLIPBOARD_TITLE);
    SetDialogText(dialog, IDC_SET_CLIPBOARD, STR_SET_CLIPBOARD);
    SetDialogText(dialog, IDOK, STR_DLG_DONE);
}

// Shows the settings in the controls.
void InitControls(HWND dialog, Screen& screen) {
    Retranslate(dialog);
    const Settings& settings = *screen.settings;
    CheckDlgButton(dialog, IDC_SET_FOLDER_ICONS,
                   settings.folderIcons ? BST_CHECKED : BST_UNCHECKED);
    CheckDlgButton(dialog, IDC_SET_ANIYOMI, settings.aniyomi ? BST_CHECKED : BST_UNCHECKED);
    CheckDlgButton(dialog, IDC_SET_CLIPBOARD,
                   settings.clipboardUrl ? BST_CHECKED : BST_UNCHECKED);

    screen.templates = foldericon::TemplateIds();
    int chosen = 0;
    for (size_t i = 0; i < screen.templates.size(); ++i) {
        SendDlgItemMessageW(dialog, IDC_SET_TEMPLATE, CB_ADDSTRING, 0,
                            reinterpret_cast<LPARAM>(Str(TemplateName(screen.templates[i]))));
        if (screen.templates[i] == settings.folderTemplate) {
            chosen = static_cast<int>(i);
        }
    }
    SendDlgItemMessageW(dialog, IDC_SET_TEMPLATE, CB_SETCURSEL, chosen, 0);
    SyncTemplateState(dialog);
}

// Reads the controls back into the settings.
void ReadControls(HWND dialog, Screen& screen) {
    Settings& settings = *screen.settings;
    settings.folderIcons = IsDlgButtonChecked(dialog, IDC_SET_FOLDER_ICONS) == BST_CHECKED;
    settings.aniyomi = IsDlgButtonChecked(dialog, IDC_SET_ANIYOMI) == BST_CHECKED;
    settings.clipboardUrl = IsDlgButtonChecked(dialog, IDC_SET_CLIPBOARD) == BST_CHECKED;
    int chosen = static_cast<int>(SendDlgItemMessageW(dialog, IDC_SET_TEMPLATE, CB_GETCURSEL, 0, 0));
    if (chosen >= 0 && static_cast<size_t>(chosen) < screen.templates.size()) {
        settings.folderTemplate = screen.templates[static_cast<size_t>(chosen)];
    }
}

// Dialog procedure: the controls mirror the settings; Done closes.
INT_PTR CALLBACK SettingsDialogProc(HWND dialog, UINT msg, WPARAM wParam, LPARAM lParam) {
    INT_PTR colour = 0;
    if (ThemeDialogMessage(msg, wParam, &colour)) {
        return colour;
    }

    auto* screen = reinterpret_cast<Screen*>(GetWindowLongPtrW(dialog, GWLP_USERDATA));
    switch (msg) {
    case WM_INITDIALOG:
        SetWindowLongPtrW(dialog, GWLP_USERDATA, lParam);
        screen = reinterpret_cast<Screen*>(lParam);
        ActiveTheme().ApplyToDialog(dialog);
        InitControls(dialog, *screen);
        return TRUE;
    case WM_COMMAND:
        switch (LOWORD(wParam)) {
        case IDC_SET_FOLDER_ICONS:
            SyncTemplateState(dialog);
            return TRUE;
        case IDOK:
        case IDCANCEL:
            ReadControls(dialog, *screen);
            EndDialog(dialog, IDOK);
            return TRUE;
        default:
            return FALSE;
        }
    case WM_CLOSE:
        ReadControls(dialog, *screen);
        EndDialog(dialog, IDOK);
        return TRUE;
    default:
        return FALSE;
    }
}

// Whether two settings differ in what the dialog edits.
bool Differs(const Settings& a, const Settings& b) {
    return a.folderIcons != b.folderIcons || a.folderTemplate != b.folderTemplate ||
           a.aniyomi != b.aniyomi || a.clipboardUrl != b.clipboardUrl;
}

}  // namespace

// Shows the options dialog over the settings.
bool ShowSettingsDialog(HWND owner, HINSTANCE instance, Settings* settings) {
    Screen screen;
    screen.settings = settings;
    screen.original = *settings;
    DialogBoxParamW(instance, MAKEINTRESOURCEW(IDD_SETTINGS), owner, SettingsDialogProc,
                    reinterpret_cast<LPARAM>(&screen));
    return Differs(screen.original, *settings);
}
