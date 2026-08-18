#include "ui/Sidebar.h"

#include <commctrl.h>
#include <windowsx.h>

#include "ui/Commands.h"
#include "ui/IconFactory.h"

namespace {
constexpr wchar_t kHeaderClass[] = L"AnimeDmCategoriesHeader";
constexpr wchar_t kHeaderText[] = L"Catégories";
constexpr int kHeaderHeight = 24;
constexpr int kCloseBoxSize = 20;

// Returns the square that holds the header close button.
RECT CloseBoxRect(HWND header) {
    RECT client = {};
    GetClientRect(header, &client);
    int top = (client.bottom - kCloseBoxSize) / 2;
    RECT box = {client.right - kCloseBoxSize - 4, top, client.right - 4, top + kCloseBoxSize};
    return box;
}

// Paints the caption bar: label on the left, close cross on the right.
void PaintHeader(HWND header) {
    PAINTSTRUCT paint = {};
    HDC dc = BeginPaint(header, &paint);

    RECT client = {};
    GetClientRect(header, &client);
    FillRect(dc, &client, GetSysColorBrush(COLOR_BTNFACE));

    HPEN pen = CreatePen(PS_SOLID, 1, GetSysColor(COLOR_BTNSHADOW));
    HPEN oldPen = static_cast<HPEN>(SelectObject(dc, pen));
    MoveToEx(dc, client.left, client.bottom - 1, nullptr);
    LineTo(dc, client.right, client.bottom - 1);

    auto font = reinterpret_cast<HFONT>(GetWindowLongPtrW(header, GWLP_USERDATA));
    HFONT oldFont = nullptr;
    if (font != nullptr) {
        oldFont = static_cast<HFONT>(SelectObject(dc, font));
    }
    SetBkMode(dc, TRANSPARENT);
    SetTextColor(dc, GetSysColor(COLOR_BTNTEXT));
    RECT label = client;
    label.left += 8;
    DrawTextW(dc, kHeaderText, -1, &label, DT_LEFT | DT_VCENTER | DT_SINGLELINE);
    if (oldFont != nullptr) {
        SelectObject(dc, oldFont);
    }

    RECT box = CloseBoxRect(header);
    HPEN cross = CreatePen(PS_SOLID, 1, GetSysColor(COLOR_BTNTEXT));
    SelectObject(dc, cross);
    MoveToEx(dc, box.left + 6, box.top + 6, nullptr);
    LineTo(dc, box.right - 6, box.bottom - 6);
    MoveToEx(dc, box.right - 7, box.top + 6, nullptr);
    LineTo(dc, box.left + 5, box.bottom - 6);

    SelectObject(dc, oldPen);
    DeleteObject(cross);
    DeleteObject(pen);
    EndPaint(header, &paint);
}

// Window procedure for the caption bar; the close cross hides the panel.
LRESULT CALLBACK HeaderProc(HWND hwnd, UINT msg, WPARAM wParam, LPARAM lParam) {
    switch (msg) {
    case WM_PAINT:
        PaintHeader(hwnd);
        return 0;
    case WM_SETFONT:
        SetWindowLongPtrW(hwnd, GWLP_USERDATA, static_cast<LONG_PTR>(wParam));
        InvalidateRect(hwnd, nullptr, TRUE);
        return 0;
    case WM_GETFONT:
        return GetWindowLongPtrW(hwnd, GWLP_USERDATA);
    case WM_LBUTTONDOWN: {
        POINT point = {GET_X_LPARAM(lParam), GET_Y_LPARAM(lParam)};
        RECT box = CloseBoxRect(hwnd);
        if (PtInRect(&box, point)) {
            SendMessageW(GetParent(hwnd), WM_COMMAND, ID_VIEW_CATEGORIES, 0);
        }
        return 0;
    }
    default:
        return DefWindowProcW(hwnd, msg, wParam, lParam);
    }
}

// Registers the caption bar class once per process.
void EnsureHeaderClass(HINSTANCE instance) {
    WNDCLASSEXW existing = {};
    existing.cbSize = sizeof(existing);
    if (GetClassInfoExW(instance, kHeaderClass, &existing)) {
        return;
    }

    WNDCLASSEXW wc = {};
    wc.cbSize = sizeof(wc);
    wc.lpfnWndProc = HeaderProc;
    wc.hInstance = instance;
    wc.lpszClassName = kHeaderClass;
    wc.hCursor = LoadCursorW(nullptr, IDC_ARROW);
    RegisterClassExW(&wc);
}
}  // namespace

