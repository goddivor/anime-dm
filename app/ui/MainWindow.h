#pragma once

#include <windows.h>

#include "ui/DownloadsView.h"
#include "ui/MenuBar.h"
#include "ui/Toolbar.h"

// Top-level application window backed by a registered Win32 window class.
class MainWindow {
public:
    bool Create(HINSTANCE instance, const wchar_t* title);
    void Show(int cmdShow);
    HWND Handle() const { return hwnd_; }

private:
    static LRESULT CALLBACK WndProcTrampoline(HWND hwnd, UINT msg, WPARAM wParam, LPARAM lParam);
    LRESULT HandleMessage(UINT msg, WPARAM wParam, LPARAM lParam);
    void OnCreate();
    void OnSize(int width, int height);
    void OnCommand(int commandId);
    void ApplyUiFont();

    HWND hwnd_ = nullptr;
    HWND statusBar_ = nullptr;
    HFONT uiFont_ = nullptr;
    MenuBar menuBar_;
    Toolbar toolbar_;
    DownloadsView downloads_;
};
