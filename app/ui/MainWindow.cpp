#include "ui/MainWindow.h"

#include <commctrl.h>
#include <shellapi.h>
#include <uxtheme.h>
#include <windowsx.h>

#include <algorithm>
#include <ctime>

#include "core/Paths.h"
#include "core/Queue.h"
#include "core/Text.h"
#include "ui/AddDialog.h"
#include "ui/AddonsDialog.h"
#include "ui/Commands.h"
#include "ui/ConfirmDialog.h"
#include "ui/ContextMenu.h"
#include "ui/HelpDialogs.h"
#include "ui/NoticeDialog.h"
#include "ui/SearchDialog.h"
#include "ui/SettingsDialog.h"
#include "ui/Strings.h"

namespace {
constexpr wchar_t kWindowClass[] = L"AnimeDmMainWindow";
constexpr int kSplitterWidth = 5;
constexpr int kMinSidebarWidth = 140;
constexpr int kMinListWidth = 240;
constexpr UINT kDownloadEvent = WM_APP + 20;
constexpr int kStatusColumn = 2;

// Whether the episode is a film rather than a numbered episode.
bool IsMovie(const std::string& name) {
    std::string lower = name;
    std::transform(lower.begin(), lower.end(), lower.begin(),
                   [](unsigned char c) { return static_cast<char>(std::tolower(c)); });
    return lower.find("film") != std::string::npos || lower.find("movie") != std::string::npos;
}

// The number of an episode as it appears in the file name: 001, 012, 12.5.
std::wstring EpisodeLabel(double number) {
    wchar_t text[32] = {};
    if (number == static_cast<double>(static_cast<long>(number))) {
        swprintf(text, ARRAYSIZE(text), L"%03ld", static_cast<long>(number));
    } else {
        swprintf(text, ARRAYSIZE(text), L"%.1f", number);
    }
    return text;
}

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
    case kDownloadEvent:
        OnDownloadEvent(std::unique_ptr<DownloadEvent>(reinterpret_cast<DownloadEvent*>(lParam)));
        return 0;
    case WM_CONTEXTMENU:
        OnContextMenu(reinterpret_cast<HWND>(wParam), GET_X_LPARAM(lParam), GET_Y_LPARAM(lParam));
        return 0;
    case WM_ERASEBKGND: {
        HBRUSH brush = ActiveTheme().WindowBrush();
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

    themeCommand_ = ID_MODE_SYSTEM;
    languageCommand_ = ID_LANG_FR;
    ActiveTheme().SetMode(ThemeMode::System);

    menuBar_.AttachTo(hwnd_);
    menuBar_.SetCategoriesChecked(sidebarVisible_);
    menuBar_.SetTheme(themeCommand_);
    menuBar_.SetLanguage(languageCommand_);
    toolbar_.Create(hwnd_, instance);
    sidebar_.Create(hwnd_, instance);

    downloads_.Create(hwnd_, instance);
    ApplyUiFont();

    items_ = queue::Load();
    for (const DownloadItem& item : items_) {
        nextId_ = std::max(nextId_, item.id + 1);
        downloads_.Upsert(item);
    }
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
        return CDRF_NOTIFYSUBITEMDRAW;
    case CDDS_ITEMPREPAINT | CDDS_SUBITEM:
        return DrawProgressCell(draw) ? CDRF_SKIPDEFAULT : CDRF_DODEFAULT;
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

    SelectObject(dc, previous);
    DeleteObject(pen);
    return CDRF_DODEFAULT;
}

// Paints the status cell of a running download as a bar with its percentage.
// False when the cell is not a running download, which the list draws itself.
bool MainWindow::DrawProgressCell(NMLVCUSTOMDRAW* draw) {
    if (draw->iSubItem != kStatusColumn) {
        return false;
    }
    const DownloadItem* item = Find(static_cast<uint64_t>(draw->nmcd.lItemlParam));
    if (item == nullptr || item->status != DownloadStatus::Downloading || item->fraction < 0.0) {
        return false;
    }

    HWND list = draw->nmcd.hdr.hwndFrom;
    int row = static_cast<int>(draw->nmcd.dwItemSpec);
    RECT cell = {};
    ListView_GetSubItemRect(list, row, kStatusColumn, LVIR_BOUNDS, &cell);

    const ThemeColors& colors = ActiveTheme().Colors();
    bool selected = ListView_GetItemState(list, row, LVIS_SELECTED) == LVIS_SELECTED;
    HDC dc = draw->nmcd.hdc;

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
    case CDDS_PREPAINT:
        FillRect(draw->nmcd.hdc, &draw->nmcd.rc, ActiveTheme().SurfaceBrush());
        return CDRF_NOTIFYITEMDRAW;
    case CDDS_ITEMPREPAINT: {
        const ThemeColors& colors = ActiveTheme().Colors();
        bool disabled = (draw->nmcd.uItemState & CDIS_DISABLED) != 0;
        draw->clrText = disabled ? colors.muted : colors.text;
        return TBCDRF_USECDCOLORS | TBCDRF_NOETCHEDEFFECT;
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

    int top = toolbar_.Height();
    int contentHeight = client.bottom - top;

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

    RECT rect = {};
    rect.left = sidebarWidth_;
    rect.right = sidebarWidth_ + kSplitterWidth;
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

// Starts a splitter drag when the press lands on the splitter band.
void MainWindow::OnLeftButtonDown(int x) {
    if (!sidebarVisible_) {
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

// Asks the user for an anime, then queues the episodes it picked.
void MainWindow::OnAddDownload() {
    HINSTANCE instance = reinterpret_cast<HINSTANCE>(GetWindowLongPtrW(hwnd_, GWLP_HINSTANCE));

    AddRequest request;
    if (ShowAddDialog(hwnd_, instance, store_, http_, &request) != IDOK) {
        return;
    }

    std::wstring title = SafeFileName(Widen(request.animeTitle));
    std::wstring base = request.destination.empty() ? paths::UserDownloadsDir()
                                                    : request.destination;
    std::wstring folder = base + L"\\" + title;

    for (const AddRequestEpisode& episode : request.episodes) {
        DownloadItem item;
        item.id = nextId_++;
        item.addonId = request.addonId;
        item.animeTitle = request.animeTitle;
        item.animeUrl = request.animeUrl;
        item.episodeNumber = episode.number;
        item.pageUrl = episode.url;
        item.player = episode.player;
        item.outPath = folder + L"\\" +
                       (IsMovie(episode.name)
                            ? title + L".mp4"
                            : title + L" - Ep " + EpisodeLabel(episode.number) + L".mp4");
        item.status = DownloadStatus::Queued;
        item.addedAt = std::time(nullptr);
        items_.push_back(item);
        downloads_.Upsert(item);
        downloader_.Start(TaskOf(item));
    }
    Persist();
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
    downloads_.Upsert(item);
}

// Records the queue on disk.
void MainWindow::Persist() {
    queue::Save(items_);
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
    Persist();
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
    Persist();
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
    Persist();
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
}
