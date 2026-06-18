#include "ui/DownloadsView.h"

#include <commctrl.h>

namespace {
struct Column {
    const wchar_t* title;
    int width;
};

constexpr Column kColumns[] = {
    {L"Nom du fichier", 360},
    {L"Taille", 100},
    {L"Progression", 120},
    {L"Statut", 110},
    {L"Vitesse", 100},
    {L"Temps restant", 110},
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
        col.pszText = const_cast<wchar_t*>(column.title);
        ListView_InsertColumn(hwnd_, index, &col);
        ++index;
    }
}

// Repositions and resizes the ListView within its parent client area.
void DownloadsView::SetBounds(int x, int y, int width, int height) {
    MoveWindow(hwnd_, x, y, width, height, TRUE);
}
