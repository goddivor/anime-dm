#pragma once

#include <windows.h>

// Owns the left-hand TreeView listing download categories and animes.
class Sidebar {
public:
    bool Create(HWND parent, HINSTANCE instance);
    void SetBounds(int x, int y, int width, int height);
    HWND Handle() const { return hwnd_; }

private:
    void Populate();
    HTREEITEM Insert(HTREEITEM parent, const wchar_t* text);

    HWND hwnd_ = nullptr;
};
