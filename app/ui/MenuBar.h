#pragma once

#include <windows.h>

// Builds the application menu bar and keeps its checkable items in sync.
class MenuBar {
public:
    void AttachTo(HWND window);
    void Rebuild(HWND window);
    void SetCategoriesChecked(bool checked);
    void SetTheme(int commandId);
    void SetLanguage(int commandId);

private:
    HMENU bar_ = nullptr;
};
