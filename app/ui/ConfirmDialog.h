#pragma once

#include <windows.h>

#include "ui/Strings.h"

// What a confirmation asks, and the optional box it offers to tick.
struct Confirm {
    StringId title;
    StringId message;
    StringId okLabel;
    StringId checkLabel;  // STR_COUNT hides the box
    bool checked = false;  // in: the initial state; out: what the user chose
};

// Asks the question in its own small dialog. True when the user confirmed.
bool ShowConfirm(HWND owner, HINSTANCE instance, Confirm* confirm);
