#include "ui/MainWindow.h"

#include <commctrl.h>
#include <uxtheme.h>
#include <windowsx.h>

#include "ui/AddDialog.h"
#include "ui/Commands.h"
#include "ui/ContextMenu.h"
#include "ui/HelpDialogs.h"
#include "ui/SearchDialog.h"
#include "ui/SettingsDialog.h"
#include "ui/Strings.h"

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
    case WM_ERASEBKGND: {
        HBRUSH brush = theme_.WindowBrush();
        if (brush == nullptr) {
            break;
        }
        RECT client = {};
        GetClientRect(hwnd_, &client);
        FillRect(reinterpret_cast<HDC>(wParam), &client, brush);
        return 1;
    }
    case WM_MEASUREITEM:
        if (menuBar_.MeasureItem(reinterpret_cast<MEASUREITEMSTRUCT*>(lParam), hwnd_)) {
            return TRUE;
        }
        break;
    case WM_DRAWITEM:
        if (menuBar_.DrawItem(reinterpret_cast<const DRAWITEMSTRUCT*>(lParam))) {
            return TRUE;
        }
        break;
    case WM_NOTIFY: {
        auto* notify = reinterpret_cast<NMHDR*>(lParam);
        if (notify->code == NM_CUSTOMDRAW) {
            if (notify->hwndFrom == toolbar_.Handle()) {
                return OnToolbarCustomDraw(reinterpret_cast<NMTBCUSTOMDRAW*>(lParam));
            }
            if (notify->hwndFrom == downloads_.Handle() ||
                notify->hwndFrom == extensions_.Handle()) {
                return OnListCustomDraw(reinterpret_cast<NMLVCUSTOMDRAW*>(lParam));
            }
        }
        break;
    }
    case WM_SETTINGCHANGE:
        if (theme_.Mode() == ThemeMode::System && lParam != 0 &&
            lstrcmpiW(reinterpret_cast<const wchar_t*>(lParam), L"ImmersiveColorSet") == 0) {
            theme_.SetMode(ThemeMode::System);
            ApplyTheme();
        }
        break;
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
        break;
    }
    return DefWindowProcW(hwnd_, msg, wParam, lParam);
}

// Builds the menu bar, toolbar, downloads list and status bar.
void MainWindow::OnCreate() {
    HINSTANCE instance = reinterpret_cast<HINSTANCE>(GetWindowLongPtrW(hwnd_, GWLP_HINSTANCE));

    themeCommand_ = ID_MODE_SYSTEM;
    languageCommand_ = ID_LANG_FR;
    theme_.SetMode(ThemeMode::System);

    menuBar_.AttachTo(hwnd_);
    menuBar_.SetCategoriesChecked(sidebarVisible_);
    menuBar_.SetTheme(themeCommand_);
    menuBar_.SetLanguage(languageCommand_);
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
        {FVIRTKEY | FCONTROL, 'F', ID_DOWNLOAD_SEARCH},
        {FVIRTKEY | FCONTROL | FSHIFT, 'V', ID_TASK_BATCH},
        {FVIRTKEY, VK_DELETE, ID_FILE_REMOVE},
        {FVIRTKEY, VK_F1, ID_HELP_HELP},
    };
    accel_ = CreateAcceleratorTableW(accels, ARRAYSIZE(accels));

    SendMessageW(statusBar_, SB_SETTEXTW, 0, reinterpret_cast<LPARAM>(Str(STR_STATUS_READY)));
    ApplyTheme();
}

// Pushes the active palette onto the frame and every child control.
void MainWindow::ApplyTheme() {
    theme_.ApplyToFrame(hwnd_);
    menuBar_.ApplyTheme(theme_, hwnd_);
    menuBar_.SetCategoriesChecked(sidebarVisible_);
    menuBar_.SetTheme(themeCommand_);
    menuBar_.SetLanguage(languageCommand_);
    theme_.ApplyToList(downloads_.Handle());
    theme_.ApplyToList(extensions_.Handle());
    sidebar_.ApplyTheme(theme_);
    toolbar_.ApplyTheme(theme_);
    SetWindowTheme(statusBar_, theme_.IsDark() ? L"DarkMode_Explorer" : L"Explorer", nullptr);
    InvalidateRect(hwnd_, nullptr, TRUE);
    DrawMenuBar(hwnd_);
}

// Rebuilds every caption of the shell in the active language.
void MainWindow::Retranslate() {
    menuBar_.Rebuild(hwnd_);
    menuBar_.SetCategoriesChecked(sidebarVisible_);
    menuBar_.SetTheme(themeCommand_);
    menuBar_.SetLanguage(languageCommand_);
    toolbar_.Retranslate();
    sidebar_.Retranslate();
    downloads_.Retranslate();
    extensions_.Retranslate();
    SendMessageW(statusBar_, SB_SETTEXTW, 0, reinterpret_cast<LPARAM>(Str(STR_STATUS_READY)));
    Relayout();
}

