#include "ui/TabStrip.h"

#include <windowsx.h>

#include "ui/Theme.h"

namespace {

constexpr int kTabPaddingUnits = 10;

HFONT FontOf(HWND window) {
    return reinterpret_cast<HFONT>(SendMessageW(window, WM_GETFONT, 0, 0));
}

RECT ToPixels(HWND dialog, RECT units) {
    MapDialogRect(dialog, &units);
    return units;
}

}  // namespace

// Lays the tabs side by side along the strip, each as wide as its caption
// and a margin either side.
void TabStrip::Init(HWND dialog, const RECT& stripUnits, const RECT& bodyUnits,
                    std::vector<StringId> titles) {
    titles_ = std::move(titles);
    strip_ = ToPixels(dialog, stripUnits);
    body_ = ToPixels(dialog, bodyUnits);
    RECT padding = ToPixels(dialog, {0, 0, kTabPaddingUnits, 0});

    HDC dc = GetDC(dialog);
    HFONT old = SelectFont(dc, FontOf(dialog));
    int x = strip_.left;
    tabs_.clear();
    for (StringId title : titles_) {
        SIZE size = {};
        const wchar_t* text = Str(title);
        GetTextExtentPoint32W(dc, text, lstrlenW(text), &size);
        RECT tab = {x, strip_.top, x + size.cx + 2 * padding.right, strip_.bottom};
        tabs_.push_back(tab);
        x = tab.right - 1;
    }
    SelectFont(dc, old);
    ReleaseDC(dialog, dc);
}

// Chooses a tab and repaints the strip down to the frame.
void TabStrip::SetPage(HWND dialog, int page) {
    page_ = page;
    RECT strip = strip_;
    strip.bottom = body_.top + 1;
    InvalidateRect(dialog, &strip, TRUE);
}

// Draws each tab outlined, the chosen one filled like the page and open onto
// it, and the body framed underneath.
void TabStrip::Paint(HWND dialog) const {
    PAINTSTRUCT ps = {};
    HDC dc = BeginPaint(dialog, &ps);
    const ThemeColors& colors = ActiveTheme().Colors();
    HFONT oldFont = SelectFont(dc, FontOf(dialog));
    HPEN pen = CreatePen(PS_SOLID, 1, colors.line);
    HPEN oldPen = SelectPen(dc, pen);
    HBRUSH page = CreateSolidBrush(colors.window);
    HBRUSH rest = CreateSolidBrush(colors.surface);

    SelectBrush(dc, page);
    Rectangle(dc, body_.left, body_.top, body_.right, body_.bottom);

    SetBkMode(dc, TRANSPARENT);
    for (size_t i = 0; i < tabs_.size(); ++i) {
        bool chosen = static_cast<int>(i) == page_;
        RECT tab = tabs_[i];
        if (!chosen) {
            tab.top += 2;
        }
        SelectBrush(dc, chosen ? page : rest);
        Rectangle(dc, tab.left, tab.top, tab.right, tab.bottom + 1);
        if (chosen) {
            HPEN erase = CreatePen(PS_SOLID, 1, colors.window);
            HPEN kept = SelectPen(dc, erase);
            MoveToEx(dc, tab.left + 1, tab.bottom, nullptr);
            LineTo(dc, tab.right - 1, tab.bottom);
            SelectPen(dc, kept);
            DeleteObject(erase);
        }
        SetTextColor(dc, chosen ? colors.text : colors.muted);
        DrawTextW(dc, Str(titles_[i]), -1, &tab,
                  DT_CENTER | DT_VCENTER | DT_SINGLELINE | DT_NOPREFIX);
    }

    SelectPen(dc, oldPen);
    SelectFont(dc, oldFont);
    DeleteObject(pen);
    DeleteObject(page);
    DeleteObject(rest);
    EndPaint(dialog, &ps);
}

// The tab under a point, or -1.
int TabStrip::HitTest(POINT point) const {
    for (size_t i = 0; i < tabs_.size(); ++i) {
        if (PtInRect(&tabs_[i], point)) {
            return static_cast<int>(i);
        }
    }
    return -1;
}
