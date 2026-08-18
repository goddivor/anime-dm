#pragma once

#include <windows.h>
#include <commctrl.h>

class Theme;

// Colours the caption bar paints itself with.
struct SidebarHeaderState {
    HFONT font;
    COLORREF surface;
    COLORREF text;
    COLORREF line;
};

// Owns the left-hand categories panel: a caption bar plus the category TreeView.
class Sidebar {
public:
    ~Sidebar();

    bool Create(HWND parent, HINSTANCE instance);
    void SetBounds(int x, int y, int width, int height);
    void SetVisible(bool visible);
    void Retranslate();
    void ApplyTheme(const Theme& theme);
    HWND Handle() const { return tree_; }
    HWND HeaderHandle() const { return header_; }

private:
    void Populate();
    HTREEITEM Insert(HTREEITEM parent, const wchar_t* text, int icon);

    HWND header_ = nullptr;
    HWND tree_ = nullptr;
    HIMAGELIST icons_ = nullptr;
    SidebarHeaderState headerState_ = {};
};
