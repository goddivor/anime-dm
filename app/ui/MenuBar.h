#pragma once

#include <windows.h>

// Builds the application menu bar and attaches it to a window.
class MenuBar {
public:
    void AttachTo(HWND window);
};
