#pragma once

#include <windows.h>
#include <commctrl.h>

// Owns the top toolbar control with the primary download actions and the search box.
class Toolbar {
public:
    ~Toolbar();

    bool Create(HWND parent, HINSTANCE instance);
    void Resize();
    int Height() const;
    HWND Handle() const { return hwnd_; }

private:
    HWND hwnd_ = nullptr;
    HIMAGELIST imageList_ = nullptr;
};
