#include "ui/MainWindow.h"

#include <commctrl.h>
#include <windowsx.h>

#include "ui/AddDialog.h"
#include "ui/Commands.h"
#include "ui/ContextMenu.h"
#include "ui/HelpDialogs.h"
#include "ui/SettingsDialog.h"

namespace {
constexpr wchar_t kWindowClass[] = L"AnimeDmMainWindow";
constexpr int kSplitterWidth = 5;
constexpr int kMinSidebarWidth = 140;
constexpr int kMinListWidth = 240;

// Clamps a candidate sidebar width to keep both panes usable.
int ClampSidebarWidth(int candidate, int clientWidth) {
    int maxWidth = clientWidth - kSplitterWidth - kMinListWidth;
    if (candidate < kMinSidebarWidth) {
        candidate = kMinSidebarWidth;
    }
    if (maxWidth >= kMinSidebarWidth && candidate > maxWidth) {
        candidate = maxWidth;
    }
    return candidate;
}
}  // namespace

// Registers the window class and creates the top-level window.
bool MainWindow::Create(HINSTANCE instance, const wchar_t* title) {
    WNDCLASSEXW wc = {};
    wc.cbSize = sizeof(wc);
    wc.lpfnWndProc = MainWindow::WndProcTrampoline;
    wc.hInstance = instance;
    wc.lpszClassName = kWindowClass;
    wc.hCursor = LoadCursorW(nullptr, IDC_ARROW);
    wc.hbrBackground = reinterpret_cast<HBRUSH>(COLOR_WINDOW + 1);
    RegisterClassExW(&wc);

    hwnd_ = CreateWindowExW(
        0, kWindowClass, title, WS_OVERLAPPEDWINDOW,
        CW_USEDEFAULT, CW_USEDEFAULT, 1100, 720,
        nullptr, nullptr, instance, this);

    return hwnd_ != nullptr;
}

// Makes the window visible and forces an initial paint.
void MainWindow::Show(int cmdShow) {
    ShowWindow(hwnd_, cmdShow);
    UpdateWindow(hwnd_);
}

// Routes messages to the instance, binding HWND and instance on creation.
LRESULT CALLBACK MainWindow::WndProcTrampoline(HWND hwnd, UINT msg, WPARAM wParam, LPARAM lParam) {
    MainWindow* self = nullptr;
    if (msg == WM_NCCREATE) {
        auto* create = reinterpret_cast<CREATESTRUCTW*>(lParam);
        self = static_cast<MainWindow*>(create->lpCreateParams);
        self->hwnd_ = hwnd;
        SetWindowLongPtrW(hwnd, GWLP_USERDATA, reinterpret_cast<LONG_PTR>(self));
    } else {
        self = reinterpret_cast<MainWindow*>(GetWindowLongPtrW(hwnd, GWLP_USERDATA));
    }

    if (self != nullptr) {
        return self->HandleMessage(msg, wParam, lParam);
    }
    return DefWindowProcW(hwnd, msg, wParam, lParam);
}

// Handles per-window messages for the instance.
LRESULT MainWindow::HandleMessage(UINT msg, WPARAM wParam, LPARAM lParam) {
    switch (msg) {
    case WM_CREATE:
        OnCreate();
        return 0;
    case WM_SIZE:
        Relayout();
        return 0;
    case WM_COMMAND:
        OnCommand(LOWORD(wParam));
        return 0;
    case WM_CONTEXTMENU:
        OnContextMenu(reinterpret_cast<HWND>(wParam), GET_X_LPARAM(lParam), GET_Y_LPARAM(lParam));
        return 0;
    case WM_SETCURSOR:
        if (LOWORD(lParam) == HTCLIENT && OnSetCursor()) {
            return TRUE;
        }
        return DefWindowProcW(hwnd_, msg, wParam, lParam);
    case WM_LBUTTONDOWN:
        OnLeftButtonDown(GET_X_LPARAM(lParam));
        return 0;
    case WM_MOUSEMOVE:
        OnMouseMove(GET_X_LPARAM(lParam));
        return 0;
    case WM_LBUTTONUP:
        OnLeftButtonUp();
        return 0;
    case WM_DESTROY:
        if (accel_ != nullptr) {
            DestroyAcceleratorTable(accel_);
            accel_ = nullptr;
        }
        if (uiFont_ != nullptr) {
            DeleteObject(uiFont_);
            uiFont_ = nullptr;
        }
        PostQuitMessage(0);
        return 0;
    default:
        return DefWindowProcW(hwnd_, msg, wParam, lParam);
    }
}

