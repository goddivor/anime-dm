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

    // Plays the sprite of the button under the pointer forward, and the
    // sprites of the others back to their first frame.
    void OnHotItem(const NMTBHOTITEM* hot);

    // Moves every sprite one frame toward where it is heading.
    void StepSprite();

    int Height() const;
    HWND Handle() const { return hwnd_; }

private:
    void RebuildImages(const Theme& theme);
    void RebuildButtons();
    void DropLists();
    // A button whose picture comes from a sprite rather than the glyphs.
    struct ButtonSprite {
        int command = 0;
        int icon = 0;
        Sprite sprite;
        std::vector<HBITMAP> frames;
        HBITMAP disabled = nullptr;
        int frame = 0;
        int target = 0;
    };

    void LoadSprites();
    void RenderSprites(int width, int height);
    void ShowSpriteFrame(const ButtonSprite& button);
    void DropSpriteFrames();

    HWND hwnd_ = nullptr;
    HIMAGELIST imageList_ = nullptr;
    HIMAGELIST disabledList_ = nullptr;
    ToolbarStrips strips_;
    std::unique_ptr<ToolbarSkin> skin_;
    std::vector<std::unique_ptr<ButtonSprite>> sprites_;
};