// Releases the image list backing the category glyphs.
Sidebar::~Sidebar() {
    if (icons_ != nullptr) {
        ImageList_Destroy(icons_);
        icons_ = nullptr;
    }
}

// Creates the caption bar and the TreeView, then seeds the default categories.
bool Sidebar::Create(HWND parent, HINSTANCE instance) {
    EnsureHeaderClass(instance);
    header_ = CreateWindowExW(
        0, kHeaderClass, nullptr, WS_CHILD | WS_VISIBLE,
        0, 0, 0, 0, parent, nullptr, instance, nullptr);

    tree_ = CreateWindowExW(
        WS_EX_CLIENTEDGE, WC_TREEVIEWW, L"",
        WS_CHILD | WS_VISIBLE | TVS_HASLINES | TVS_HASBUTTONS | TVS_LINESATROOT | TVS_SHOWSELALWAYS,
        0, 0, 0, 0, parent, nullptr, instance, nullptr);
    if (tree_ == nullptr) {
        return false;
    }

    icons_ = CreateCategoryImageList();
    SendMessageW(tree_, TVM_SETIMAGELIST, TVSIL_NORMAL, reinterpret_cast<LPARAM>(icons_));

    Populate();
    return true;
}

// Inserts the fixed category roots; the download nodes fill in at runtime.
void Sidebar::Populate() {
    HTREEITEM all = Insert(TVI_ROOT, L"Tous les téléchargements", CAT_FOLDER_OPEN, CAT_FOLDER_OPEN);
    Insert(all, L"Animés", CAT_VIDEO, CAT_VIDEO);
    Insert(all, L"Films", CAT_VIDEO, CAT_VIDEO);
    Insert(all, L"Autres", CAT_FOLDER, CAT_FOLDER);

    Insert(TVI_ROOT, L"Incomplets", CAT_PENDING, CAT_PENDING);
    Insert(TVI_ROOT, L"Terminés", CAT_DONE, CAT_DONE);

    HTREEITEM queues = Insert(TVI_ROOT, L"Files d'attente", CAT_QUEUE, CAT_QUEUE);
    Insert(queues, L"File principale", CAT_QUEUE, CAT_QUEUE);
    Insert(queues, L"Planificateur", CAT_PENDING, CAT_PENDING);

    SendMessageW(tree_, TVM_EXPAND, TVE_EXPAND, reinterpret_cast<LPARAM>(all));
    SendMessageW(tree_, TVM_EXPAND, TVE_EXPAND, reinterpret_cast<LPARAM>(queues));
    SendMessageW(tree_, TVM_SELECTITEM, TVGN_CARET, reinterpret_cast<LPARAM>(all));
}

// Appends a labelled item under the given parent and returns its handle.
HTREEITEM Sidebar::Insert(HTREEITEM parent, const wchar_t* text, int icon, int selectedIcon) {
    TVINSERTSTRUCTW insert = {};
    insert.hParent = parent;
    insert.hInsertAfter = TVI_LAST;
    insert.item.mask = TVIF_TEXT | TVIF_IMAGE | TVIF_SELECTEDIMAGE;
    insert.item.pszText = const_cast<wchar_t*>(text);
    insert.item.iImage = icon;
    insert.item.iSelectedImage = selectedIcon;
    return reinterpret_cast<HTREEITEM>(
        SendMessageW(tree_, TVM_INSERTITEMW, 0, reinterpret_cast<LPARAM>(&insert)));
}

// Repositions the caption bar and the TreeView inside the panel bounds.
void Sidebar::SetBounds(int x, int y, int width, int height) {
    int headerHeight = height < kHeaderHeight ? height : kHeaderHeight;
    MoveWindow(header_, x, y, width, headerHeight, TRUE);
    MoveWindow(tree_, x, y + headerHeight, width, height - headerHeight, TRUE);
}

// Shows or hides the whole panel, caption bar included.
void Sidebar::SetVisible(bool visible) {
    ShowWindow(header_, visible ? SW_SHOW : SW_HIDE);
    ShowWindow(tree_, visible ? SW_SHOW : SW_HIDE);
}