// Builds the menu bar, toolbar, downloads list and status bar.
void MainWindow::OnCreate() {
    HINSTANCE instance = reinterpret_cast<HINSTANCE>(GetWindowLongPtrW(hwnd_, GWLP_HINSTANCE));

    menuBar_.AttachTo(hwnd_);
    toolbar_.Create(hwnd_, instance);
    sidebar_.Create(hwnd_, instance);

    statusBar_ = CreateWindowExW(
        0, STATUSCLASSNAMEW, nullptr,
        WS_CHILD | WS_VISIBLE | SBARS_SIZEGRIP,
        0, 0, 0, 0, hwnd_, nullptr, instance, nullptr);

    downloads_.Create(hwnd_, instance);
    extensions_.Create(hwnd_, instance);
    ApplyUiFont();

    ACCEL accels[] = {
        {FVIRTKEY | FCONTROL, 'N', ID_TASK_ADD},
        {FVIRTKEY, VK_DELETE, ID_FILE_REMOVE},
    };
    accel_ = CreateAcceleratorTableW(accels, ARRAYSIZE(accels));

    SendMessageW(statusBar_, SB_SETTEXTW, 0, reinterpret_cast<LPARAM>(L"Prêt"));
}

// Switches between the downloads and extensions screens.
void MainWindow::ShowView(View view) {
    if (currentView_ == view) {
        return;
    }
    currentView_ = view;

    bool downloads = view == View::Downloads;
    sidebar_.SetVisible(downloads && sidebarVisible_);
    ShowWindow(downloads_.Handle(), downloads ? SW_SHOW : SW_HIDE);
    extensions_.SetVisible(!downloads);

    Relayout();
}

// Reports whether the categories panel is currently part of the layout.
bool MainWindow::SidebarShown() const {
    return currentView_ == View::Downloads && sidebarVisible_;
}

// Lays out the toolbar, status bar and the active content view.
void MainWindow::Relayout() {
    toolbar_.Resize();
    SendMessageW(statusBar_, WM_SIZE, 0, 0);

    RECT client = {};
    GetClientRect(hwnd_, &client);

    int top = toolbar_.Height();

    RECT statusRect = {};
    GetWindowRect(statusBar_, &statusRect);
    int statusHeight = statusRect.bottom - statusRect.top;

    int contentHeight = client.bottom - top - statusHeight;

    if (currentView_ == View::Extensions) {
        extensions_.SetBounds(0, top, client.right, contentHeight);
        return;
    }

    if (!sidebarVisible_) {
        downloads_.SetBounds(0, top, client.right, contentHeight);
        return;
    }

    sidebarWidth_ = ClampSidebarWidth(sidebarWidth_, client.right);
    sidebar_.SetBounds(0, top, sidebarWidth_, contentHeight);
    int listX = sidebarWidth_ + kSplitterWidth;
    downloads_.SetBounds(listX, top, client.right - listX, contentHeight);
}

// Returns the draggable splitter band between the sidebar and the list.
RECT MainWindow::SplitterRect() const {
    RECT client = {};
    GetClientRect(hwnd_, &client);

    RECT statusRect = {};
    GetWindowRect(statusBar_, &statusRect);
    int statusHeight = statusRect.bottom - statusRect.top;

    RECT rect = {};
    rect.left = sidebarWidth_;
    rect.right = sidebarWidth_ + kSplitterWidth;
    rect.top = toolbar_.Height();
    rect.bottom = client.bottom - statusHeight;
    return rect;
}

// Shows the horizontal resize cursor while hovering the splitter band.
bool MainWindow::OnSetCursor() {
    if (!SidebarShown()) {
        return false;
    }
    POINT pt = {};
    GetCursorPos(&pt);
    ScreenToClient(hwnd_, &pt);

    RECT splitter = SplitterRect();
    if (draggingSplitter_ || PtInRect(&splitter, pt)) {
        SetCursor(LoadCursorW(nullptr, IDC_SIZEWE));
        return true;
    }
    return false;
}

