#include "ui/Sidebar.h"

#include <commctrl.h>

// Creates the TreeView child and seeds it with the default categories.
bool Sidebar::Create(HWND parent, HINSTANCE instance) {
    hwnd_ = CreateWindowExW(
        WS_EX_CLIENTEDGE, WC_TREEVIEWW, L"",
        WS_CHILD | WS_VISIBLE | TVS_HASLINES | TVS_HASBUTTONS | TVS_LINESATROOT | TVS_SHOWSELALWAYS,
        0, 0, 0, 0, parent, nullptr, instance, nullptr);
    if (hwnd_ == nullptr) {
        return false;
    }

    Populate();
    return true;
}

// Inserts the fixed category roots; the "Animés" node fills in at runtime.
void Sidebar::Populate() {
    Insert(TVI_ROOT, L"Tous les téléchargements");
    Insert(TVI_ROOT, L"File principale");
    Insert(TVI_ROOT, L"Planificateur");
    Insert(TVI_ROOT, L"Animés");
}

// Appends a labelled item under the given parent and returns its handle.
HTREEITEM Sidebar::Insert(HTREEITEM parent, const wchar_t* text) {
    TVINSERTSTRUCTW insert = {};
    insert.hParent = parent;
    insert.hInsertAfter = TVI_LAST;
    insert.item.mask = TVIF_TEXT;
    insert.item.pszText = const_cast<wchar_t*>(text);
    return reinterpret_cast<HTREEITEM>(
        SendMessageW(hwnd_, TVM_INSERTITEMW, 0, reinterpret_cast<LPARAM>(&insert)));
}

// Repositions and resizes the TreeView within its parent client area.
void Sidebar::SetBounds(int x, int y, int width, int height) {
    MoveWindow(hwnd_, x, y, width, height, TRUE);
}
