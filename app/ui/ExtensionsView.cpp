#include "ui/ExtensionsView.h"

#include <commctrl.h>

#include "ui/Strings.h"

namespace {
struct Column {
    StringId title;
    int width;
};

constexpr Column kColumns[] = {
    {STR_EXT_NAME, 260},
    {STR_EXT_LANG, 90},
    {STR_EXT_VERSION, 100},
    {STR_EXT_STATUS, 140},
};
}  // namespace

// Creates the extensions ListView, initially hidden behind the downloads view.
bool ExtensionsView::Create(HWND parent, HINSTANCE instance) {
    hwnd_ = CreateWindowExW(
        0, WC_LISTVIEWW, L"",
        WS_CHILD | LVS_REPORT | LVS_SHOWSELALWAYS,
        0, 0, 0, 0,
        parent, nullptr, instance, nullptr);
    if (hwnd_ == nullptr) {
        return false;
    }

    ListView_SetExtendedListViewStyle(
        hwnd_, LVS_EX_FULLROWSELECT | LVS_EX_GRIDLINES | LVS_EX_DOUBLEBUFFER);
    AddColumns();
    return true;
}

// Inserts the report columns in declaration order.
void ExtensionsView::AddColumns() {
    LVCOLUMNW col = {};
    col.mask = LVCF_TEXT | LVCF_WIDTH | LVCF_SUBITEM;
    int index = 0;
    for (const Column& column : kColumns) {
        col.iSubItem = index;
        col.cx = column.width;
        col.pszText = const_cast<wchar_t*>(Str(column.title));
        ListView_InsertColumn(hwnd_, index, &col);
        ++index;
    }
}

// Refreshes the column captions after a language change.
void ExtensionsView::Retranslate() {
    LVCOLUMNW col = {};
    col.mask = LVCF_TEXT;
    int index = 0;
    for (const Column& column : kColumns) {
        col.pszText = const_cast<wchar_t*>(Str(column.title));
        ListView_SetColumn(hwnd_, index, &col);
        ++index;
    }
}

// Repositions and resizes the ListView within its parent client area.
void ExtensionsView::SetBounds(int x, int y, int width, int height) {
    MoveWindow(hwnd_, x, y, width, height, TRUE);
}

// Shows or hides the whole view when switching screens.
void ExtensionsView::SetVisible(bool visible) {
    ShowWindow(hwnd_, visible ? SW_SHOW : SW_HIDE);
}