// Starts a splitter drag when the press lands on the splitter band.
void MainWindow::OnLeftButtonDown(int x) {
    if (!SidebarShown()) {
        return;
    }
    if (x >= sidebarWidth_ && x < sidebarWidth_ + kSplitterWidth) {
        draggingSplitter_ = true;
        SetCapture(hwnd_);
    }
}

// Resizes the sidebar to follow the cursor during a splitter drag.
void MainWindow::OnMouseMove(int x) {
    if (!draggingSplitter_) {
        return;
    }
    RECT client = {};
    GetClientRect(hwnd_, &client);
    sidebarWidth_ = ClampSidebarWidth(x, client.right);
    Relayout();
}

// Ends an in-progress splitter drag.
void MainWindow::OnLeftButtonUp() {
    if (draggingSplitter_) {
        draggingSplitter_ = false;
        ReleaseCapture();
    }
}

// Dispatches menu and toolbar commands.
void MainWindow::OnCommand(int commandId) {
    switch (commandId) {
    case ID_TASK_ADD: {
        HINSTANCE instance = reinterpret_cast<HINSTANCE>(GetWindowLongPtrW(hwnd_, GWLP_HINSTANCE));
        ShowAddDialog(hwnd_, instance);
        break;
    }
    case ID_HELP_ABOUT: {
        HINSTANCE instance = reinterpret_cast<HINSTANCE>(GetWindowLongPtrW(hwnd_, GWLP_HINSTANCE));
        ShowAboutDialog(hwnd_, instance);
        break;
    }
    case ID_HELP_SHORTCUTS: {
        HINSTANCE instance = reinterpret_cast<HINSTANCE>(GetWindowLongPtrW(hwnd_, GWLP_HINSTANCE));
        ShowShortcutsDialog(hwnd_, instance);
        break;
    }
    case ID_VIEW_SETTINGS: {
        HINSTANCE instance = reinterpret_cast<HINSTANCE>(GetWindowLongPtrW(hwnd_, GWLP_HINSTANCE));
        ShowSettingsDialog(hwnd_, instance);
        break;
    }
    case ID_VIEW_DOWNLOADS:
        ShowView(View::Downloads);
        break;
    case ID_VIEW_ADDONS:
        ShowView(View::Extensions);
        break;
    case ID_VIEW_CATEGORIES:
        sidebarVisible_ = !sidebarVisible_;
        sidebar_.SetVisible(SidebarShown());
        Relayout();
        break;
    case ID_FILE_EXIT:
        DestroyWindow(hwnd_);
        break;
    default:
        break;
    }
}

// Shows the right-click menu over the downloads list and routes the result.
void MainWindow::OnContextMenu(HWND target, int x, int y) {
    if (target != downloads_.Handle()) {
        return;
    }

    if (x == -1 && y == -1) {
        RECT rect = {};
        GetWindowRect(downloads_.Handle(), &rect);
        x = rect.left + 8;
        y = rect.top + 8;
    }

    int command = ShowDownloadsContextMenu(hwnd_, x, y);
    if (command != 0) {
        OnCommand(command);
    }
}

// Applies the system message font to the child controls for a native look.
void MainWindow::ApplyUiFont() {
    NONCLIENTMETRICSW metrics = {};
    metrics.cbSize = sizeof(metrics);
    if (!SystemParametersInfoW(SPI_GETNONCLIENTMETRICS, sizeof(metrics), &metrics, 0)) {
        return;
    }

    uiFont_ = CreateFontIndirectW(&metrics.lfMessageFont);
    SendMessageW(toolbar_.Handle(), WM_SETFONT, reinterpret_cast<WPARAM>(uiFont_), TRUE);
    SendMessageW(toolbar_.SearchHandle(), WM_SETFONT, reinterpret_cast<WPARAM>(uiFont_), TRUE);
    SendMessageW(sidebar_.Handle(), WM_SETFONT, reinterpret_cast<WPARAM>(uiFont_), TRUE);
    SendMessageW(sidebar_.HeaderHandle(), WM_SETFONT, reinterpret_cast<WPARAM>(uiFont_), TRUE);
    SendMessageW(downloads_.Handle(), WM_SETFONT, reinterpret_cast<WPARAM>(uiFont_), TRUE);
    SendMessageW(extensions_.Handle(), WM_SETFONT, reinterpret_cast<WPARAM>(uiFont_), TRUE);
    SendMessageW(statusBar_, WM_SETFONT, reinterpret_cast<WPARAM>(uiFont_), TRUE);
}
