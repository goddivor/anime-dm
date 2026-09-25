#pragma once

#include <vector>

#include <windows.h>

// Lets the user choose the columns of the file list and their order. In:
// the columns on screen, left to right; out, on OK: the chosen ones. True
// when the user confirmed.
bool ShowColumnsDialog(HWND owner, HINSTANCE instance, std::vector<int>* shown);
