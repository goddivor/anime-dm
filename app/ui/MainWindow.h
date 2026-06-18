#pragma once

#include <windows.h>

// Top-level application window backed by a registered Win32 window class.
class MainWindow {
public:
    bool Create(HINSTANCE instance, const wchar_t* title);
    void Show(int cmdShow);
    HWND Handle() const { return hwnd_; }

private:
    static LRESULT CALLBACK WndProcTrampoline(HWND hwnd, UINT msg, WPARAM wParam, LPARAM lParam);
    LRESULT HandleMessage(UINT msg, WPARAM wParam, LPARAM lParam);

    HWND hwnd_ = nullptr;
};
