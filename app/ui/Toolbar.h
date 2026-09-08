#pragma once

#include <windows.h>
#include <commctrl.h>

class Theme;

// Owns the top toolbar control with the primary download actions.
class Toolbar {
public:
    ~Toolbar();

    bool Create(HWND parent, HINSTANCE instance);
    void Resize();
    void Retranslate();
    void ApplyTheme(const Theme& theme);
    void Enable(int command, bool enabled);
    int Height() const;
    HWND Handle() const { return hwnd_; }

private:
    HWND hwnd_ = nullptr;
    HIMAGELIST imageList_ = nullptr;
    HIMAGELIST disabledList_ = nullptr;
};
