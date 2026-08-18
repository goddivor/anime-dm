#pragma once

#include <windows.h>
#include <commctrl.h>

// Owns the left-hand categories panel: a caption bar plus the category TreeView.
class Sidebar {
public:
    ~Sidebar();

    bool Create(HWND parent, HINSTANCE instance);
    void SetBounds(int x, int y, int width, int height);
    void SetVisible(bool visible);
    HWND Handle() const { return tree_; }
    HWND HeaderHandle() const { return header_; }

private:
    void Populate();
    HTREEITEM Insert(HTREEITEM parent, const wchar_t* text, int icon, int selectedIcon);

    HWND header_ = nullptr;
    HWND tree_ = nullptr;
    HIMAGELIST icons_ = nullptr;
};
