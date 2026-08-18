#pragma once

#include <windows.h>

// Shows the modal search dialog. Returns IDOK or IDCANCEL.
INT_PTR ShowSearchDialog(HWND owner, HINSTANCE instance);
