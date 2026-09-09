#pragma once

#include <windows.h>

// Colour schemes the shell can switch between at runtime.
enum class ThemeMode {
    Dark,
    Light,
    System,
};

// Palette resolved from the active mode.
struct ThemeColors {
    COLORREF window;
    COLORREF surface;
    COLORREF text;
    COLORREF line;
    COLORREF accent;      // what a chosen item is filled with
    COLORREF accentText;  // what is written on top of it
    COLORREF hover;       // what the pointer lights up
    COLORREF muted;       // what is greyed out
    COLORREF ok;          // what succeeded
    COLORREF bad;         // what failed
    COLORREF header;      // the caption row of a list, a shade under the rows
    COLORREF frame;       // the outline of a list, a shade over the rules
    bool dark;
};

// Resolves the active colour scheme and pushes it onto the common controls.
class Theme {
public:
    ~Theme();

    void SetMode(ThemeMode mode);
    ThemeMode Mode() const { return mode_; }
    bool IsDark() const { return colors_.dark; }
    const ThemeColors& Colors() const { return colors_; }
    HBRUSH WindowBrush() const { return window_; }
    HBRUSH SurfaceBrush() const { return surface_; }

    void ApplyToFrame(HWND window) const;
    void ApplyToList(HWND list) const;
    void ApplyToTree(HWND tree) const;
    void ApplyToDialog(HWND dialog) const;
    INT_PTR ControlColor(HDC dc, bool input) const;

private:
    void Refresh();


    ThemeMode mode_ = ThemeMode::System;
    ThemeColors colors_ = {};
    HBRUSH window_ = nullptr;
    HBRUSH surface_ = nullptr;
};

// The palette every window of the application shares.
Theme& ActiveTheme();

// Answers the colour messages common to every dialog; true when handled.
bool ThemeDialogMessage(UINT msg, WPARAM wParam, INT_PTR* result);
