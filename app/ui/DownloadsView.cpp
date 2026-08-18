#include "ui/DownloadsView.h"

#include <commctrl.h>

#include "ui/Strings.h"

namespace {
struct Column {
    StringId title;
    int width;
};

constexpr Column kColumns[] = {
    {STR_COL_FILENAME, 320},
    {STR_COL_SIZE, 90},
    {STR_COL_STATUS, 120},
    {STR_COL_TIME_LEFT, 110},
    {STR_COL_SPEED, 150},
    {STR_COL_LAST_TRY, 150},
    {STR_COL_ADDED, 150},
};
}  // namespace

// Creates the ListView child and configures its columns and extended styles.
bool DownloadsView::Create(HWND parent, HINSTANCE instance) {
    hwnd_ = CreateWindowExW(
        0, WC_LISTVIEWW, L"",
        WS_CHILD | WS_VISIBLE | LVS_REPORT | LVS_SHOWSELALWAYS,
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
void DownloadsView::AddColumns() {
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
void DownloadsView::Retranslate() {
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
void DownloadsView::SetBounds(int x, int y, int width, int height) {
    MoveWindow(hwnd_, x, y, width, height, TRUE);
}
