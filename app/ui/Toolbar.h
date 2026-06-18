#pragma once

#include <windows.h>

// Owns the top toolbar control with the primary download actions.
class Toolbar {
public:
    bool Create(HWND parent, HINSTANCE instance);
    void Resize();
    int Height() const;
    HWND Handle() const { return hwnd_; }

private:
    HWND hwnd_ = nullptr;
};
