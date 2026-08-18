#pragma once

#include <windows.h>

// Shows the modal addon store window. Returns IDOK or IDCANCEL.
INT_PTR ShowAddonsDialog(HWND owner, HINSTANCE instance);
