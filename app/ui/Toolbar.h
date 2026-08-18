#pragma once

#include <windows.h>
#include <commctrl.h>

// Owns the top toolbar control with the primary download actions and the search box.
class Toolbar {
public:
    ~Toolbar();

    bool Create(HWND parent, HINSTANCE instance);
    void Layout(int clientWidth);
    int Height() const;
    HWND Handle() const { return hwnd_; }
    HWND SearchHandle() const { return search_; }

private:
    void LayoutSearchBox(int clientWidth, int barHeight);

    HWND hwnd_ = nullptr;
    HWND search_ = nullptr;
    HIMAGELIST imageList_ = nullptr;
};
