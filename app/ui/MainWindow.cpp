#include "ui/MainWindow.h"

#include <commctrl.h>
#include <shellapi.h>
#include <uxtheme.h>
#include <windowsx.h>

#include <algorithm>
#include <ctime>
#include <filesystem>
#include <fstream>
#include <thread>

#include "core/Addon.h"
#include "core/Digest.h"
#include "core/FolderIcon.h"
#include "core/Paths.h"
#include "core/Queue.h"
#include "core/Text.h"
#include "ui/AddDialog.h"
#include "ui/AddonsDialog.h"
#include "ui/Commands.h"
#include "ui/ConfirmDialog.h"
#include "ui/ContextMenu.h"
#include "ui/FileIcons.h"
#include "ui/HelpDialogs.h"
#include "ui/NoticeDialog.h"
#include "ui/Resource.h"
#include "ui/SearchDialog.h"
#include "ui/SettingsDialog.h"
#include "ui/Strings.h"

namespace {
constexpr wchar_t kWindowClass[] = L"AnimeDmMainWindow";
constexpr int kSplitterWidth = 5;
constexpr int kMargin = 6;  // breathing room between the panels and the frame
constexpr int kMinSidebarWidth = 140;
constexpr int kMinListWidth = 240;
constexpr char kFluentSkin[] = "fluent";  // the settings value naming the icon font
constexpr UINT kDownloadEvent = WM_APP + 20;
constexpr UINT kPosterEvent = WM_APP + 21;
constexpr UINT kIconEvent = WM_APP + 22;
constexpr int kNameColumn = 0;
constexpr int kStatusColumn = 2;
constexpr int kIconGap = 4;  // around the picture of a file type

// The bytes of an image, on their way from a worker thread to the panel.
struct PosterPayloadData {
    std::string animeUrl;
    std::string posterUrl;
    std::vector<uint8_t> bytes;
};

// What a worker thread reports once it dressed a folder up.
struct IconPayloadData {
    std::string animeUrl;
    std::string templateId;  // empty when no icon was asked for
    foldericon::Error error = foldericon::Error::None;
    std::string detail;
    bool aniyomi = false;
    bool aniyomiOk = false;
    bool announce = false;  // whether the user asked for it and awaits an answer
};

// The scheme and host of a URL, with a trailing slash: what image hosts want
// to see as a referer.
std::string OriginOf(const std::string& url) {
    size_t scheme = url.find("://");
    if (scheme == std::string::npos) {
        return std::string();
    }
    size_t end = url.find('/', scheme + 3);
    return url.substr(0, end == std::string::npos ? url.size() : end) + "/";
}

// Where the poster of an anime is kept, named after its page.
std::wstring PosterPath(const std::string& animeUrl) {
    std::wstring dir = paths::PostersDir();
    if (dir.empty()) {
        return std::wstring();
    }
    std::vector<uint8_t> key(animeUrl.begin(), animeUrl.end());
    return dir + L"\\" + Widen(digest::Sha256Hex(key).substr(0, 16)) + L".img";
}

// The whole content of a file, empty when absent.
std::vector<uint8_t> ReadBytes(const std::wstring& path) {
    std::ifstream file(std::filesystem::path(path), std::ios::binary);
    if (!file) {
        return {};
    }
    return std::vector<uint8_t>(std::istreambuf_iterator<char>(file), {});
}

// Writes a file whole.
void WriteBytes(const std::wstring& path, const std::vector<uint8_t>& bytes) {
    std::ofstream file(std::filesystem::path(path), std::ios::binary | std::ios::trunc);
    if (file) {
        file.write(reinterpret_cast<const char*>(bytes.data()),
                   static_cast<std::streamsize>(bytes.size()));
    }
}

// The folder that holds a file.
std::wstring FolderOf(const std::wstring& path) {
    size_t cut = path.find_last_of(L"\\/");
    return cut == std::wstring::npos ? std::wstring() : path.substr(0, cut);
}

// Whether the episode is a film rather than a numbered episode.
bool IsMovie(const std::string& name) {
    std::string lower = name;
    std::transform(lower.begin(), lower.end(), lower.begin(),
                   [](unsigned char c) { return static_cast<char>(std::tolower(c)); });
    return lower.find("film") != std::string::npos || lower.find("movie") != std::string::npos;
}

// Clamps a candidate sidebar width to keep both panes usable.
int ClampSidebarWidth(int candidate, int clientWidth) {
    int maxWidth = clientWidth - 2 * kMargin - kSplitterWidth - kMinListWidth;
    if (candidate < kMinSidebarWidth) {
        candidate = kMinSidebarWidth;
    }
    if (maxWidth >= kMinSidebarWidth && candidate > maxWidth) {
        candidate = maxWidth;
    }
    return candidate;
}
}  // namespace

// The bytes of an image, on their way from a worker thread to the panel.
struct PosterPayload : PosterPayloadData {};

// What a worker thread reports once it dressed a folder up.
struct IconPayload : IconPayloadData {};

