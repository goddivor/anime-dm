#pragma once

#include <windows.h>

// Shows the modal "À propos" dialog, after the one of IDM. Returns
// IDC_ABOUT_ADDONS when the user asked for the Addon Store, which the caller
// opens once the dialog is gone.
INT_PTR ShowAboutDialog(HWND owner, HINSTANCE instance);

// Shows the modal keyboard-shortcuts dialog.
void ShowShortcutsDialog(HWND owner, HINSTANCE instance);
