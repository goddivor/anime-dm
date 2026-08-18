#pragma once

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

private:
    HFONT MenuFont() const;

    HMENU bar_ = nullptr;
    HBRUSH background_ = nullptr;
    mutable HFONT font_ = nullptr;
    COLORREF surface_ = 0;
    COLORREF text_ = 0;
    COLORREF highlight_ = 0;
    bool dark_ = false;
};