// Registers the window class and creates the top-level window.
bool MainWindow::Create(HINSTANCE instance, const wchar_t* title) {
    WNDCLASSEXW wc = {};
    wc.cbSize = sizeof(wc);
    wc.lpfnWndProc = MainWindow::WndProcTrampoline;
    wc.hInstance = instance;
    wc.lpszClassName = kWindowClass;
    wc.hCursor = LoadCursorW(nullptr, IDC_ARROW);
    wc.hbrBackground = reinterpret_cast<HBRUSH>(COLOR_WINDOW + 1);
    wc.hIcon = LoadIconW(instance, MAKEINTRESOURCEW(IDI_APP));
    wc.hIconSm = static_cast<HICON>(LoadImageW(instance, MAKEINTRESOURCEW(IDI_APP), IMAGE_ICON,
                                               GetSystemMetrics(SM_CXSMICON),
                                               GetSystemMetrics(SM_CYSMICON), LR_DEFAULTCOLOR));
    RegisterClassExW(&wc);

    hwnd_ = CreateWindowExW(
        0, kWindowClass, title, WS_OVERLAPPEDWINDOW | WS_CLIPCHILDREN,
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
    case kDownloadEvent:
        OnDownloadEvent(std::unique_ptr<DownloadEvent>(reinterpret_cast<DownloadEvent*>(lParam)));
        return 0;
    case kPosterEvent:
        OnPosterEvent(std::unique_ptr<PosterPayload>(reinterpret_cast<PosterPayload*>(lParam)));
        return 0;
    case kIconEvent:
        OnIconEvent(std::unique_ptr<IconPayload>(reinterpret_cast<IconPayload*>(lParam)));
        return 0;
    case WM_CONTEXTMENU:
        OnContextMenu(reinterpret_cast<HWND>(wParam), GET_X_LPARAM(lParam), GET_Y_LPARAM(lParam));
        return 0;
    case WM_ERASEBKGND: {
        // What shows between the panels is the colour of the toolbar, as in IDM.
        HBRUSH brush = ActiveTheme().SurfaceBrush();
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
        if (notify->hwndFrom == toolbar_.Handle() && notify->code == TBN_HOTITEMCHANGE) {
            toolbar_.OnHotItem(reinterpret_cast<NMTBHOTITEM*>(lParam));
            return 0;
        }
        if (notify->code == NM_CUSTOMDRAW) {
            if (notify->hwndFrom == toolbar_.Handle()) {
                return OnToolbarCustomDraw(reinterpret_cast<NMTBCUSTOMDRAW*>(lParam));
            }
            if (notify->hwndFrom == downloads_.Handle()) {
                return OnListCustomDraw(reinterpret_cast<NMLVCUSTOMDRAW*>(lParam));
            }
        }
        if (notify->hwndFrom == downloads_.Handle()) {
            if (notify->code == LVN_ITEMCHANGED) {
                UpdateActions();
            } else if (notify->code == NM_DBLCLK) {
                OpenSelected(false);
            }
        }
        if (notify->hwndFrom == sidebar_.Handle()) {
            return OnSidebarNotify(notify);
        }
        break;
    }
    case WM_SETTINGCHANGE:
        if (ActiveTheme().Mode() == ThemeMode::System && lParam != 0 &&
            lstrcmpiW(reinterpret_cast<const wchar_t*>(lParam), L"ImmersiveColorSet") == 0) {
            ActiveTheme().SetMode(ThemeMode::System);
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
    case WM_CAPTURECHANGED:
        if (draggingSplitter_ && reinterpret_cast<HWND>(lParam) != hwnd_) {
            CancelSplitterDrag();
        }
        return 0;
    case WM_DESTROY:
        OnDestroy();
        return 0;
    default:
        break;
    }
    return DefWindowProcW(hwnd_, msg, wParam, lParam);
}

// Builds the menu bar, toolbar, downloads list and status bar.
void MainWindow::OnCreate() {
    HINSTANCE instance = reinterpret_cast<HINSTANCE>(GetWindowLongPtrW(hwnd_, GWLP_HINSTANCE));

    settings_ = settings::Load();
    languageCommand_ = settings_.language == "en" ? ID_LANG_EN : ID_LANG_FR;
    ::SetLanguage(languageCommand_ == ID_LANG_EN ? Language::English : Language::French);
    themeCommand_ = settings_.theme == "dark"    ? ID_MODE_DARK
                    : settings_.theme == "light" ? ID_MODE_LIGHT
                                                 : ID_MODE_SYSTEM;
    ActiveTheme().SetMode(themeCommand_ == ID_MODE_DARK    ? ThemeMode::Dark
                          : themeCommand_ == ID_MODE_LIGHT ? ThemeMode::Light
                                                           : ThemeMode::System);

    skins_ = skins::Discover();
    std::vector<std::wstring> skinNames;
    for (const ToolbarSkin& skin : skins_) {
        skinNames.push_back(skin.name);
    }
    menuBar_.SetToolbarSkins(skinNames, ChosenSkin());

    menuBar_.AttachTo(hwnd_);
    menuBar_.SetCategoriesChecked(sidebarVisible_);
    menuBar_.SetTheme(themeCommand_);
    menuBar_.SetLanguage(languageCommand_);
    toolbar_.Create(hwnd_, instance);
    if (ChosenSkin() >= 0) {
        toolbar_.SetSkin(&skins_[static_cast<size_t>(ChosenSkin())], ActiveTheme());
    }
    sidebar_.Create(hwnd_, instance);

    downloads_.Create(hwnd_, instance);
    ApplyUiFont();

    queue::State state = queue::Load();
    items_ = std::move(state.items);
    groups_ = std::move(state.groups);
    for (const DownloadItem& item : items_) {
        nextId_ = std::max(nextId_, item.id + 1);
    }
    PruneGroups();
    FillList();
    RebuildSidebar();
    LoadPosters();
    downloader_.Attach(hwnd_, kDownloadEvent);

    ACCEL accels[] = {
        {FVIRTKEY | FCONTROL, 'N', ID_TASK_ADD},
        {FVIRTKEY | FCONTROL, 'F', ID_DOWNLOAD_SEARCH},
        {FVIRTKEY | FCONTROL | FSHIFT, 'V', ID_TASK_BATCH},
        {FVIRTKEY, VK_DELETE, ID_FILE_REMOVE},
        {FVIRTKEY, VK_F1, ID_HELP_HELP},
    };
    accel_ = CreateAcceleratorTableW(accels, ARRAYSIZE(accels));

    ApplyTheme();
    UpdateActions();
}

// Stops the transfers, keeps their parts, and records the queue as it stands.
void MainWindow::OnDestroy() {
    downloader_.Attach(nullptr, 0);
    downloader_.PauseAll();
    for (DownloadItem& item : items_) {
        if (IsActive(item.status)) {
            item.status = DownloadStatus::Stopped;
            item.speed = 0.0;
        }
    }
    Persist();

    if (accel_ != nullptr) {
        DestroyAcceleratorTable(accel_);
        accel_ = nullptr;
    }
    if (uiFont_ != nullptr) {
        DeleteObject(uiFont_);
        uiFont_ = nullptr;
    }
    PostQuitMessage(0);
}

// Pushes the active palette onto the frame and every child control.
void MainWindow::ApplyTheme() {
    ActiveTheme().ApplyToFrame(hwnd_);
    menuBar_.ApplyTheme(ActiveTheme(), hwnd_);
    menuBar_.SetCategoriesChecked(sidebarVisible_);
    menuBar_.SetTheme(themeCommand_);
    menuBar_.SetLanguage(languageCommand_);
    ActiveTheme().ApplyToList(downloads_.Handle());
    sidebar_.ApplyTheme(ActiveTheme());
    toolbar_.ApplyTheme(ActiveTheme());
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
    for (const DownloadItem& item : items_) {
        downloads_.Upsert(item);
    }
    Relayout();
    UpdateActions();
}

// Draws the column separators of a list, which the built-in grid lines only
// render in a fixed light colour that glares on a dark background.
LRESULT MainWindow::OnListCustomDraw(NMLVCUSTOMDRAW* draw) {
    switch (draw->nmcd.dwDrawStage) {
    case CDDS_PREPAINT:
        return CDRF_NOTIFYITEMDRAW | CDRF_NOTIFYPOSTPAINT;
    case CDDS_ITEMPREPAINT:
        DrawRow(draw);
        return CDRF_SKIPDEFAULT;
    case CDDS_POSTPAINT:
        break;
    default:
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
    HPEN pen = CreatePen(PS_SOLID, 1, ActiveTheme().Colors().line);
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

    // One rule under every row, existing or not, so the grid reaches the
    // bottom of the list the way the built-in one does.
    RECT first = {};
    if (ListView_GetItemCount(list) > 0 && ListView_GetItemRect(list, 0, &first, LVIR_BOUNDS)) {
        int height = first.bottom - first.top;
        for (int y = first.bottom - 1; height > 0 && y < client.bottom; y += height) {
            MoveToEx(dc, client.left, y, nullptr);
            LineTo(dc, client.right, y);
        }
    }

    SelectObject(dc, previous);
    DeleteObject(pen);
    return CDRF_DODEFAULT;
}

// Paints one row of the list the way IDM paints its own: the flat highlight
// of the system behind a chosen row, white captions on it, and every cell
// clipped to the column its header describes.
void MainWindow::DrawRow(NMLVCUSTOMDRAW* draw) {
    HWND list = draw->nmcd.hdr.hwndFrom;
    HWND header = ListView_GetHeader(list);
    int row = static_cast<int>(draw->nmcd.dwItemSpec);
    RECT bounds = {};
    if (header == nullptr || !ListView_GetItemRect(list, row, &bounds, LVIR_BOUNDS)) {
        return;
    }

    const ThemeColors& colors = ActiveTheme().Colors();
    bool selected = (ListView_GetItemState(list, row, LVIS_SELECTED) & LVIS_SELECTED) != 0;
    HDC dc = draw->nmcd.hdc;

    RECT client = {};
    GetClientRect(list, &client);
    RECT blank = {client.left, bounds.top, client.right, bounds.bottom};
    HBRUSH background = CreateSolidBrush(colors.window);
    FillRect(dc, &blank, background);
    DeleteObject(background);

    uint64_t id = static_cast<uint64_t>(draw->nmcd.lItemlParam);
    const DownloadItem* item = Find(id);
    int icon = item != nullptr ? fileicons::IndexOf(item->outPath) : -1;
    int iconSize = icon >= 0 ? fileicons::Size() : 0;
    // The highlight leaves the picture of the file type outside, the way a
    // list of Windows does and IDM after it.
    if (selected) {
        RECT highlight = bounds;
        if (icon >= 0) {
            highlight.left = std::min<LONG>(bounds.right, bounds.left + kIconGap * 2 + iconSize);
        }
        HBRUSH fill = CreateSolidBrush(colors.accent);
        FillRect(dc, &highlight, fill);
        DeleteObject(fill);
    }

    HFONT font = reinterpret_cast<HFONT>(SendMessageW(list, WM_GETFONT, 0, 0));
    HFONT previous = font != nullptr ? static_cast<HFONT>(SelectObject(dc, font)) : nullptr;
    SetBkMode(dc, TRANSPARENT);
    SetTextColor(dc, selected ? colors.accentText : colors.text);

    int columns = Header_GetItemCount(header);
    for (int column = 0; column < columns; ++column) {
        RECT span = {};
        if (!Header_GetItemRect(header, column, &span)) {
            continue;
        }
        RECT cell = {span.left, bounds.top, span.right, bounds.bottom};
        if (cell.right <= client.left || cell.left >= client.right) {
            continue;
        }
        if (column == kStatusColumn && DrawProgressCell(dc, cell, id, selected)) {
            continue;
        }
        wchar_t text[512] = {};
        ListView_GetItemText(list, row, column, text, ARRAYSIZE(text));
        RECT label = cell;
        label.left += 6;
        label.right -= 6;
        if (column == kNameColumn && icon >= 0) {
            ImageList_Draw(fileicons::SmallList(), icon, dc, cell.left + kIconGap,
                           (cell.top + cell.bottom - iconSize) / 2, ILD_TRANSPARENT);
            label.left = cell.left + kIconGap * 2 + iconSize;
        }
        if (label.right > label.left) {
            DrawTextW(dc, text, -1, &label,
                      DT_LEFT | DT_SINGLELINE | DT_VCENTER | DT_END_ELLIPSIS | DT_NOPREFIX);
        }
    }
    if (previous != nullptr) {
        SelectObject(dc, previous);
    }
}

// Paints the status cell of a running download as a bar with its percentage.
// False when the cell is not a running download, whose caption the row writes.
bool MainWindow::DrawProgressCell(HDC dc, const RECT& cell, uint64_t id, bool selected) {
    const DownloadItem* item = Find(id);
    if (item == nullptr || item->status != DownloadStatus::Downloading || item->fraction < 0.0) {
        return false;
    }

    const ThemeColors& colors = ActiveTheme().Colors();
    RECT track = cell;
    track.left += 6;
    track.right -= 6;
    track.top += 4;
    track.bottom -= 4;
    int width = std::max<int>(0, static_cast<int>(track.right - track.left));
    int filled = static_cast<int>(width * std::min(1.0, item->fraction));

    HBRUSH trackBrush = CreateSolidBrush(selected ? colors.accent : colors.surface);
    FillRect(dc, &track, trackBrush);
    DeleteObject(trackBrush);

    RECT bar = track;
    bar.right = bar.left + filled;
    HBRUSH barBrush = CreateSolidBrush(selected ? colors.accentText : colors.accent);
    FillRect(dc, &bar, barBrush);
    DeleteObject(barBrush);

    HPEN pen = CreatePen(PS_SOLID, 1, selected ? colors.accentText : colors.line);
    HPEN previousPen = static_cast<HPEN>(SelectObject(dc, pen));
    HBRUSH previousBrush = static_cast<HBRUSH>(SelectObject(dc, GetStockObject(NULL_BRUSH)));
    Rectangle(dc, track.left, track.top, track.right, track.bottom);
    SelectObject(dc, previousBrush);
    SelectObject(dc, previousPen);
    DeleteObject(pen);

    std::wstring text = StatusText(*item);
    SetBkMode(dc, TRANSPARENT);
    SetTextColor(dc, selected ? colors.accentText : colors.text);
    DrawTextW(dc, text.c_str(), -1, &track, DT_CENTER | DT_VCENTER | DT_SINGLELINE);
    return true;
}

// Paints the toolbar background and captions with the active palette.
LRESULT MainWindow::OnToolbarCustomDraw(NMTBCUSTOMDRAW* draw) {
    switch (draw->nmcd.dwDrawStage) {
    case CDDS_PREPAINT: {
        FillRect(draw->nmcd.hdc, &draw->nmcd.rc, ActiveTheme().SurfaceBrush());
        // The rule under the menu bar, which the toolbar carries on its top
        // edge. In the dark palette the system already draws one there, as it
        // does under the menu bar of IDM.
        if (!ActiveTheme().IsDark()) {
            RECT rule = draw->nmcd.rc;
            rule.bottom = rule.top + 1;
            HBRUSH line = CreateSolidBrush(ActiveTheme().Colors().line);
            FillRect(draw->nmcd.hdc, &rule, line);
            DeleteObject(line);
        }
        return CDRF_NOTIFYITEMDRAW;
    }
    case CDDS_ITEMPREPAINT: {
        const ThemeColors& colors = ActiveTheme().Colors();
        bool disabled = (draw->nmcd.uItemState & CDIS_DISABLED) != 0;
        draw->clrText = disabled ? colors.muted : colors.text;
        if (!ActiveTheme().IsDark()) {
            return TBCDRF_USECDCOLORS | TBCDRF_NOETCHEDEFFECT;
        }
        // The dark palette runs without a visual style, which would paint over
        // it; the button under the pointer then wears a raised classic frame
        // instead of a highlight. It gets the quiet fill of IDM in its place,
        // and the frame is asked to stay away.
        if (!disabled && (draw->nmcd.uItemState & (CDIS_HOT | CDIS_SELECTED)) != 0) {
            HBRUSH fill = CreateSolidBrush(colors.hover);
            FillRect(draw->nmcd.hdc, &draw->nmcd.rc, fill);
            DeleteObject(fill);
        }
        return TBCDRF_USECDCOLORS | TBCDRF_NOETCHEDEFFECT | TBCDRF_NOEDGES | TBCDRF_NOOFFSET |
               TBCDRF_NOBACKGROUND;
    }
    default:
        return CDRF_DODEFAULT;
    }
}

// Lays out the toolbar, status bar and the active content view.
void MainWindow::Relayout() {
    toolbar_.Resize();

    RECT client = {};
    GetClientRect(hwnd_, &client);

    int top = toolbar_.Height() + kMargin;
    int bottom = client.bottom - kMargin;
    int height = std::max<int>(0, bottom - top);
    int right = client.right - kMargin;

    // The panels move in one deferred batch, without copying their old
    // pixels: a copied image would leave the hand-drawn edges ghosting at
    // every position the splitter passes through.
    HDWP batch = BeginDeferWindowPos(3);
    if (!sidebarVisible_) {
        batch = downloads_.Place(batch, kMargin, top, right - kMargin, height);
    } else {
        sidebarWidth_ = ClampSidebarWidth(sidebarWidth_, client.right);
        batch = sidebar_.Place(batch, kMargin, top, sidebarWidth_, height);
        int listX = kMargin + sidebarWidth_ + kSplitterWidth;
        batch = downloads_.Place(batch, listX, top, right - listX, height);
    }
    if (batch != nullptr) {
        EndDeferWindowPos(batch);
    }
}

// Returns the draggable splitter band between the sidebar and the list.
RECT MainWindow::SplitterRect() const {
    RECT client = {};
    GetClientRect(hwnd_, &client);

    RECT rect = {};
    rect.left = kMargin + sidebarWidth_;
    rect.right = rect.left + kSplitterWidth;
    rect.top = toolbar_.Height();
    rect.bottom = client.bottom;
    return rect;
}

// Shows the horizontal resize cursor while hovering the splitter band.
bool MainWindow::OnSetCursor() {
    if (!sidebarVisible_) {
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

// Inverts the splitter band at a position: drawn once to show the tracker,
// drawn again to take it away. The window is locked for the duration of the
// drag, so the bar paints over the panels without disturbing them.
void MainWindow::DrawTracker(int x) {
    HDC dc = GetDCEx(hwnd_, nullptr, DCX_CACHE | DCX_LOCKWINDOWUPDATE);
    if (dc == nullptr) {
        return;
    }
    RECT band = SplitterRect();
    PatBlt(dc, x, band.top, kSplitterWidth, band.bottom - band.top, DSTINVERT);
    ReleaseDC(hwnd_, dc);
}

// Starts a splitter drag when the press lands on the splitter band. The
// panels stay put until the release: only a tracker bar follows the pointer,
// the way IDM does it, so nothing is repainted halfway.
void MainWindow::OnLeftButtonDown(int x) {
    if (!sidebarVisible_) {
        return;
    }
    RECT splitter = SplitterRect();
    if (x >= splitter.left && x < splitter.right) {
        draggingSplitter_ = true;
        trackX_ = splitter.left;
        SetCapture(hwnd_);
        LockWindowUpdate(hwnd_);
        DrawTracker(trackX_);
    }
}

// Moves the tracker bar with the pointer during a splitter drag.
void MainWindow::OnMouseMove(int x) {
    if (!draggingSplitter_) {
        return;
    }
    RECT client = {};
    GetClientRect(hwnd_, &client);
    int next = kMargin + ClampSidebarWidth(x - kMargin, client.right);
    if (next != trackX_) {
        DrawTracker(trackX_);
        trackX_ = next;
        DrawTracker(trackX_);
    }
}

// Ends a splitter drag: the tracker goes away and the panels take the width
// it marked, in one repaint.
void MainWindow::OnLeftButtonUp() {
    if (!draggingSplitter_) {
        return;
    }
    DrawTracker(trackX_);
    LockWindowUpdate(nullptr);
    draggingSplitter_ = false;
    sidebarWidth_ = trackX_ - kMargin;
    ReleaseCapture();
    Relayout();
}

// Drops a drag the system interrupted, leaving the panels as they were.
void MainWindow::CancelSplitterDrag() {
    DrawTracker(trackX_);
    LockWindowUpdate(nullptr);
    draggingSplitter_ = false;
}

// Asks the user for an anime, then queues the episodes it picked.
void MainWindow::OnAddDownload() {
    HINSTANCE instance = reinterpret_cast<HINSTANCE>(GetWindowLongPtrW(hwnd_, GWLP_HINSTANCE));

    AddRequest request;
    if (ShowAddDialog(hwnd_, instance, store_, http_, settings_, &request) != IDOK) {
        return;
    }

    request.animeTitle = TidyText(request.animeTitle);
    std::wstring title = SafeFileName(Widen(request.animeTitle));
    std::wstring base = request.destination.empty() ? paths::UserDownloadsDir()
                                                    : request.destination;
    std::wstring folder = base + L"\\" + title;

    if (FindGroup(request.animeUrl) == nullptr) {
        AnimeGroup group;
        group.url = request.animeUrl;
        group.title = request.animeTitle;
        group.posterUrl = request.posterUrl;
        groups_.push_back(group);
        if (!request.posterBytes.empty()) {
            WriteBytes(PosterPath(group.url), request.posterBytes);
            sidebar_.SetPoster(group.url, request.posterBytes);
        } else {
            FetchPoster(group);
        }
    }

    for (const AddRequestEpisode& episode : request.episodes) {
        DownloadItem item;
        item.id = nextId_++;
        item.addonId = request.addonId;
        item.animeTitle = request.animeTitle;
        item.animeUrl = request.animeUrl;
        item.episodeNumber = episode.number;
        item.pageUrl = episode.url;
        item.player = episode.player;
        item.movie = IsMovie(episode.name);
        item.outPath = folder + L"\\" +
                       (IsMovie(episode.name)
                            ? title + L".mp4"
                            : title + L" - Ep " + EpisodeLabel(episode.number) + L".mp4");
        item.status = DownloadStatus::Queued;
        item.addedAt = std::time(nullptr);
        items_.push_back(item);
        Refresh(item);
        downloader_.Start(TaskOf(item));
    }
    if (!request.posterBytes.empty()) {
        DecorateFolder(request.animeUrl, request.folderTemplate);
    }
    Persist();
    RebuildSidebar();
    UpdateActions();
}

// Records what a worker thread did to a folder and tells the user when asked.
void MainWindow::OnIconEvent(std::unique_ptr<IconPayload> payload) {
    AnimeGroup* group = FindGroup(payload->animeUrl);
    if (!payload->templateId.empty()) {
        if (payload->error == foldericon::Error::None) {
            if (group != nullptr && group->iconTemplate != payload->templateId) {
                group->iconTemplate = payload->templateId;
                Persist();
            }
            if (payload->announce) {
                ShowNotice(Str(STR_ICON_APPLIED));
            }
        } else {
            StringId text = payload->error == foldericon::Error::NoMagick   ? STR_ICON_NO_MAGICK
                            : payload->error == foldericon::Error::NoAssets ? STR_ICON_NO_ASSETS
                                                                             : STR_ICON_FAILED;
            std::wstring message = Str(text);
            if (!payload->detail.empty()) {
                message += L"\n" + Widen(payload->detail);
            }
            ShowNotice(message.c_str());
        }
    }
    if (payload->aniyomi && payload->announce) {
        ShowNotice(Str(payload->aniyomiOk ? STR_ANIME_ANIYOMI_DONE : STR_ICON_FAILED));
    }
}

// The folder the episodes of an anime go to, empty when it has none.
std::wstring MainWindow::FolderOfAnime(const std::string& url) const {
    for (const DownloadItem& item : items_) {
        if (item.animeUrl == url) {
            return FolderOf(item.outPath);
        }
    }
    return std::wstring();
}

// Dresses the folder of a new anime up the way the settings ask, with the
// recipe the user picked on the way in, if any.
void MainWindow::DecorateFolder(const std::string& url, const std::string& chosenTemplate) {
    std::string templateId;
    if (settings_.folderIcons) {
        templateId = chosenTemplate.empty() ? settings_.folderTemplate : chosenTemplate;
    }
    if (templateId.empty() && !settings_.aniyomi) {
        return;
    }
    ApplyIcon(url, templateId, settings_.aniyomi, false);
}

// Renders and applies an icon, writes the Aniyomi files, or both, off the
// interface thread.
void MainWindow::ApplyIcon(const std::string& url, const std::string& templateId, bool aniyomi,
                           bool announce) {
    std::wstring folder = FolderOfAnime(url);
    std::vector<uint8_t> poster = ReadBytes(PosterPath(url));
    if (folder.empty() || poster.empty()) {
        if (announce) {
            ShowNotice(Str(STR_ICON_NO_FOLDER));
        }
        return;
    }

    HWND window = hwnd_;
    std::thread([window, url, templateId, aniyomi, announce, folder, poster] {
        auto* payload = new IconPayload();
        payload->animeUrl = url;
        payload->templateId = templateId;
        payload->aniyomi = aniyomi;
        payload->announce = announce;
        if (!templateId.empty()) {
            payload->error = foldericon::Apply(folder, poster, templateId, &payload->detail);
        }
        if (aniyomi) {
            payload->aniyomiOk = foldericon::AdaptForAniyomi(folder, poster);
        }
        if (!PostMessageW(window, kIconEvent, 0, reinterpret_cast<LPARAM>(payload))) {
            delete payload;
        }
    }).detach();
}

// Keeps the poster a worker thread fetched.
void MainWindow::OnPosterEvent(std::unique_ptr<PosterPayload> payload) {
    AnimeGroup* group = FindGroup(payload->animeUrl);
    if (payload->bytes.empty() || group == nullptr) {
        return;
    }
    if (group->posterUrl != payload->posterUrl) {
        group->posterUrl = payload->posterUrl;
        Persist();
    }
    WriteBytes(PosterPath(payload->animeUrl), payload->bytes);
    sidebar_.SetPoster(payload->animeUrl, payload->bytes);
    if (group->iconTemplate.empty()) {
        DecorateFolder(payload->animeUrl, std::string());
    }
}

// Routes what the categories tree reports: painting, clicks, folds.
LRESULT MainWindow::OnSidebarNotify(NMHDR* notify) {
    switch (notify->code) {
    case NM_CUSTOMDRAW:
        return sidebar_.OnCustomDraw(reinterpret_cast<NMTVCUSTOMDRAW*>(notify));
    case TVN_SELCHANGINGW: {
        const SidebarNode* node =
            sidebar_.NodeOf(reinterpret_cast<NMTREEVIEWW*>(notify)->itemNew.hItem);
        return node != nullptr && node->kind == SidebarNodeKind::Separator ? TRUE : FALSE;
    }
    case TVN_SELCHANGEDW:
        if (!sidebar_.Busy()) {
            OnSidebarSelect(sidebar_.NodeOf(reinterpret_cast<NMTREEVIEWW*>(notify)->itemNew.hItem));
        }
        return 0;
    case TVN_ITEMEXPANDEDW: {
        auto* view = reinterpret_cast<NMTREEVIEWW*>(notify);
        const SidebarNode* node = sidebar_.NodeOf(view->itemNew.hItem);
        if (!sidebar_.Busy() && node != nullptr && node->kind == SidebarNodeKind::Anime) {
            if (AnimeGroup* group = FindGroup(node->animeUrl)) {
                group->expanded = view->action == TVE_EXPAND;
                Persist();
            }
        }
        return 0;
    }
    case NM_RCLICK:
        OnSidebarContext();
        return 1;
    default:
        return 0;
    }
}

// Filters the list to what the chosen row stands for.
void MainWindow::OnSidebarSelect(const SidebarNode* node) {
    if (node == nullptr) {
        return;
    }
    ListFilter filter;
    switch (node->kind) {
    case SidebarNodeKind::Anime:
    case SidebarNodeKind::Episode:
        filter.kind = ListFilter::Kind::Anime;
        filter.animeUrl = node->animeUrl;
        break;
    case SidebarNodeKind::QueueMain:
        filter.kind = ListFilter::Kind::QueueMain;
        break;
    case SidebarNodeKind::QueueScheduler:
        filter.kind = ListFilter::Kind::QueueScheduler;
        break;
    default:
        break;
    }
    if (filter.kind != filter_.kind || filter.animeUrl != filter_.animeUrl) {
        filter_ = filter;
        FillList();
    }
    if (node->kind == SidebarNodeKind::Episode) {
        int row = downloads_.RowOf(node->itemId);
        if (row >= 0) {
            ListView_SetItemState(downloads_.Handle(), -1, 0, LVIS_SELECTED);
            ListView_SetItemState(downloads_.Handle(), row, LVIS_SELECTED | LVIS_FOCUSED,
                                  LVIS_SELECTED | LVIS_FOCUSED);
            ListView_EnsureVisible(downloads_.Handle(), row, FALSE);
        }
    }
    UpdateActions();
}

// Shows the menu of the anime or the episode under the pointer.
void MainWindow::OnSidebarContext() {
    POINT screen = {};
    GetCursorPos(&screen);
    TVHITTESTINFO hit = {};
    hit.pt = screen;
    ScreenToClient(sidebar_.Handle(), &hit.pt);
    HTREEITEM item = TreeView_HitTest(sidebar_.Handle(), &hit);
    const SidebarNode* node = sidebar_.NodeOf(item);
    if (node == nullptr) {
        return;
    }
    TreeView_SelectItem(sidebar_.Handle(), item);
    std::string url = node->animeUrl;

    if (node->kind == SidebarNodeKind::Anime) {
        AnimeMenuOptions options;
        options.templates = foldericon::TemplateIds();
        if (const AnimeGroup* group = FindGroup(url)) {
            options.currentTemplate = group->iconTemplate;
        }
        std::wstring folder = FolderOfAnime(url);
        options.offerAniyomi = !folder.empty() && !foldericon::HasAniyomiFiles(folder);

        int command = ShowAnimeContextMenu(hwnd_, screen.x, screen.y, options);
        if (command >= ID_ICON_TEMPLATE_FIRST &&
            command < ID_ICON_TEMPLATE_FIRST + static_cast<int>(options.templates.size())) {
            ApplyIcon(url, options.templates[static_cast<size_t>(command - ID_ICON_TEMPLATE_FIRST)],
                      false, true);
            return;
        }
        switch (command) {
        case ID_ANIME_OPEN:
            OpenAnime(url);
            break;
        case ID_ANIME_OPEN_FOLDER:
            OpenAnimeFolder(url);
            break;
        case ID_ANIME_ANIYOMI:
            ApplyIcon(url, std::string(), true, true);
            break;
        case ID_ANIME_DELETE:
            DeleteAnime(url);
            break;
        default:
            break;
        }
    } else if (node->kind == SidebarNodeKind::Episode) {
        int command = ShowDownloadsContextMenu(hwnd_, screen.x, screen.y);
        if (command != 0) {
            OnCommand(command);
        }
    }
}

// The group of an anime, or nothing.
AnimeGroup* MainWindow::FindGroup(const std::string& url) {
    for (AnimeGroup& group : groups_) {
        if (group.url == url) {
            return &group;
        }
    }
    return nullptr;
}

// Whether the list shows an item under the current filter.
bool MainWindow::Visible(const DownloadItem& item) const {
    switch (filter_.kind) {
    case ListFilter::Kind::Anime:
        return item.animeUrl == filter_.animeUrl;
    case ListFilter::Kind::QueueScheduler:
        return false;
    default:
        return true;
    }
}

// Rebuilds the list from the items the filter lets through.
void MainWindow::FillList() {
    downloads_.Clear();
    for (const DownloadItem& item : items_) {
        if (Visible(item)) {
            downloads_.Upsert(item);
        }
    }
}

// Hands the model to the categories panel.
void MainWindow::RebuildSidebar() {
    sidebar_.Rebuild(groups_, items_);
}

// Drops the groups no item refers to any more, and their posters.
void MainWindow::PruneGroups() {
    for (auto it = groups_.begin(); it != groups_.end();) {
        bool used = std::any_of(items_.begin(), items_.end(),
                                [&](const DownloadItem& item) { return item.animeUrl == it->url; });
        if (used) {
            ++it;
            continue;
        }
        DeleteFileW(PosterPath(it->url).c_str());
        sidebar_.DropPoster(it->url);
        it = groups_.erase(it);
    }
}

// Shows the posters kept on disk, and fetches the missing ones.
void MainWindow::LoadPosters() {
    for (const AnimeGroup& group : groups_) {
        std::vector<uint8_t> bytes = ReadBytes(PosterPath(group.url));
        if (!bytes.empty()) {
            sidebar_.SetPoster(group.url, bytes);
        } else {
            FetchPoster(group);
        }
    }
}

// Fetches the poster of an anime off the interface thread. When its address
// is not known yet, the source is asked for it first.
void MainWindow::FetchPoster(const AnimeGroup& group) {
    HWND window = hwnd_;
    Http* http = &http_;
    const AddonStore* store = &store_;
    std::string animeUrl = group.url;
    std::string posterUrl = group.posterUrl;
    std::string addonId;
    for (const DownloadItem& item : items_) {
        if (item.animeUrl == animeUrl) {
            addonId = item.addonId;
            break;
        }
    }
    if (posterUrl.empty() && addonId.empty()) {
        return;
    }

    std::thread([window, http, store, animeUrl, posterUrl, addonId]() mutable {
        if (posterUrl.empty()) {
            std::unique_ptr<Addon> addon =
                Addon::Load(store->LibraryPath(addonId), *http, store->ReadConfig(addonId));
            if (!addon) {
                return;
            }
            std::optional<nlohmann::json> details =
                addon->Call("adm_anime_details", {{"url", animeUrl}});
            if (!details || !details->is_object()) {
                return;
            }
            posterUrl = details->value("posterUrl", std::string());
            if (posterUrl.empty()) {
                return;
            }
        }
        std::map<std::string, std::string> headers{{"Referer", OriginOf(animeUrl)}};
        std::optional<std::vector<uint8_t>> bytes = http->GetBytes(posterUrl, headers);
        if (!bytes || bytes->empty()) {
            return;
        }
        auto* payload = new PosterPayload();
        payload->animeUrl = animeUrl;
        payload->posterUrl = posterUrl;
        payload->bytes = std::move(*bytes);
        if (!PostMessageW(window, kPosterEvent, 0, reinterpret_cast<LPARAM>(payload))) {
            delete payload;
        }
    }).detach();
}

// Opens every completed episode of an anime.
void MainWindow::OpenAnime(const std::string& url) {
    int opened = 0;
    for (const DownloadItem& item : items_) {
        if (item.animeUrl != url || item.status != DownloadStatus::Completed ||
            GetFileAttributesW(item.outPath.c_str()) == INVALID_FILE_ATTRIBUTES) {
            continue;
        }
        ShellExecuteW(hwnd_, L"open", item.outPath.c_str(), nullptr, nullptr, SW_SHOWNORMAL);
        ++opened;
    }
    if (opened == 0) {
        ShowNotice(Str(STR_FILE_MISSING));
    }
}

// Opens the folder of an anime.
void MainWindow::OpenAnimeFolder(const std::string& url) {
    for (const DownloadItem& item : items_) {
        if (item.animeUrl != url) {
            continue;
        }
        std::wstring folder = FolderOf(item.outPath);
        if (GetFileAttributesW(folder.c_str()) == INVALID_FILE_ATTRIBUTES) {
            break;
        }
        ShellExecuteW(hwnd_, L"open", folder.c_str(), nullptr, nullptr, SW_SHOWNORMAL);
        return;
    }
    ShowNotice(Str(STR_FOLDER_MISSING));
}

// Takes an anime and its episodes out of the queue, and off the disk when asked.
void MainWindow::DeleteAnime(const std::string& url) {
    AnimeGroup* group = FindGroup(url);
    if (group == nullptr) {
        return;
    }
    HINSTANCE instance = reinterpret_cast<HINSTANCE>(GetWindowLongPtrW(hwnd_, GWLP_HINSTANCE));
    Confirm confirm{STR_CONFIRM_DELETE_ANIME_TITLE, STR_CONFIRM_DELETE_ANIME_MSG, STR_ANIME_DELETE,
                    STR_CONFIRM_DELETE_FILES};
    wchar_t text[512] = {};
    swprintf(text, ARRAYSIZE(text), Str(STR_CONFIRM_DELETE_ANIME_MSG), Widen(group->title).c_str());
    confirm.text = text;
    if (!ShowConfirm(hwnd_, instance, &confirm)) {
        return;
    }

    std::wstring folder;
    for (auto it = items_.begin(); it != items_.end();) {
        if (it->animeUrl != url) {
            ++it;
            continue;
        }
        downloader_.Cancel(it->id);
        if (confirm.checked) {
            DeleteFileW(it->outPath.c_str());
            folder = FolderOf(it->outPath);
        }
        downloads_.Remove(it->id);
        it = items_.erase(it);
    }
    if (!folder.empty()) {
        RemoveDirectoryW(folder.c_str());
    }
    PruneGroups();
    Persist();
    RebuildSidebar();
    UpdateActions();
}

// Applies what the engine reports to the item and its row.
void MainWindow::OnDownloadEvent(std::unique_ptr<DownloadEvent> event) {
    DownloadItem* item = Find(event->id);
    if (item == nullptr) {
        return;
    }
    // A stop that lands after the user already restarted the item is stale.
    if (event->status == DownloadStatus::Stopped && downloader_.Holds(item->id)) {
        return;
    }

    bool statusChanged = item->status != event->status;
    item->status = event->status;
    item->done = event->done;
    if (event->total > 0) {
        item->total = event->total;
    }
    item->fraction = event->fraction;
    item->speed = event->speed;
    item->error = event->error;
    item->detail = event->detail;
    if (!event->address.empty()) {
        item->address = event->address;
    }
    if (!event->outPath.empty()) {
        item->outPath = event->outPath;
    }
    if (IsActive(event->status)) {
        item->lastTry = std::time(nullptr);
    }
    if (event->status == DownloadStatus::Completed) {
        item->fraction = 1.0;
        item->speed = 0.0;
    }

    Refresh(*item);
    if (statusChanged) {
        Persist();
        RebuildSidebar();
        UpdateActions();
    }
}

// The item behind an id, or nothing.
DownloadItem* MainWindow::Find(uint64_t id) {
    for (DownloadItem& item : items_) {
        if (item.id == id) {
            return &item;
        }
    }
    return nullptr;
}

// What the engine needs to run an item.
DownloadTask MainWindow::TaskOf(const DownloadItem& item) const {
    DownloadTask task;
    task.id = item.id;
    task.addonId = item.addonId;
    task.pageUrl = item.pageUrl;
    task.player = item.player;
    task.outPath = item.outPath;
    return task;
}

// Redraws the row of an item.
void MainWindow::Refresh(const DownloadItem& item) {
    if (Visible(item)) {
        downloads_.Upsert(item);
    } else {
        downloads_.Remove(item.id);
    }
}

// Records the queue on disk.
void MainWindow::Persist() {
    queue::State state;
    state.items = items_;
    state.groups = groups_;
    queue::Save(state);
}

// Lights the actions that apply to the selection, greys the others.
void MainWindow::UpdateActions() {
    bool canResume = false;
    bool canStop = false;
    bool anyActive = false;
    std::vector<uint64_t> selected = downloads_.Selected();
    for (const DownloadItem& item : items_) {
        bool chosen = std::find(selected.begin(), selected.end(), item.id) != selected.end();
        if (IsActive(item.status)) {
            anyActive = true;
            canStop = canStop || chosen;
        }
        if (chosen && (item.status == DownloadStatus::Stopped ||
                       item.status == DownloadStatus::Failed)) {
            canResume = true;
        }
    }
    bool anySelected = !selected.empty();
    bool anyItem = !items_.empty();

    struct Action {
        int command;
        bool enabled;
    };
    const Action actions[] = {
        {ID_FILE_START, canResume},
        {ID_FILE_STOP, canStop},
        {ID_FILE_REDOWNLOAD, anySelected},
        {ID_FILE_REMOVE, anySelected},
        {ID_DOWNLOAD_STOP_ALL, anyActive},
        {ID_DOWNLOAD_DELETE_ALL, anyItem},
        {ID_DOWNLOAD_REMOVE_COMPLETED, anyItem},
    };
    HMENU menu = GetMenu(hwnd_);
    for (const Action& action : actions) {
        toolbar_.Enable(action.command, action.enabled);
        EnableMenuItem(menu, action.command,
                       MF_BYCOMMAND | (action.enabled ? MF_ENABLED : MF_GRAYED));
    }
}

// Hands an item to the engine, from its parts or from nothing.
void MainWindow::StartItem(DownloadItem& item, bool fresh) {
    if (fresh) {
        downloader_.Cancel(item.id);
        paths::RemoveTree(paths::PartsDir(item.id));
        DeleteFileW(item.outPath.c_str());
        item.done = 0;
        item.total = 0;
    }
    item.status = DownloadStatus::Queued;
    item.fraction = -1.0;
    item.speed = 0.0;
    item.error = DownloadError::None;
    item.detail.clear();
    Refresh(item);
    downloader_.Start(TaskOf(item));
}

// Continues the stopped and failed items of the selection.
void MainWindow::ResumeSelected() {
    for (uint64_t id : downloads_.Selected()) {
        DownloadItem* item = Find(id);
        if (item != nullptr && (item->status == DownloadStatus::Stopped ||
                                item->status == DownloadStatus::Failed)) {
            StartItem(*item, false);
        }
    }
    Persist();
    UpdateActions();
}

// Stops the running items of the selection, keeping their parts.
void MainWindow::StopSelected() {
    for (uint64_t id : downloads_.Selected()) {
        DownloadItem* item = Find(id);
        if (item != nullptr && IsActive(item->status)) {
            downloader_.Pause(id);
        }
    }
}

// Starts the selection over from nothing.
void MainWindow::RedownloadSelected() {
    for (uint64_t id : downloads_.Selected()) {
        DownloadItem* item = Find(id);
        if (item != nullptr) {
            StartItem(*item, true);
        }
    }
    Persist();
    UpdateActions();
}

// Takes the selection out of the queue, and off the disk when asked.
void MainWindow::RemoveSelected() {
    std::vector<uint64_t> selected = downloads_.Selected();
    if (selected.empty()) {
        ShowNotice(Str(STR_NO_SELECTION));
        return;
    }
    HINSTANCE instance = reinterpret_cast<HINSTANCE>(GetWindowLongPtrW(hwnd_, GWLP_HINSTANCE));
    Confirm confirm{STR_CONFIRM_DELETE_TITLE, STR_CONFIRM_DELETE_MSG, STR_TB_REMOVE,
                    STR_CONFIRM_DELETE_FILES};
    if (!ShowConfirm(hwnd_, instance, &confirm)) {
        return;
    }
    for (uint64_t id : selected) {
        DownloadItem* item = Find(id);
        if (item == nullptr) {
            continue;
        }
        downloader_.Cancel(id);
        if (confirm.checked) {
            DeleteFileW(item->outPath.c_str());
        }
        downloads_.Remove(id);
        items_.erase(std::remove_if(items_.begin(), items_.end(),
                                    [&](const DownloadItem& i) { return i.id == id; }),
                     items_.end());
    }
    PruneGroups();
    Persist();
    RebuildSidebar();
    UpdateActions();
}

// Stops every running item, keeping the parts.
void MainWindow::StopAll() {
    HINSTANCE instance = reinterpret_cast<HINSTANCE>(GetWindowLongPtrW(hwnd_, GWLP_HINSTANCE));
    Confirm confirm{STR_CONFIRM_STOP_ALL_TITLE, STR_CONFIRM_STOP_ALL_MSG, STR_TB_STOP_ALL,
                    STR_COUNT};
    if (ShowConfirm(hwnd_, instance, &confirm)) {
        downloader_.PauseAll();
    }
}

// Empties the queue, dropping the parts of what was running.
void MainWindow::DeleteAll() {
    HINSTANCE instance = reinterpret_cast<HINSTANCE>(GetWindowLongPtrW(hwnd_, GWLP_HINSTANCE));
    Confirm confirm{STR_CONFIRM_DELETE_ALL_TITLE, STR_CONFIRM_DELETE_ALL_MSG, STR_TB_REMOVE_ALL,
                    STR_COUNT};
    if (!ShowConfirm(hwnd_, instance, &confirm)) {
        return;
    }
    downloader_.CancelAll();
    for (const DownloadItem& item : items_) {
        paths::RemoveTree(paths::PartsDir(item.id));
    }
    items_.clear();
    downloads_.Clear();
    PruneGroups();
    Persist();
    RebuildSidebar();
    UpdateActions();
}

// Takes the completed items out of the queue.
void MainWindow::RemoveCompleted() {
    int removed = 0;
    for (auto it = items_.begin(); it != items_.end();) {
        if (it->status == DownloadStatus::Completed) {
            downloads_.Remove(it->id);
            it = items_.erase(it);
            ++removed;
        } else {
            ++it;
        }
    }
    PruneGroups();
    Persist();
    RebuildSidebar();
    UpdateActions();

    wchar_t message[128] = {};
    swprintf(message, ARRAYSIZE(message), Str(STR_COMPLETED_REMOVED), removed);
    ShowNotice(message);
}

// Opens the file of the first selected item, or the folder that holds it.
void MainWindow::OpenSelected(bool folder) {
    std::vector<uint64_t> selected = downloads_.Selected();
    if (selected.empty()) {
        return;
    }
    const DownloadItem* item = Find(selected.front());
    if (item == nullptr) {
        return;
    }
    if (folder) {
        std::wstring arguments = L"/select,\"" + item->outPath + L"\"";
        ShellExecuteW(hwnd_, L"open", L"explorer.exe", arguments.c_str(), nullptr, SW_SHOWNORMAL);
        return;
    }
    if (item->status != DownloadStatus::Completed ||
        GetFileAttributesW(item->outPath.c_str()) == INVALID_FILE_ATTRIBUTES) {
        ShowNotice(Str(STR_FILE_NOT_READY));
        return;
    }
    ShellExecuteW(hwnd_, L"open", item->outPath.c_str(), nullptr, nullptr, SW_SHOWNORMAL);
}

// The index of the skin the settings name, or -1 for the icon font.
int MainWindow::ChosenSkin() const {
    if (settings_.toolbarSkin == kFluentSkin) {
        return -1;
    }
    for (size_t i = 0; i < skins_.size(); ++i) {
        bool named = !settings_.toolbarSkin.empty() && Narrow(skins_[i].name) == settings_.toolbarSkin;
        bool fallback = settings_.toolbarSkin.empty() && skins_[i].isDefault;
        if (named || fallback) {
            return static_cast<int>(i);
        }
    }
    return -1;
}

// Dresses the toolbar with a skin, or the icon font for -1, and remembers it.
void MainWindow::ChooseSkin(int index) {
    const ToolbarSkin* skin =
        index >= 0 && index < static_cast<int>(skins_.size()) ? &skins_[static_cast<size_t>(index)]
                                                               : nullptr;
    toolbar_.SetSkin(skin, ActiveTheme());
    menuBar_.SetToolbarSkin(skin != nullptr ? index : -1);
    settings_.toolbarSkin = skin != nullptr ? Narrow(skin->name) : std::string(kFluentSkin);
    settings::Save(settings_);
    Relayout();
}

// Shows a short message in the notice dialog.
void MainWindow::ShowNotice(const wchar_t* message) {
    HINSTANCE instance = reinterpret_cast<HINSTANCE>(GetWindowLongPtrW(hwnd_, GWLP_HINSTANCE));
    ::ShowNotice(hwnd_, instance, message);
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
    ShowNotice(message);
}

// Dispatches menu, toolbar and context menu commands.
void MainWindow::OnCommand(int commandId) {
    HINSTANCE instance = reinterpret_cast<HINSTANCE>(GetWindowLongPtrW(hwnd_, GWLP_HINSTANCE));

    switch (commandId) {
    case ID_TASK_ADD:
        OnAddDownload();
        break;
    case ID_FILE_START:
        ResumeSelected();
        break;
    case ID_FILE_STOP:
        StopSelected();
        break;
    case ID_FILE_REDOWNLOAD:
        RedownloadSelected();
        break;
    case ID_FILE_REMOVE:
        RemoveSelected();
        break;
    case ID_DOWNLOAD_STOP_ALL:
        StopAll();
        break;
    case ID_DOWNLOAD_DELETE_ALL:
        DeleteAll();
        break;
    case ID_DOWNLOAD_REMOVE_COMPLETED:
        RemoveCompleted();
        break;
    case ID_CTX_OPEN:
        OpenSelected(false);
        break;
    case ID_CTX_OPEN_FOLDER:
        OpenSelected(true);
        break;
    case ID_DOWNLOAD_SEARCH:
        ShowSearchDialog(hwnd_, instance);
        break;
    case ID_VIEW_SETTINGS:
        if (ShowSettingsDialog(hwnd_, instance, &settings_)) {
            settings::Save(settings_);
        }
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
        ShowAddonsDialog(hwnd_, instance, store_, http_);
        break;
    case ID_VIEW_CATEGORIES:
        sidebarVisible_ = !sidebarVisible_;
        menuBar_.SetCategoriesChecked(sidebarVisible_);
        sidebar_.SetVisible(sidebarVisible_);
        Relayout();
        break;
    case ID_MODE_DARK:
    case ID_MODE_LIGHT:
    case ID_MODE_SYSTEM:
        themeCommand_ = commandId;
        ActiveTheme().SetMode(commandId == ID_MODE_DARK    ? ThemeMode::Dark
                       : commandId == ID_MODE_LIGHT ? ThemeMode::Light
                                                    : ThemeMode::System);
        menuBar_.SetTheme(commandId);
        ApplyTheme();
        settings_.theme = commandId == ID_MODE_DARK    ? "dark"
                          : commandId == ID_MODE_LIGHT ? "light"
                                                       : "system";
        settings::Save(settings_);
        break;
    case ID_TOOLBAR_FLUENT:
        ChooseSkin(-1);
        break;
    case ID_LANG_EN:
    case ID_LANG_FR:
        languageCommand_ = commandId;
        ::SetLanguage(commandId == ID_LANG_EN ? Language::English : Language::French);
        Retranslate();
        settings_.language = commandId == ID_LANG_EN ? "en" : "fr";
        settings::Save(settings_);
        break;
    case ID_TASK_QUIT:
        DestroyWindow(hwnd_);
        break;
    default:
        if (commandId >= ID_TOOLBAR_SKIN_FIRST &&
            commandId < ID_TOOLBAR_SKIN_FIRST + static_cast<int>(skins_.size())) {
            ChooseSkin(commandId - ID_TOOLBAR_SKIN_FIRST);
            break;
        }
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
}
