#pragma once

#include <windows.h>

#include <vector>

#include "ui/Strings.h"

// A row of tabs drawn by hand over a framed body, for the dialogs that hold
// several pages: the common tab control never takes the dark palette. The
// dialog creates its pages inside Body() and shows the one Page() names.
class TabStrip {
public:
    // Measures the tabs from their captions; the rectangles are dialog units.
    void Init(HWND dialog, const RECT& stripUnits, const RECT& bodyUnits,
              std::vector<StringId> titles);

    const RECT& Body() const { return body_; }
    int Page() const { return page_; }
    int Count() const { return static_cast<int>(tabs_.size()); }

    // Chooses a tab and repaints the strip.
    void SetPage(HWND dialog, int page);

    // Paints strip and frame inside a WM_PAINT of the dialog.
    void Paint(HWND dialog) const;

    // The tab under a client point, or -1.
    int HitTest(POINT point) const;

private:
    std::vector<StringId> titles_;
    std::vector<RECT> tabs_;
    RECT strip_ = {};
    RECT body_ = {};
    int page_ = 0;
};
