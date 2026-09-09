#pragma once

#include <string>
#include <vector>

#include <windows.h>

class Theme;

// Builds the application menu bar and keeps its checkable items in sync.
class MenuBar {
public:
    ~MenuBar();

    void AttachTo(HWND window);
    void Rebuild(HWND window);
    void ApplyTheme(const Theme& theme, HWND window);
    bool MeasureItem(MEASUREITEMSTRUCT* measure, HWND window) const;
    bool DrawItem(const DRAWITEMSTRUCT* draw) const;
    void SetCategoriesChecked(bool checked);
    void SetTheme(int commandId);
    void SetLanguage(int commandId);

    // Names the toolbar skins the View menu offers, and ticks the chosen one
    // (-1 for the icon font). Takes effect on the next rebuild.
    void SetToolbarSkins(const std::vector<std::wstring>& names, int chosen);
    void SetToolbarSkin(int chosen);

private:
    HFONT MenuFont() const;

    HMENU bar_ = nullptr;
    HBRUSH background_ = nullptr;
    std::vector<std::wstring> skins_;
    int skin_ = -1;
    mutable HFONT font_ = nullptr;
    COLORREF surface_ = 0;
    COLORREF text_ = 0;
    COLORREF highlight_ = 0;
    bool dark_ = false;
};
