#pragma once

#include <memory>

#include <windows.h>
#include <commctrl.h>

#include "ui/ToolbarSkin.h"

class Theme;

// Owns the top toolbar control with the primary download actions. Its
// pictures come from the system icon font by default, or from a toolbar skin
// in the format of IDM when the user picks one.
class Toolbar {
public:
    ~Toolbar();

    bool Create(HWND parent, HINSTANCE instance);
    void Resize();
    void Retranslate();
    void ApplyTheme(const Theme& theme);
    void Enable(int command, bool enabled);

    // Dresses the buttons with a skin, or with the icon font when null.
    void SetSkin(const ToolbarSkin* skin, const Theme& theme);

    int Height() const;
    HWND Handle() const { return hwnd_; }

private:
    void RebuildImages(const Theme& theme);
    void RebuildButtons();
    void DropLists();

    HWND hwnd_ = nullptr;
    HIMAGELIST imageList_ = nullptr;
    HIMAGELIST disabledList_ = nullptr;
    ToolbarStrips strips_;
    std::unique_ptr<ToolbarSkin> skin_;
};
