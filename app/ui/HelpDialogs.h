#pragma once

#include <windows.h>

// Shows the modal "À propos" dialog, after the one of IDM. Returns
// IDC_ABOUT_UPDATE when the user asked to check for updates, which the
// caller does once the dialog is gone.
INT_PTR ShowAboutDialog(HWND owner, HINSTANCE instance);

// Opens the website of the application in the browser.
void OpenWebsite(HWND owner);

// Shows the modal keyboard-shortcuts dialog.
void ShowShortcutsDialog(HWND owner, HINSTANCE instance);
