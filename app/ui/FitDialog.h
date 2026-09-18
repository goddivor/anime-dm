#pragma once

#include <windows.h>

// Lets a message dialog take the height of its text: the label grows or
// shrinks to hold its lines, every control under it moves by the same
// amount, and the window follows.
void FitDialogToText(HWND dialog, int textId);
