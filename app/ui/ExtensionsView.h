#pragma once

#include <windows.h>

// Owns the report-mode ListView that lists installed/available extensions.
class ExtensionsView {
public:
    bool Create(HWND parent, HINSTANCE instance);
    void SetBounds(int x, int y, int width, int height);
    void SetVisible(bool visible);
    HWND Handle() const { return hwnd_; }

private:
    void AddColumns();

    HWND hwnd_ = nullptr;
};
