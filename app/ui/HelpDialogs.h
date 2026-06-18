#pragma once

#include <windows.h>

// Shows the modal "À propos" dialog.
void ShowAboutDialog(HWND owner, HINSTANCE instance);

// Shows the modal keyboard-shortcuts dialog.
void ShowShortcutsDialog(HWND owner, HINSTANCE instance);
