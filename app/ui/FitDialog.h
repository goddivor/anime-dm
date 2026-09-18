#pragma once

#include <windows.h>

// Lets a message dialog take the height of its text: the label grows or
// shrinks to hold its lines, every control under it moves by the same
// amount, and the window follows.
void FitDialogToText(HWND dialog, int textId);

// Widens a button whose caption needs more room than it has, keeping its
// right edge where it is, so that a long caption never touches the border.
void FitButtonToCaption(HWND dialog, int buttonId);
