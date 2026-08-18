#include "ui/Sidebar.h"

#include <commctrl.h>
#include <windowsx.h>

#include "ui/Commands.h"
#include "ui/IconFactory.h"
#include "ui/Strings.h"
#include "ui/Theme.h"

namespace {
constexpr wchar_t kHeaderClass[] = L"AnimeDmCategoriesHeader";
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
    auto* state = reinterpret_cast<SidebarHeaderState*>(GetWindowLongPtrW(header, GWLP_USERDATA));
    if (state == nullptr) {
        return;
    }

    PAINTSTRUCT paint = {};
    HDC dc = BeginPaint(header, &paint);

    RECT client = {};
    GetClientRect(header, &client);
    HBRUSH background = CreateSolidBrush(state->surface);
    FillRect(dc, &client, background);
    DeleteObject(background);

    HPEN pen = CreatePen(PS_SOLID, 1, state->line);
    HPEN oldPen = static_cast<HPEN>(SelectObject(dc, pen));
    MoveToEx(dc, client.left, client.bottom - 1, nullptr);
    LineTo(dc, client.right, client.bottom - 1);

    HFONT oldFont = nullptr;
    if (state->font != nullptr) {
        oldFont = static_cast<HFONT>(SelectObject(dc, state->font));
    }
    SetBkMode(dc, TRANSPARENT);
    SetTextColor(dc, state->text);
    RECT label = client;
    label.left += 8;
    DrawTextW(dc, Str(STR_SIDEBAR_TITLE), -1, &label, DT_LEFT | DT_VCENTER | DT_SINGLELINE);
    if (oldFont != nullptr) {
        SelectObject(dc, oldFont);
    }

    RECT box = CloseBoxRect(header);
    HPEN cross = CreatePen(PS_SOLID, 1, state->text);
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
    auto* state = reinterpret_cast<SidebarHeaderState*>(GetWindowLongPtrW(hwnd, GWLP_USERDATA));

    switch (msg) {
    case WM_PAINT:
        PaintHeader(hwnd);
        return 0;
    case WM_ERASEBKGND:
        return 1;
    case WM_SETFONT:
        if (state != nullptr) {
            state->font = reinterpret_cast<HFONT>(wParam);
        }
        InvalidateRect(hwnd, nullptr, TRUE);
        return 0;
    case WM_GETFONT:
        return state != nullptr ? reinterpret_cast<LRESULT>(state->font) : 0;
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
    // The close cross is laid out from the right edge, so the whole bar has to
    // repaint when the splitter changes its width.
    wc.style = CS_HREDRAW | CS_VREDRAW;
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

    headerState_.surface = GetSysColor(COLOR_BTNFACE);
    headerState_.text = GetSysColor(COLOR_BTNTEXT);
    headerState_.line = GetSysColor(COLOR_BTNSHADOW);

    header_ = CreateWindowExW(
        0, kHeaderClass, nullptr, WS_CHILD | WS_VISIBLE,
        0, 0, 0, 0, parent, nullptr, instance, nullptr);
    SetWindowLongPtrW(header_, GWLP_USERDATA, reinterpret_cast<LONG_PTR>(&headerState_));

    tree_ = CreateWindowExW(
        WS_EX_CLIENTEDGE, WC_TREEVIEWW, L"",
        WS_CHILD | WS_VISIBLE | TVS_HASLINES | TVS_HASBUTTONS | TVS_LINESATROOT | TVS_SHOWSELALWAYS,
        0, 0, 0, 0, parent, nullptr, instance, nullptr);
    if (tree_ == nullptr) {
        return false;
    }

    icons_ = CreateCategoryImageList(GetSysColor(COLOR_WINDOWTEXT));
    SendMessageW(tree_, TVM_SETIMAGELIST, TVSIL_NORMAL, reinterpret_cast<LPARAM>(icons_));

    Populate();
    return true;
}

// Inserts the fixed category roots; the anime groups fill in at runtime.
void Sidebar::Populate() {
    HTREEITEM all = Insert(TVI_ROOT, Str(STR_CAT_ALL), CAT_FOLDER);

    HTREEITEM queues = Insert(TVI_ROOT, Str(STR_CAT_QUEUE), CAT_QUEUE);
    Insert(queues, Str(STR_QUEUE_MAIN), CAT_QUEUE);
    Insert(queues, Str(STR_QUEUE_SCHEDULER), CAT_TIMER);

    SendMessageW(tree_, TVM_EXPAND, TVE_EXPAND, reinterpret_cast<LPARAM>(queues));
    SendMessageW(tree_, TVM_SELECTITEM, TVGN_CARET, reinterpret_cast<LPARAM>(all));
}

// Appends a labelled item under the given parent and returns its handle.
HTREEITEM Sidebar::Insert(HTREEITEM parent, const wchar_t* text, int icon) {
    TVINSERTSTRUCTW insert = {};
    insert.hParent = parent;
    insert.hInsertAfter = TVI_LAST;
    insert.item.mask = TVIF_TEXT | TVIF_IMAGE | TVIF_SELECTEDIMAGE;
    insert.item.pszText = const_cast<wchar_t*>(text);
    insert.item.iImage = icon;
    insert.item.iSelectedImage = icon;
    return reinterpret_cast<HTREEITEM>(
        SendMessageW(tree_, TVM_INSERTITEMW, 0, reinterpret_cast<LPARAM>(&insert)));
}

// Rebuilds the tree and repaints the caption in the active language.
void Sidebar::Retranslate() {
    SendMessageW(tree_, TVM_DELETEITEM, 0, reinterpret_cast<LPARAM>(TVI_ROOT));
    Populate();
    InvalidateRect(header_, nullptr, TRUE);
}

// Pushes the palette onto the caption bar, the tree and its glyphs.
void Sidebar::ApplyTheme(const Theme& theme) {
    const ThemeColors& colors = theme.Colors();
    headerState_.surface = colors.surface;
    headerState_.text = colors.text;
    headerState_.line = colors.line;
    InvalidateRect(header_, nullptr, TRUE);

    HIMAGELIST previous = icons_;
    icons_ = CreateCategoryImageList(colors.text);
    SendMessageW(tree_, TVM_SETIMAGELIST, TVSIL_NORMAL, reinterpret_cast<LPARAM>(icons_));
    if (previous != nullptr) {
        ImageList_Destroy(previous);
    }

    theme.ApplyToTree(tree_);
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
