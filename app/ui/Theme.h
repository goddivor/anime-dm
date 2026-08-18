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

private:
    void Refresh();

    ThemeMode mode_ = ThemeMode::System;
    ThemeColors colors_ = {};
    HBRUSH window_ = nullptr;
    HBRUSH surface_ = nullptr;
};