// Draws the column separators of a list, which the built-in grid lines only
// render in a fixed light colour that glares on a dark background.
LRESULT MainWindow::OnListCustomDraw(NMLVCUSTOMDRAW* draw) {
    if (draw->nmcd.dwDrawStage == CDDS_PREPAINT) {
        return CDRF_NOTIFYPOSTPAINT;
    }
    if (draw->nmcd.dwDrawStage != CDDS_POSTPAINT) {
        return CDRF_DODEFAULT;
    }

    HWND list = draw->nmcd.hdr.hwndFrom;
    HWND header = ListView_GetHeader(list);
    if (header == nullptr) {
        return CDRF_DODEFAULT;
    }

    RECT client = {};
    GetClientRect(list, &client);

    HDC dc = draw->nmcd.hdc;
    HPEN pen = CreatePen(PS_SOLID, 1, theme_.Colors().line);
    HPEN previous = static_cast<HPEN>(SelectObject(dc, pen));

    int columns = Header_GetItemCount(header);
    for (int column = 0; column < columns; ++column) {
        RECT item = {};
        if (!Header_GetItemRect(header, column, &item)) {
            continue;
        }
        MoveToEx(dc, item.right - 1, item.bottom, nullptr);
        LineTo(dc, item.right - 1, client.bottom);
    }

    SelectObject(dc, previous);
    DeleteObject(pen);
    return CDRF_DODEFAULT;
}

// Paints the toolbar background and captions with the active palette.
LRESULT MainWindow::OnToolbarCustomDraw(NMTBCUSTOMDRAW* draw) {
    switch (draw->nmcd.dwDrawStage) {
    case CDDS_PREPAINT:
        FillRect(draw->nmcd.hdc, &draw->nmcd.rc, theme_.SurfaceBrush());
        return CDRF_NOTIFYITEMDRAW;
    case CDDS_ITEMPREPAINT:
        draw->clrText = theme_.Colors().text;
        return TBCDRF_USECDCOLORS;
    default:
        return CDRF_DODEFAULT;
    }
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

// Reports an entry the shell does not implement yet in the status bar.
void MainWindow::ShowSoon(int commandId) {
    wchar_t label[128] = {};
    if (GetMenuStringW(GetMenu(hwnd_), commandId, label, ARRAYSIZE(label), MF_BYCOMMAND) <= 0) {
        return;
    }

    wchar_t* shortcut = wcschr(label, L'\t');
    if (shortcut != nullptr) {
        *shortcut = L'\0';
    }

    wchar_t message[192] = {};
    wsprintfW(message, Str(STR_STATUS_SOON), label);
    SendMessageW(statusBar_, SB_SETTEXTW, 0, reinterpret_cast<LPARAM>(message));
}

// Dispatches menu, toolbar and context menu commands.
void MainWindow::OnCommand(int commandId) {
    HINSTANCE instance = reinterpret_cast<HINSTANCE>(GetWindowLongPtrW(hwnd_, GWLP_HINSTANCE));

    switch (commandId) {
    case ID_TASK_ADD:
        ShowAddDialog(hwnd_, instance);
        break;
    case ID_DOWNLOAD_SEARCH:
        ShowSearchDialog(hwnd_, instance);
        break;
    case ID_VIEW_SETTINGS:
        ShowSettingsDialog(hwnd_, instance);
        break;
    case ID_HELP_SHORTCUTS:
        ShowShortcutsDialog(hwnd_, instance);
        break;
    case ID_HELP_ABOUT:
    case ID_HELP_AUTHORS:
    case ID_HELP_LICENSE:
    case ID_HELP_CREDITS:
        ShowAboutDialog(hwnd_, instance);
        break;
    case ID_VIEW_ADDONS:
        ShowView(currentView_ == View::Extensions ? View::Downloads : View::Extensions);
        break;
    case ID_VIEW_CATEGORIES:
        sidebarVisible_ = !sidebarVisible_;
        menuBar_.SetCategoriesChecked(sidebarVisible_);
        sidebar_.SetVisible(SidebarShown());
        Relayout();
        break;
    case ID_MODE_DARK:
    case ID_MODE_LIGHT:
    case ID_MODE_SYSTEM:
        themeCommand_ = commandId;
        theme_.SetMode(commandId == ID_MODE_DARK    ? ThemeMode::Dark
                       : commandId == ID_MODE_LIGHT ? ThemeMode::Light
                                                    : ThemeMode::System);
        menuBar_.SetTheme(commandId);
        ApplyTheme();
        break;
    case ID_LANG_EN:
    case ID_LANG_FR:
        languageCommand_ = commandId;
        ::SetLanguage(commandId == ID_LANG_EN ? Language::English : Language::French);
        Retranslate();
        break;
    case ID_TASK_QUIT:
        DestroyWindow(hwnd_);
        break;
    default:
        ShowSoon(commandId);
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
    SendMessageW(sidebar_.Handle(), WM_SETFONT, reinterpret_cast<WPARAM>(uiFont_), TRUE);
    SendMessageW(sidebar_.HeaderHandle(), WM_SETFONT, reinterpret_cast<WPARAM>(uiFont_), TRUE);
    SendMessageW(downloads_.Handle(), WM_SETFONT, reinterpret_cast<WPARAM>(uiFont_), TRUE);
    SendMessageW(extensions_.Handle(), WM_SETFONT, reinterpret_cast<WPARAM>(uiFont_), TRUE);
    SendMessageW(statusBar_, WM_SETFONT, reinterpret_cast<WPARAM>(uiFont_), TRUE);
}
