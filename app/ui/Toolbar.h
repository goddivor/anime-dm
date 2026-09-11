#pragma once

#include <memory>
#include <vector>

#include <windows.h>
#include <commctrl.h>

#include "ui/SpriteIcon.h"
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

    // Plays the sprite of the Addons button forward when the pointer comes
    // onto it, backward when it leaves.
    void OnHotItem(const NMTBHOTITEM* hot);

    // Moves the sprite one frame toward where it is heading.
    void StepSprite();

    int Height() const;
    HWND Handle() const { return hwnd_; }

private:
    void RebuildImages(const Theme& theme);
    void RebuildButtons();
    void DropLists();
    void RenderSprite(int width, int height);
    void ShowSpriteFrame();
    void DropSpriteFrames();

    HWND hwnd_ = nullptr;
    HIMAGELIST imageList_ = nullptr;
    HIMAGELIST disabledList_ = nullptr;
    ToolbarStrips strips_;
    std::unique_ptr<ToolbarSkin> skin_;
    Sprite sprite_;
    std::vector<HBITMAP> spriteFrames_;
    int spriteFrame_ = 0;
    int spriteTarget_ = 0;
};
