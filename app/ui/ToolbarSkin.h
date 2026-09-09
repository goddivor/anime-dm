#pragma once

#include <string>
#include <vector>

#include <windows.h>
#include <commctrl.h>

// A toolbar skin in the format of Internet Download Manager: a `.tbi` text
// descriptor naming 24-bit BMP strips of twelve buttons, one strip per size
// (large, small, high density) and state (normal, hot, disabled), the corner
// pixel of a strip standing for its transparent colour. Any skin made for IDM
// drops into `%APPDATA%\anime-dm\toolbar` and shows up in the View menu.
struct ToolbarSkin {
    std::wstring name;
    std::wstring large;
    std::wstring largeHot;
    std::wstring largeDisabled;
    std::wstring hdpi;
    std::wstring hdpiHot;
};

// The image lists a skin yields for this toolbar, in its own cell size. The
// caller destroys the lists.
struct ToolbarStrips {
    HIMAGELIST normal = nullptr;
    HIMAGELIST hot = nullptr;
    HIMAGELIST disabled = nullptr;
    int width = 0;
    int height = 0;
};

namespace skins {

// The skins found next to the executable and in the user's data folder, by
// name.
std::vector<ToolbarSkin> Discover();

// Reads the strips of a skin into image lists laid out for this toolbar; the
// buttons IDM does not have get a glyph of the icon font in `glyph`.
bool Load(const ToolbarSkin& skin, double scale, COLORREF glyph, COLORREF glyphMuted,
          ToolbarStrips* strips);

void Release(ToolbarStrips* strips);

}  // namespace skins
