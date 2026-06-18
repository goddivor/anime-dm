#pragma once

#include <windows.h>

// Shows the modal "add download" dialog. Returns IDOK or IDCANCEL.
INT_PTR ShowAddDialog(HWND owner, HINSTANCE instance);
