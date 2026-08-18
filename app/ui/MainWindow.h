#pragma once

#include <windows.h>

#include "ui/DownloadsView.h"
#include "ui/MenuBar.h"
#include "ui/Sidebar.h"
#include "ui/Theme.h"
#include "ui/Toolbar.h"

// Top-level application window backed by a registered Win32 window class.
class MainWindow {
public:
    bool Create(HINSTANCE instance, const wchar_t* title);
    void Show(int cmdShow);
    HWND Handle() const { return hwnd_; }
    HACCEL Accelerator() const { return accel_; }

private:
    static LRESULT CALLBACK WndProcTrampoline(HWND hwnd, UINT msg, WPARAM wParam, LPARAM lParam);
    LRESULT HandleMessage(UINT msg, WPARAM wParam, LPARAM lParam);
    void OnCreate();
    void OnCommand(int commandId);
    void ShowSoon(int commandId);
    void ApplyTheme();
    void Retranslate();
    LRESULT OnToolbarCustomDraw(NMTBCUSTOMDRAW* draw);
    LRESULT OnListCustomDraw(NMLVCUSTOMDRAW* draw);
    void OnContextMenu(HWND target, int x, int y);
    void Relayout();
    RECT SplitterRect() const;
    bool OnSetCursor();
    void OnLeftButtonDown(int x);
    void OnMouseMove(int x);
    void OnLeftButtonUp();
    void ApplyUiFont();

    HWND hwnd_ = nullptr;
    HFONT uiFont_ = nullptr;
    HACCEL accel_ = nullptr;
    MenuBar menuBar_;
    Toolbar toolbar_;
    Sidebar sidebar_;
    DownloadsView downloads_;
    Theme theme_;
    int themeCommand_ = 0;
    int languageCommand_ = 0;
    int sidebarWidth_ = 230;
    bool sidebarVisible_ = true;
    bool draggingSplitter_ = false;
};
