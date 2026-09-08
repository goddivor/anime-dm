#pragma once

#include <memory>
#include <vector>

#include <windows.h>

#include "core/AddonStore.h"
#include "core/Download.h"
#include "core/Downloader.h"
#include "core/Http.h"
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
    void OnDestroy();
    void OnCommand(int commandId);
    void ShowSoon(int commandId);
    void OnAddDownload();
    void OnDownloadEvent(std::unique_ptr<DownloadEvent> event);
    void ApplyTheme();
    void Retranslate();
    LRESULT OnToolbarCustomDraw(NMTBCUSTOMDRAW* draw);
    LRESULT OnListCustomDraw(NMLVCUSTOMDRAW* draw);
    bool DrawProgressCell(NMLVCUSTOMDRAW* draw);
    void OnContextMenu(HWND target, int x, int y);
    void Relayout();
    RECT SplitterRect() const;
    bool OnSetCursor();
    void OnLeftButtonDown(int x);
    void OnMouseMove(int x);
    void OnLeftButtonUp();
    void ApplyUiFont();

    // --- the queue ---
    DownloadItem* Find(uint64_t id);
    DownloadTask TaskOf(const DownloadItem& item) const;
    void Refresh(const DownloadItem& item);
    void Persist();
    void UpdateActions();
    void StartItem(DownloadItem& item, bool fresh);
    void ResumeSelected();
    void StopSelected();
    void RedownloadSelected();
    void RemoveSelected();
    void StopAll();
    void DeleteAll();
    void RemoveCompleted();
    void OpenSelected(bool folder);
    void ShowNotice(const wchar_t* message);

    HWND hwnd_ = nullptr;
    HFONT uiFont_ = nullptr;
    HACCEL accel_ = nullptr;
    Http http_;
    AddonStore store_{http_};
    Downloader downloader_{http_, store_};
    std::vector<DownloadItem> items_;
    uint64_t nextId_ = 1;
    MenuBar menuBar_;
    Toolbar toolbar_;
    Sidebar sidebar_;
    DownloadsView downloads_;
    int themeCommand_ = 0;
    int languageCommand_ = 0;
    int sidebarWidth_ = 230;
    bool sidebarVisible_ = true;
    bool draggingSplitter_ = false;
};
