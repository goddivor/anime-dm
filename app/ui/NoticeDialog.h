#pragma once

#include <windows.h>

// Shows a short message in its own small dialog.
void ShowNotice(HWND owner, HINSTANCE instance, const wchar_t* message);
