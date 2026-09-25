#pragma once

#include <windows.h>

// Shows the modal "À propos" dialog, after the one of IDM.
void ShowAboutDialog(HWND owner, HINSTANCE instance);

// Opens the website of the application in the browser.
void OpenWebsite(HWND owner);

// Shows the modal keyboard-shortcuts dialog.
void ShowShortcutsDialog(HWND owner, HINSTANCE instance);
