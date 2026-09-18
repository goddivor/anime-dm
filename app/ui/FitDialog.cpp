#include "ui/FitDialog.h"

#include <string>

namespace {

struct Mover {
    HWND dialog;
    int from;
    int by;
};

// Moves one control down when it lies under the label.
BOOL CALLBACK MoveChild(HWND child, LPARAM data) {
    const Mover& mover = *reinterpret_cast<const Mover*>(data);
    RECT rect = {};
    GetWindowRect(child, &rect);
    MapWindowPoints(nullptr, mover.dialog, reinterpret_cast<POINT*>(&rect), 2);
    if (rect.top >= mover.from) {
        SetWindowPos(child, nullptr, rect.left, rect.top + mover.by, 0, 0,
                     SWP_NOSIZE | SWP_NOZORDER | SWP_NOACTIVATE);
    }
    return TRUE;
}

}  // namespace

// Grows the label to its text and shifts the rest of the dialog after it.
void FitDialogToText(HWND dialog, int textId) {
    HWND label = GetDlgItem(dialog, textId);
    RECT bounds = {};
    GetWindowRect(label, &bounds);
    MapWindowPoints(nullptr, dialog, reinterpret_cast<POINT*>(&bounds), 2);

    int length = GetWindowTextLengthW(label);
    std::wstring text(static_cast<size_t>(length) + 1, L'\0');
    GetWindowTextW(label, text.data(), length + 1);
    text.resize(static_cast<size_t>(length));

    HDC dc = GetDC(label);
    HFONT font = reinterpret_cast<HFONT>(SendMessageW(label, WM_GETFONT, 0, 0));
    HFONT previous = static_cast<HFONT>(SelectObject(dc, font));
    RECT measure = {0, 0, bounds.right - bounds.left, 0};
    DrawTextW(dc, text.c_str(), -1, &measure, DT_CALCRECT | DT_WORDBREAK | DT_NOPREFIX);
    SelectObject(dc, previous);
    ReleaseDC(label, dc);

    int wanted = measure.bottom;
    int grow = wanted - (bounds.bottom - bounds.top);
    if (grow == 0) {
        return;
    }
    SetWindowPos(label, nullptr, 0, 0, bounds.right - bounds.left, wanted,
                 SWP_NOMOVE | SWP_NOZORDER | SWP_NOACTIVATE);

    Mover mover = {dialog, bounds.bottom + 1, grow};
    EnumChildWindows(dialog, MoveChild, reinterpret_cast<LPARAM>(&mover));

    RECT window = {};
    GetWindowRect(dialog, &window);
    SetWindowPos(dialog, nullptr, 0, 0, window.right - window.left,
                 window.bottom - window.top + grow, SWP_NOMOVE | SWP_NOZORDER | SWP_NOACTIVATE);
}

// Grows the button to the left when its caption, with a margin either side,
// is wider than it.
void FitButtonToCaption(HWND dialog, int buttonId) {
    HWND button = GetDlgItem(dialog, buttonId);
    int length = GetWindowTextLengthW(button);
    std::wstring caption(static_cast<size_t>(length) + 1, L'\0');
    GetWindowTextW(button, caption.data(), length + 1);

    HDC dc = GetDC(button);
    HFONT font = reinterpret_cast<HFONT>(SendMessageW(button, WM_GETFONT, 0, 0));
    HFONT previous = static_cast<HFONT>(SelectObject(dc, font));
    SIZE size = {};
    GetTextExtentPoint32W(dc, caption.c_str(), length, &size);
    SelectObject(dc, previous);
    ReleaseDC(button, dc);

    RECT margin = {0, 0, 10, 0};
    MapDialogRect(dialog, &margin);
    RECT rect = {};
    GetWindowRect(button, &rect);
    MapWindowPoints(nullptr, dialog, reinterpret_cast<POINT*>(&rect), 2);
    int wanted = size.cx + 2 * margin.right;
    int width = rect.right - rect.left;
    if (wanted <= width) {
        return;
    }
    SetWindowPos(button, nullptr, rect.right - wanted, rect.top, wanted, rect.bottom - rect.top,
                 SWP_NOZORDER | SWP_NOACTIVATE);
}
