#pragma once

#include <windows.h>

// Shows the modal settings dialog. Returns IDOK or IDCANCEL.
INT_PTR ShowSettingsDialog(HWND owner, HINSTANCE instance);
