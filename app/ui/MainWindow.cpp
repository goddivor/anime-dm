#include "ui/MainWindow.h"

#include <commctrl.h>
#include <commdlg.h>
#include <shellapi.h>
#include <uxtheme.h>
#include <windowsx.h>

#include <algorithm>
#include <ctime>
#include <filesystem>
#include <fstream>
#include <regex>
#include <thread>

#include "core/Addon.h"
#include "core/Autostart.h"
#include "core/Bridge.h"
#include "core/BridgeProtocol.h"
#include "core/Digest.h"
#include "core/Export.h"
#include "core/Import.h"
#include "core/FolderIcon.h"
#include "core/Paths.h"
#include "core/Queue.h"
#include "core/Text.h"
#include "core/Url.h"
#include "ui/AddDialog.h"
#include "ui/AddonsDialog.h"
#include "ui/Commands.h"
#include "ui/ColumnsDialog.h"
#include "ui/ConfirmDialog.h"
#include "ui/ContextMenu.h"
#include "ui/FileIcons.h"
#include "ui/FilePicker.h"
#include "ui/HelpDialogs.h"
#include "ui/NoticeDialog.h"
#include "ui/Resource.h"
#include "ui/SchedulerDialog.h"
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
constexpr UINT kOutsideAdd = WM_APP + 23;
constexpr UINT kFollowEvent = WM_APP + 24;
constexpr UINT kImportEvent = WM_APP + 25;
constexpr UINT kAddDone = WM_APP + 26;
constexpr UINT kBatchReady = WM_APP + 27;
constexpr UINT kTrayMessage = WM_APP + 28;
constexpr UINT kTrayIcon = 1;
constexpr UINT_PTR kScheduleTimer = 7;
constexpr UINT kScheduleTickMs = 30000;
constexpr int kNameColumn = 0;
constexpr int kStatusColumn = 2;
constexpr int kIconGap = 4;  // around the picture of a file type

// An address handed in from outside, with the episode wanted on it.
struct Handed {
    std::string url;
    std::string episode;
};

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

// What a worker thread resolved out of an imported file.
// The addresses of a batch, gathered by anime on a worker thread.
struct BatchPayload {
    Grouping grouping;
};

struct ImportPayload {
    ImportResult result;
};

// What a worker thread brings back from the page of a followed anime.
struct FollowPayload {
    std::string animeUrl;
    bool ok = false;
    std::vector<AddRequestEpisode> episodes;
};

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
    if (hwnd_ == nullptr) {
        return false;
    }
    RestorePlacement();
    return true;
}

// Puts the window back where the last session left it, when that place is
// still on a screen; the default frame stays otherwise.
void MainWindow::RestorePlacement() {
    if (settings_.windowWidth <= 0 || settings_.windowHeight <= 0) {
        return;
    }
    RECT frame = {settings_.windowX, settings_.windowY, settings_.windowX + settings_.windowWidth,
                  settings_.windowY + settings_.windowHeight};
    if (MonitorFromRect(&frame, MONITOR_DEFAULTTONULL) == nullptr) {
        return;
    }
    WINDOWPLACEMENT placement = {};
    placement.length = sizeof(placement);
    GetWindowPlacement(hwnd_, &placement);
    placement.rcNormalPosition = frame;
    placement.showCmd = SW_HIDE;
    SetWindowPlacement(hwnd_, &placement);
    startMaximized_ = settings_.windowMaximized;
}

// Keeps the frame of the window and the panel beside the list for the next
// session: written at once, so that a session cut short keeps them too.
void MainWindow::SavePlacement() {
    if (!placementReady_) {
        return;
    }
    WINDOWPLACEMENT placement = {};
    placement.length = sizeof(placement);
    if (!GetWindowPlacement(hwnd_, &placement)) {
        return;
    }
    const RECT& frame = placement.rcNormalPosition;
    settings_.windowX = frame.left;
    settings_.windowY = frame.top;
    settings_.windowWidth = frame.right - frame.left;
    settings_.windowHeight = frame.bottom - frame.top;
    settings_.windowMaximized =
        placement.showCmd == SW_SHOWMAXIMIZED ||
        (placement.showCmd == SW_SHOWMINIMIZED && (placement.flags & WPF_RESTORETOMAXIMIZED) != 0);
    settings_.sidebarWidth = sidebarWidth_;
    settings_.sidebarVisible = sidebarVisible_;
    settings::Save(settings_);
}

// Makes the window visible and forces an initial paint, maximised when the
// last session left it so.
void MainWindow::Show(int cmdShow) {
    AddTrayIcon();
    placementReady_ = true;
    if (cmdShow == SW_HIDE) {
        return;
    }
    if (startMaximized_ && (cmdShow == SW_SHOWNORMAL || cmdShow == SW_SHOWDEFAULT)) {
        cmdShow = SW_SHOWMAXIMIZED;
    }
    ShowWindow(hwnd_, cmdShow);
    UpdateWindow(hwnd_);
    shown_ = true;
}

// --- the icon beside the clock ---------------------------------------------

// Puts the icon of the application in the notification area.
void MainWindow::AddTrayIcon() {
    HINSTANCE instance = reinterpret_cast<HINSTANCE>(GetWindowLongPtrW(hwnd_, GWLP_HINSTANCE));
    NOTIFYICONDATAW data = {};
    data.cbSize = sizeof(data);
    data.hWnd = hwnd_;
    data.uID = kTrayIcon;
    data.uFlags = NIF_ICON | NIF_MESSAGE | NIF_TIP | NIF_SHOWTIP;
    data.uCallbackMessage = kTrayMessage;
    data.hIcon = static_cast<HICON>(LoadImageW(instance, MAKEINTRESOURCEW(IDI_APP), IMAGE_ICON,
                                               GetSystemMetrics(SM_CXSMICON),
                                               GetSystemMetrics(SM_CYSMICON), LR_DEFAULTCOLOR));
    lstrcpynW(data.szTip, L"Anime Download Manager", ARRAYSIZE(data.szTip));
    Shell_NotifyIconW(NIM_ADD, &data);
    data.uVersion = NOTIFYICON_VERSION_4;
    Shell_NotifyIconW(NIM_SETVERSION, &data);
    UpdateTrayTip();
}

void MainWindow::RemoveTrayIcon() {
    NOTIFYICONDATAW data = {};
    data.cbSize = sizeof(data);
    data.hWnd = hwnd_;
    data.uID = kTrayIcon;
    Shell_NotifyIconW(NIM_DELETE, &data);
}

// Says in the tooltip of the icon how many downloads are running.
void MainWindow::UpdateTrayTip() {
    int running = static_cast<int>(std::count_if(
        items_.begin(), items_.end(), [](const DownloadItem& item) { return IsActive(item.status); }));
    NOTIFYICONDATAW data = {};
    data.cbSize = sizeof(data);
    data.hWnd = hwnd_;
    data.uID = kTrayIcon;
    data.uFlags = NIF_TIP | NIF_SHOWTIP;
    if (running > 0) {
        swprintf(data.szTip, ARRAYSIZE(data.szTip), Str(STR_TRAY_TIP_ACTIVE), running);
    } else {
        lstrcpynW(data.szTip, L"Anime Download Manager", ARRAYSIZE(data.szTip));
    }
    Shell_NotifyIconW(NIM_MODIFY, &data);
}

// Tells that a download is complete, in a notification of the system, when
// the window is not there to show it.
void MainWindow::NotifyDone(const DownloadItem& item) {
    bool seen = IsWindowVisible(hwnd_) && !IsIconic(hwnd_) && GetForegroundWindow() == hwnd_;
    if (seen) {
        return;
    }
    NOTIFYICONDATAW data = {};
    data.cbSize = sizeof(data);
    data.hWnd = hwnd_;
    data.uID = kTrayIcon;
    data.uFlags = NIF_INFO;
    data.dwInfoFlags = NIIF_INFO;
    lstrcpynW(data.szInfoTitle, Str(STR_TRAY_DONE), ARRAYSIZE(data.szInfoTitle));
    lstrcpynW(data.szInfo, FileNameOf(item.outPath).c_str(), ARRAYSIZE(data.szInfo));
    Shell_NotifyIconW(NIM_MODIFY, &data);
}

// Brings the window back from the notification area, or from the taskbar.
void MainWindow::RestoreFromTray() {
    if (!IsWindowVisible(hwnd_)) {
        ShowWindow(hwnd_, !shown_ && startMaximized_ ? SW_SHOWMAXIMIZED : SW_SHOW);
        shown_ = true;
    }
    if (IsIconic(hwnd_)) {
        ShowWindow(hwnd_, SW_RESTORE);
    }
    SetForegroundWindow(hwnd_);
}

// A click on the icon brings the window back; the right button opens its
// menu; a click on a notification shows the window too.
void MainWindow::OnTrayMessage(UINT event, int x, int y) {
    switch (event) {
    case NIN_SELECT:
    case NIN_KEYSELECT:
    case NIN_BALLOONUSERCLICK:
        RestoreFromTray();
        break;
    case WM_CONTEXTMENU:
        ShowTrayMenu(x, y);
        break;
    default:
        break;
    }
}

// The menu of the icon, as IDM has one: the window, the adds, the queue,
// the settings, and the only way out of the application. Each entry is as
// grey as the same entry of the menu bar.
void MainWindow::ShowTrayMenu(int x, int y) {
    struct Entry {
        int command;
        const wchar_t* label;
    };
    const Entry top[] = {
        {ID_TASK_ADD, Str(STR_TASK_ADD)},
        {ID_TASK_BATCH, Str(STR_TASK_BATCH)},
        {0, nullptr},
        {ID_QUEUE_START_MAIN, Str(STR_TRAY_START)},
        {ID_DOWNLOAD_STOP_ALL, Str(STR_DL_STOP_ALL)},
        {ID_DOWNLOAD_SCHEDULE, Str(STR_DL_SCHEDULE)},
        {0, nullptr},
    };
    const Entry bottom[] = {
        {ID_VIEW_SETTINGS, Str(STR_TB_OPTIONS)},
        {ID_VIEW_ADDONS, Str(STR_VIEW_ADDONS)},
        {0, nullptr},
        {ID_TASK_QUIT, Str(STR_TASK_QUIT)},
    };

    HMENU bar = GetMenu(hwnd_);
    auto append = [bar](HMENU menu, const Entry& entry) {
        if (entry.command == 0) {
            AppendMenuW(menu, MF_SEPARATOR, 0, nullptr);
            return true;
        }
        UINT state = GetMenuState(bar, static_cast<UINT>(entry.command), MF_BYCOMMAND);
        bool grey = state != static_cast<UINT>(-1) && (state & (MF_GRAYED | MF_DISABLED)) != 0;
        AppendMenuW(menu, MF_STRING | (grey ? MF_GRAYED : 0), static_cast<UINT_PTR>(entry.command),
                    entry.label);
        return !grey;
    };

    HMENU menu = CreatePopupMenu();
    AppendMenuW(menu, MF_STRING, ID_TRAY_RESTORE, Str(STR_TRAY_RESTORE));
    SetMenuDefaultItem(menu, ID_TRAY_RESTORE, FALSE);
    AppendMenuW(menu, MF_SEPARATOR, 0, nullptr);
    for (const Entry& entry : top) {
        append(menu, entry);
    }
    for (const Entry& entry : bottom) {
        append(menu, entry);
    }

    // The menu of an icon closes when the user clicks elsewhere only if its
    // window stands in front, and a message after it lets it go.
    SetForegroundWindow(hwnd_);
    UINT flags = TPM_RIGHTBUTTON | TPM_RETURNCMD | TPM_NONOTIFY;
    UINT chosen = static_cast<UINT>(TrackPopupMenu(menu, flags, x, y, 0, hwnd_, nullptr));
    PostMessageW(hwnd_, WM_NULL, 0, 0);
    DestroyMenu(menu);
    if (chosen != 0) {
        OnCommand(static_cast<int>(chosen));
    }
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
    // Explorer starting again forgets every icon beside the clock.
    static const UINT taskbarCreated = RegisterWindowMessageW(L"TaskbarCreated");
    if (msg == taskbarCreated) {
        AddTrayIcon();
        return 0;
    }
    switch (msg) {
    case kTrayMessage:
        OnTrayMessage(LOWORD(lParam), GET_X_LPARAM(wParam), GET_Y_LPARAM(wParam));
        return 0;
    case WM_CLOSE:
        // The close box hides the window: downloads, schedules and the
        // browser extension go on, and the icon beside the clock brings the
        // window back or quits.
        if (settings_.closeToTray) {
            ShowWindow(hwnd_, SW_HIDE);
            return 0;
        }
        break;
    case WM_ENDSESSION:
        if (wParam) {
            DestroyWindow(hwnd_);
        }
        return 0;
    case WM_CREATE:
        OnCreate();
        return 0;
    case WM_SIZE:
        Relayout();
        // Maximising and restoring end without a move loop to report them.
        if (wParam == SIZE_MAXIMIZED || (wParam == SIZE_RESTORED && wasMaximized_)) {
            SavePlacement();
        }
        if (wParam != SIZE_MINIMIZED) {
            wasMaximized_ = wParam == SIZE_MAXIMIZED;
        }
        return 0;
    case WM_EXITSIZEMOVE:
        SavePlacement();
        return 0;
    case WM_COMMAND:
        OnCommand(LOWORD(wParam));
        return 0;
    case WM_TIMER:
        if (wParam == kScheduleTimer) {
            OnScheduleTick();
        }
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
    case kFollowEvent:
        OnFollowEvent(std::unique_ptr<FollowPayload>(reinterpret_cast<FollowPayload*>(lParam)));
        return 0;
    case kImportEvent:
        OnImportEvent(std::unique_ptr<ImportPayload>(reinterpret_cast<ImportPayload*>(lParam)));
        return 0;
    case kOutsideAdd: {
        std::unique_ptr<Handed> handed(reinterpret_cast<Handed*>(lParam));
        std::vector<std::string> episodes;
        if (!handed->episode.empty()) {
            episodes.push_back(handed->episode);
        }
        OnAddDownload(handed->url, episodes);
        return 0;
    }
    case kAddDone:
        OnAddAccepted(std::unique_ptr<AddRequest>(reinterpret_cast<AddRequest*>(lParam)));
        return 0;
    case kBatchReady:
        OnBatchReady(std::unique_ptr<BatchPayload>(reinterpret_cast<BatchPayload*>(lParam)));
        return 0;
    case WM_COPYDATA: {
        // What the native host of the browser extension, or a second
        // instance, hands over: a JSON document marked as ours.
        auto* data = reinterpret_cast<const COPYDATASTRUCT*>(lParam);
        if (data == nullptr || data->dwData != bridge::kCopyDataMark) {
            break;
        }
        std::string text(static_cast<const char*>(data->lpData), data->cbData);
        nlohmann::json message = nlohmann::json::parse(text, nullptr, false);
        std::string kind = message.is_object() ? message.value("kind", std::string()) : "";
        if (kind == "add") {
            AddFromOutside(message.value("url", std::string()),
                           message.value("episode", std::string()));
        } else if (kind == "show") {
            RestoreFromTray();
        }
        return TRUE;
    }
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
    case WM_MOUSEWHEEL:
    case WM_MOUSEHWHEEL: {
        // The wheel is delivered to the window that has the focus; the user
        // expects it to move what lies under the pointer.
        static thread_local bool forwarding = false;
        POINT at = {GET_X_LPARAM(lParam), GET_Y_LPARAM(lParam)};
        HWND under = WindowFromPoint(at);
        if (!forwarding && under != nullptr && under != hwnd_ && IsChild(hwnd_, under)) {
            forwarding = true;
            LRESULT result = SendMessageW(under, msg, wParam, lParam);
            forwarding = false;
            return result;
        }
        break;
    }
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
            } else if (notify->code == LVN_COLUMNCLICK) {
                OnColumnClick(downloads_.ColumnAt(reinterpret_cast<NMLISTVIEW*>(lParam)->iSubItem));
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
    if (settings_.sidebarWidth > 0) {
        sidebarWidth_ = settings_.sidebarWidth;
    }
    sidebarVisible_ = settings_.sidebarVisible;
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
    sidebar_.SetVisible(sidebarVisible_);

    downloads_.Create(hwnd_, instance);
    downloads_.SetWidths(settings_.columnWidths);
    downloads_.SetShown(settings_.columns);
    downloads_.OnColumnsResized([this] {
        settings_.columnWidths = downloads_.Widths();
        settings::Save(settings_);
    });
    ApplyUiFont();

    queue::State state = queue::Load();
    items_ = std::move(state.items);
    groups_ = std::move(state.groups);
    // Nothing runs yet: an item the last session left running, or a session
    // cut short left waiting, is stopped until the user or a queue starts it.
    for (DownloadItem& item : items_) {
        nextId_ = std::max(nextId_, item.id + 1);
        if (IsActive(item.status)) {
            item.status = DownloadStatus::Stopped;
            item.speed = 0.0;
        }
    }
    PruneGroups();
    FillList();
    RebuildSidebar();
    LoadPosters();
    downloader_.Attach(hwnd_, kDownloadEvent);
    schedule::Load(&scheduler_);
    follows_ = follow::Load();
    SetTimer(hwnd_, kScheduleTimer, kScheduleTickMs, nullptr);

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
    PublishSources();
}

// Tells the browsers where the native host is, and writes what the extension
// needs to know of the sources; the libraries are asked off this thread.
void MainWindow::PublishSources() {
    bridge::RegisterHost(settings_.browsers);
    const AddonStore* store = &store_;
    Http* http = &http_;
    bridge::Panel panel;
    panel.mode = settings_.panelMode;
    panel.onPage = settings_.panelOnPage;
    panel.onLinks = settings_.panelOnLinks;
    std::thread([store, http, panel] { bridge::WriteSources(*store, *http, panel); }).detach();
}

// Pushes onto the system and the engine what the options decide.
void MainWindow::ApplySettings() {
    autostart::Set(settings_.startWithWindows);
    PublishSources();
    downloader_.SetLimits(settings_.maxRunning, settings_.connections);
}

// Stops the transfers, keeps their parts, and records the queue as it stands.
void MainWindow::OnDestroy() {
    SavePlacement();
    CloseAddWindows();
    RemoveTrayIcon();
    KillTimer(hwnd_, kScheduleTimer);
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
        Refresh(item);
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

    // The header answers in its own coordinates, and the list slides the
    // header window sideways to scroll: every rectangle is brought back into
    // the coordinates of the list before anything is drawn from it.
    int columns = Header_GetItemCount(header);
    for (int column = 0; column < columns; ++column) {
        RECT item = {};
        if (!Header_GetItemRect(header, column, &item)) {
            continue;
        }
        MapWindowPoints(header, list, reinterpret_cast<POINT*>(&item), 2);
        MoveToEx(dc, item.right - 1, item.bottom, nullptr);
        LineTo(dc, item.right - 1, client.bottom);
    }

    // One rule under every row, existing or not, so the grid reaches the
    // bottom of the list the way the built-in one does, and fills an empty
    // list as IDM's does.
    RECT first = {};
    int top = 0;
    int height = 0;
    if (ListView_GetItemCount(list) > 0 && ListView_GetItemRect(list, 0, &first, LVIR_BOUNDS)) {
        top = first.top;
        height = first.bottom - first.top;
    } else {
        RECT bar = {};
        GetWindowRect(header, &bar);
        MapWindowPoints(nullptr, list, reinterpret_cast<POINT*>(&bar), 2);
        top = bar.bottom;
        height = HIWORD(ListView_ApproximateViewRect(list, -1, -1, 2)) -
                 HIWORD(ListView_ApproximateViewRect(list, -1, -1, 1));
    }
    for (int y = top + height - 1; height > 0 && y < client.bottom; y += height) {
        MoveToEx(dc, client.left, y, nullptr);
        LineTo(dc, client.right, y);
    }

    SelectObject(dc, previous);
    DeleteObject(pen);
    return CDRF_DODEFAULT;
}

// A click on a column sorts the rows by it; a second click turns the order
// round, the way IDM does.
void MainWindow::OnColumnClick(int column) {
    if (column == sortColumn_) {
        sortAscending_ = !sortAscending_;
    } else {
        sortColumn_ = column;
        sortAscending_ = true;
    }
    downloads_.SetSortMark(sortColumn_, sortAscending_);
    ApplySort();
}

namespace {
// The seconds a running transfer still needs, or -1 when nothing says.
double TimeLeft(const DownloadItem& item) {
    if (item.status != DownloadStatus::Downloading || item.speed <= 0.0) {
        return -1.0;
    }
    if (item.total > item.done) {
        return static_cast<double>(item.total - item.done) / item.speed;
    }
    if (item.fraction > 0.0 && item.done > 0) {
        return (static_cast<double>(item.done) / item.fraction - static_cast<double>(item.done)) /
               item.speed;
    }
    return -1.0;
}
}  // namespace

// Reorders the rows by the chosen column, when one is chosen.
void MainWindow::ApplySort() {
    if (sortColumn_ < 0) {
        return;
    }
    int column = sortColumn_;
    bool ascending = sortAscending_;
    downloads_.Sort([this, column, ascending](uint64_t first, uint64_t second) {
        const DownloadItem* a = Find(first);
        const DownloadItem* b = Find(second);
        if (a == nullptr || b == nullptr) {
            return false;
        }
        const DownloadItem& lower = ascending ? *a : *b;
        const DownloadItem& upper = ascending ? *b : *a;
        switch (column) {
        case 0:
            return lstrcmpiW(FileNameOf(lower.outPath).c_str(),
                             FileNameOf(upper.outPath).c_str()) < 0;
        case 1:
            return (lower.total > 0 ? lower.total : lower.done) <
                   (upper.total > 0 ? upper.total : upper.done);
        case 2:
            return lower.status != upper.status ? lower.status < upper.status
                                                : lower.fraction < upper.fraction;
        case 3:
            return TimeLeft(lower) < TimeLeft(upper);
        case 4:
            return lower.speed < upper.speed;
        case 5:
            return lower.lastTry < upper.lastTry;
        case 7:
            return lstrcmpiW(FolderOf(lower.outPath).c_str(), FolderOf(upper.outPath).c_str()) < 0;
        case 8:
            return lower.pageUrl < upper.pageUrl;
        case 9:
            return lower.animeUrl < upper.animeUrl;
        default:
            return lower.addedAt < upper.addedAt;
        }
    });
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
    // The whole row lights up, picture included. The row under the pointer
    // gets the softer light of Explorer; the row the keyboard stands on, as
    // Ctrl and the arrows move it without selecting, gets a dotted outline,
    // and so does the one under the pointer.
    bool hot = !selected && row == downloads_.HotRow();
    bool focused = GetFocus() == list &&
                   (ListView_GetItemState(list, row, LVIS_FOCUSED) & LVIS_FOCUSED) != 0;
    if (selected || hot) {
        HBRUSH fill = CreateSolidBrush(selected ? colors.accent : colors.hover);
        FillRect(dc, &bounds, fill);
        DeleteObject(fill);
    }
    if (hot || focused) {
        RECT outline = bounds;
        outline.right = std::min<LONG>(outline.right, client.right - 1);
        DrawFocusRect(dc, &outline);
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
        MapWindowPoints(header, list, reinterpret_cast<POINT*>(&span), 2);
        RECT cell = {span.left, bounds.top, span.right, bounds.bottom};
        if (cell.right <= client.left || cell.left >= client.right) {
            continue;
        }
        int shown = downloads_.ColumnAt(column);
        if (shown == kStatusColumn && DrawProgressCell(dc, cell, id, selected)) {
            continue;
        }
        wchar_t text[512] = {};
        ListView_GetItemText(list, row, column, text, ARRAYSIZE(text));
        RECT label = cell;
        label.left += 6;
        label.right -= 6;
        if (shown == kNameColumn && icon >= 0) {
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
    SavePlacement();
}

// Drops a drag the system interrupted, leaving the panels as they were.
void MainWindow::CancelSplitterDrag() {
    DrawTracker(trackX_);
    LockWindowUpdate(nullptr);
    draggingSplitter_ = false;
}

// Opens an add window on an address handed in from outside. The request
// arrives inside a message the sender waits on, so the window opens from a
// message of its own. The add window stands on its own: neither another
// add window nor a dialog of the main window keeps it from opening.
void MainWindow::AddFromOutside(const std::string& url, const std::string& episode) {
    if (url.empty()) {
        MessageBeep(MB_ICONWARNING);
        return;
    }
    auto* copy = new Handed{url, episode};
    if (!PostMessageW(hwnd_, kOutsideAdd, 0, reinterpret_cast<LPARAM>(copy))) {
        delete copy;
    }
}

// Opens an add window of its own; what the user confirms there comes back
// through kAddDone.
void MainWindow::OnAddDownload(const std::string& initialUrl,
                               const std::vector<std::string>& initialEpisodes,
                               const std::string& sourceId) {
    HINSTANCE instance = reinterpret_cast<HINSTANCE>(GetWindowLongPtrW(hwnd_, GWLP_HINSTANCE));
    OpenAddWindow(instance, store_, http_, settings_, {hwnd_, kAddDone}, initialUrl,
                  initialEpisodes, sourceId);
}

// Queues the episodes an add window confirmed, and remembers its folder
// when the user asked to.
void MainWindow::OnAddAccepted(std::unique_ptr<AddRequest> request) {
    request->animeTitle = TidyText(request->animeTitle);
    AddEpisodes(*request, QueueKind::Main, !request->later);
    if (request->rememberPath != settings_.rememberPath ||
        (request->rememberPath && Narrow(request->destination) != settings_.savePath)) {
        settings_.rememberPath = request->rememberPath;
        settings_.savePath = request->rememberPath ? Narrow(request->destination) : std::string();
        settings::Save(settings_);
    }
    if (!request->posterBytes.empty()) {
        DecorateFolder(request->animeUrl, request->folderTemplate);
    }
    ApplySort();
    Persist();
    RebuildSidebar();
    UpdateActions();
}

// Reads every address the clipboard holds and gathers them by anime off the
// interface thread; one add window per anime follows, through kBatchReady.
void MainWindow::OnBatchAdd() {
    std::wstring text;
    if (IsClipboardFormatAvailable(CF_UNICODETEXT) && OpenClipboard(hwnd_)) {
        if (HANDLE data = GetClipboardData(CF_UNICODETEXT)) {
            if (auto* locked = static_cast<const wchar_t*>(GlobalLock(data))) {
                text = locked;
                GlobalUnlock(data);
            }
        }
        CloseClipboard();
    }
    std::vector<std::string> addresses;
    std::wregex pattern(LR"(https?://[^\s"'<>]+)");
    for (std::wsregex_iterator it(text.begin(), text.end(), pattern), end; it != end; ++it) {
        std::string address = Narrow(it->str());
        bool seen = std::any_of(addresses.begin(), addresses.end(), [&](const std::string& other) {
            return url::SamePage(other, address);
        });
        if (!seen) {
            addresses.push_back(address);
        }
    }
    if (addresses.empty()) {
        ShowNotice(Str(STR_BATCH_EMPTY));
        return;
    }
    const AddonStore* store = &store_;
    Http* http = &http_;
    HWND window = hwnd_;
    std::thread([addresses, store, http, window] {
        auto* payload = new BatchPayload();
        payload->grouping = importing::Group(addresses, *store, *http);
        if (!PostMessageW(window, kBatchReady, 0, reinterpret_cast<LPARAM>(payload))) {
            delete payload;
        }
    }).detach();
}

// Opens one add window per anime of a batch, its source known and its
// episodes named, and says which addresses no source serves.
void MainWindow::OnBatchReady(std::unique_ptr<BatchPayload> payload) {
    for (const AddressGroup& group : payload->grouping.groups) {
        OnAddDownload(group.animeUrl, group.episodes, group.addonId);
    }
    if (payload->grouping.unknown > 0) {
        wchar_t message[256] = {};
        swprintf(message, 256, Str(STR_BATCH_UNKNOWN), payload->grouping.unknown);
        ShowNotice(message);
    }
}

// Puts the episodes of a request in the queue, under the folder of their
// anime, creating the anime group on the way; `start` hands them to the
// engine at once, otherwise they wait stopped for the queue.
void MainWindow::AddEpisodes(const AddRequest& request, QueueKind queue, bool start) {
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
        item.queue = queue;
        // Left stopped, the episode waits for Resume or for its queue to start.
        item.status = start ? DownloadStatus::Queued : DownloadStatus::Stopped;
        item.addedAt = std::time(nullptr);
        items_.push_back(item);
        Refresh(item);
        if (start) {
            downloader_.Start(TaskOf(item));
        }
    }
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

// Selects an item in the list, lifting the filter when it hides it.
void MainWindow::RevealItem(uint64_t id) {
    if (downloads_.RowOf(id) < 0) {
        filter_ = ListFilter();
        FillList();
    }
    int row = downloads_.RowOf(id);
    if (row < 0) {
        return;
    }
    ListView_SetItemState(downloads_.Handle(), -1, 0, LVIS_SELECTED);
    ListView_SetItemState(downloads_.Handle(), row, LVIS_SELECTED | LVIS_FOCUSED,
                          LVIS_SELECTED | LVIS_FOCUSED);
    ListView_EnsureVisible(downloads_.Handle(), row, FALSE);
    SetFocus(downloads_.Handle());
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
        options.followed = std::any_of(follows_.begin(), follows_.end(),
                                       [&](const FollowedAnime& f) { return f.animeUrl == url; });
        for (const DownloadItem& item : items_) {
            if (item.animeUrl == url) {
                options.anyMain = options.anyMain || item.queue == QueueKind::Main;
                options.anyScheduler = options.anyScheduler || item.queue == QueueKind::Scheduler;
            }
        }

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
        case ID_ANIME_FOLLOW:
            FollowAnime(url);
            break;
        case ID_ANIME_QUEUE_MAIN:
            MoveAnimeTo(url, QueueKind::Main);
            break;
        case ID_ANIME_QUEUE_SCHEDULER:
            MoveAnimeTo(url, QueueKind::Scheduler);
            break;
        default:
            break;
        }
    } else if (node->kind == SidebarNodeKind::Episode) {
        RunDownloadsMenu(screen.x, screen.y);
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
    case ListFilter::Kind::QueueMain:
        return item.queue == QueueKind::Main;
    case ListFilter::Kind::QueueScheduler:
        return item.queue == QueueKind::Scheduler;
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
    if (!event->players.empty()) {
        item->players = event->players;
    }
    if (IsActive(event->status)) {
        item->lastTry = std::time(nullptr);
    }
    if (event->status == DownloadStatus::Completed) {
        item->fraction = 1.0;
        item->speed = 0.0;
    }

    Refresh(*item);
    if (statusChanged && item->status == DownloadStatus::Completed) {
        NotifyDone(*item);
    }
    if (statusChanged) {
        Persist();
        RebuildSidebar();
        UpdateActions();
        if (!IsActive(item->status)) {
            FinishScheduledRun(item->queue);
        }
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
    bool anyCompleted = false;
    bool canStartQueue[2] = {false, false};
    bool canStopQueue[2] = {false, false};
    std::vector<uint64_t> selected = downloads_.Selected();
    for (const DownloadItem& item : items_) {
        bool chosen = std::find(selected.begin(), selected.end(), item.id) != selected.end();
        int slot = item.queue == QueueKind::Scheduler ? 1 : 0;
        bool halted = item.status == DownloadStatus::Stopped ||
                      item.status == DownloadStatus::Failed;
        if (IsActive(item.status)) {
            anyActive = true;
            canStop = canStop || chosen;
            canStopQueue[slot] = true;
        }
        if (halted) {
            canResume = canResume || chosen;
            canStartQueue[slot] = true;
        }
        anyCompleted = anyCompleted || item.status == DownloadStatus::Completed;
    }
    bool anySelected = !selected.empty();
    bool anyItem = !items_.empty();

    struct Action {
        int command;
        bool enabled;
    };
    // The entries that are not written yet stay grey rather than opening a
    // notice that they will come.
    const Action actions[] = {
        {ID_FILE_START, canResume},
        {ID_FILE_STOP, canStop},
        {ID_FILE_REDOWNLOAD, anySelected},
        {ID_FILE_REMOVE, anySelected},
        {ID_DOWNLOAD_STOP_ALL, anyActive},
        {ID_DOWNLOAD_DELETE_ALL, anyItem},
        {ID_DOWNLOAD_REMOVE_COMPLETED, anyCompleted},
        {ID_DOWNLOAD_SEARCH, anyItem},
        {ID_QUEUE_START_MAIN, canStartQueue[0]},
        {ID_QUEUE_START_SCHEDULER, canStartQueue[1]},
        {ID_QUEUE_STOP_MAIN, canStopQueue[0]},
        {ID_QUEUE_STOP_SCHEDULER, canStopQueue[1]},
        {ID_TASK_EXPORT_ADM, anyItem},
        {ID_TASK_EXPORT_TXT, anyItem},
        {ID_TASK_EXPORT_JSON, anyItem},
        {ID_TASK_EXPORT_SHEET, anyItem},
        {ID_TASK_EXPORT_XLSX, anyItem},
        {ID_TASK_EXPORT_ODS, anyItem},
        {ID_HELP_HELP, false},
        {ID_HELP_UPDATE, false},
    };
    HMENU menu = GetMenu(hwnd_);
    for (const Action& action : actions) {
        toolbar_.Enable(action.command, action.enabled);
        EnableMenuItem(menu, action.command,
                       MF_BYCOMMAND | (action.enabled ? MF_ENABLED : MF_GRAYED));
    }
    for (int command = ID_SORT_DATE_ADDED; command <= ID_SORT_PARENT_PAGE; ++command) {
        EnableMenuItem(menu, command, MF_BYCOMMAND | (anyItem ? MF_ENABLED : MF_GRAYED));
    }
    // A sub-menu whose every entry is grey goes grey itself.
    for (int child : {ID_TASK_EXPORT_ADM, ID_SORT_NAME, ID_QUEUE_START_MAIN,
                      ID_QUEUE_STOP_MAIN}) {
        GreyEmptyPopup(menu, child);
    }
    UpdateTrayTip();
}

// Greys the entry that opens the sub-menu holding `child` when none of that
// sub-menu's entries is enabled, and lights it up again otherwise. True once
// the sub-menu is found.
bool MainWindow::GreyEmptyPopup(HMENU menu, int child) {
    int count = GetMenuItemCount(menu);
    for (int position = 0; position < count; ++position) {
        HMENU sub = GetSubMenu(menu, position);
        if (sub == nullptr) {
            continue;
        }
        int entries = GetMenuItemCount(sub);
        bool holds = false;
        for (int index = 0; index < entries && !holds; ++index) {
            holds = GetMenuItemID(sub, index) == static_cast<UINT>(child);
        }
        if (!holds) {
            if (GreyEmptyPopup(sub, child)) {
                return true;
            }
            continue;
        }
        bool any = false;
        for (int index = 0; index < entries && !any; ++index) {
            UINT state = GetMenuState(sub, static_cast<UINT>(index), MF_BYPOSITION);
            any = (state & (MF_GRAYED | MF_DISABLED | MF_SEPARATOR)) == 0;
        }
        EnableMenuItem(menu, static_cast<UINT>(position),
                       MF_BYPOSITION | (any ? MF_ENABLED : MF_GRAYED));
        return true;
    }
    return false;
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

// Hands every stopped or failed item of a queue to the engine.
void MainWindow::StartQueue(QueueKind queue) {
    for (DownloadItem& item : items_) {
        if (item.queue == queue && (item.status == DownloadStatus::Stopped ||
                                    item.status == DownloadStatus::Failed)) {
            StartItem(item, false);
        }
    }
    Persist();
    UpdateActions();
}

// Stops every running item of a queue, keeping the parts.
void MainWindow::StopQueue(QueueKind queue) {
    for (const DownloadItem& item : items_) {
        if (item.queue == queue && IsActive(item.status)) {
            downloader_.Pause(item.id);
        }
    }
}

// Puts the selection in a queue; the categories panel follows.
void MainWindow::MoveSelectedTo(QueueKind queue) {
    for (uint64_t id : downloads_.Selected()) {
        if (DownloadItem* item = Find(id)) {
            item->queue = queue;
        }
    }
    Persist();
    FillList();
    RebuildSidebar();
    UpdateActions();
}

// Puts every episode of an anime in a queue.
void MainWindow::MoveAnimeTo(const std::string& url, QueueKind queue) {
    for (DownloadItem& item : items_) {
        if (item.animeUrl == url) {
            item.queue = queue;
        }
    }
    Persist();
    FillList();
    RebuildSidebar();
    UpdateActions();
}

// Opens the scheduler window on the scheduler queue, or on the queue the
// panel shows.
void MainWindow::OpenScheduler() {
    HINSTANCE instance = reinterpret_cast<HINSTANCE>(GetWindowLongPtrW(hwnd_, GWLP_HINSTANCE));
    SchedulerScreen screen;
    screen.scheduler = &scheduler_;
    screen.follows = &follows_;
    screen.items = &items_;
    screen.choices = FollowChoices();
    screen.initial = filter_.kind == ListFilter::Kind::QueueMain ? QueueKind::Main
                                                                  : QueueKind::Scheduler;
    screen.run = [this](QueueKind queue, bool start) {
        if (start) {
            StartQueue(queue);
        } else {
            StopQueue(queue);
        }
    };
    if (ShowSchedulerDialog(hwnd_, instance, &screen)) {
        schedule::Save(scheduler_);
        follow::Save(follows_);
        CheckFollows();
    }
}

// The kind of file behind each entry of the Export and Import submenus.
FileKind KindOf(int format) {
    static const StringId kLabels[6] = {STR_KIND_ADM,  STR_KIND_TEXT, STR_KIND_JSON,
                                        STR_KIND_CSV,  STR_KIND_XLSX, STR_KIND_ODS};
    exporting::Format kind = static_cast<exporting::Format>(format);
    return {Str(kLabels[format]), exporting::Extension(kind)};
}

// Writes the selection, or the whole list when nothing is selected, to a
// file of the chosen shape.
void MainWindow::ExportList(int format) {
    std::vector<uint64_t> selected = downloads_.Selected();
    std::vector<DownloadItem> chosen;
    for (const DownloadItem& item : items_) {
        if (selected.empty() ||
            std::find(selected.begin(), selected.end(), item.id) != selected.end()) {
            chosen.push_back(item);
        }
    }
    if (chosen.empty()) {
        ShowNotice(Str(STR_EXPORT_NOTHING));
        return;
    }
    FileKind kind = KindOf(format);
    std::wstring path = PickSaveFile(hwnd_, kind, std::wstring(L"anime-dm.") + kind.extension);
    if (path.empty()) {
        return;
    }
    std::string text = exporting::Render(static_cast<exporting::Format>(format), chosen, groups_);
    bool written = false;
    {
        std::ofstream file(std::filesystem::path(path), std::ios::binary | std::ios::trunc);
        written = file && (file << text) && file.flush();
    }
    if (!written) {
        ShowNotice(Str(STR_EXPORT_FAILED));
        return;
    }
    wchar_t message[128] = {};
    swprintf(message, 128, Str(STR_EXPORT_DONE), static_cast<int>(chosen.size()));
    ShowNotice(message);
}

// Reads a file of the chosen shape and resolves its pages off the interface
// thread; the episodes come back through kImportEvent.
void MainWindow::ImportList(int format) {
    std::wstring path = PickOpenFile(hwnd_, KindOf(format));
    if (path.empty()) {
        return;
    }
    std::ifstream file(std::filesystem::path(path), std::ios::binary);
    std::string text((std::istreambuf_iterator<char>(file)), std::istreambuf_iterator<char>());
    std::vector<ImportEntry> entries = importing::Parse(text);
    if (entries.empty()) {
        ShowNotice(Str(STR_IMPORT_EMPTY));
        return;
    }
    const AddonStore* store = &store_;
    Http* http = &http_;
    HWND window = hwnd_;
    std::thread([entries, store, http, window] {
        auto* payload = new ImportPayload();
        payload->result = importing::Resolve(entries, *store, *http);
        if (!PostMessageW(window, kImportEvent, 0, reinterpret_cast<LPARAM>(payload))) {
            delete payload;
        }
    }).detach();
}

// Queues what an import resolved, stopped in the main queue, leaving aside
// the episodes the list already holds, and tells the user the count.
void MainWindow::OnImportEvent(std::unique_ptr<ImportPayload> payload) {
    int added = 0;
    int present = 0;
    for (const ImportedAnime& anime : payload->result.animes) {
        AddRequest request;
        request.addonId = anime.addonId;
        request.animeTitle = TidyText(anime.title);
        request.animeUrl = anime.animeUrl;
        request.posterUrl = anime.posterUrl;
        request.destination = settings_.rememberPath ? Widen(settings_.savePath) : std::wstring();
        for (const ImportedEpisode& episode : anime.episodes) {
            bool queued = std::any_of(items_.begin(), items_.end(), [&](const DownloadItem& item) {
                return url::SamePage(item.pageUrl, episode.url);
            });
            if (queued) {
                present += 1;
                continue;
            }
            AddRequestEpisode wanted;
            wanted.url = episode.url;
            wanted.name = episode.name;
            wanted.number = episode.number;
            wanted.player = episode.player;
            request.episodes.push_back(wanted);
        }
        if (!request.episodes.empty()) {
            added += static_cast<int>(request.episodes.size());
            AddEpisodes(request, QueueKind::Main, false);
        }
    }
    if (added > 0) {
        ApplySort();
        Persist();
        RebuildSidebar();
        UpdateActions();
    }
    wchar_t message[400] = {};
    swprintf(message, 400, Str(STR_IMPORT_DONE), added, present,
             payload->result.unknown + payload->result.failed);
    ShowNotice(message);
}

// The animes of the queue a follow may name, with the source and the folder
// their episodes came through.
std::vector<FollowChoice> MainWindow::FollowChoices() const {
    std::vector<FollowChoice> choices;
    for (const AnimeGroup& group : groups_) {
        FollowChoice choice;
        choice.animeUrl = group.url;
        choice.title = group.title;
        for (const DownloadItem& item : items_) {
            if (item.animeUrl == group.url) {
                choice.addonId = item.addonId;
                choice.destination = FolderOf(FolderOf(item.outPath));
                break;
            }
        }
        if (!choice.addonId.empty()) {
            choices.push_back(choice);
        }
    }
    return choices;
}

// Opens the follow dialog on an anime of the panel, or on the follow it
// already has.
void MainWindow::FollowAnime(const std::string& url) {
    HINSTANCE instance = reinterpret_cast<HINSTANCE>(GetWindowLongPtrW(hwnd_, GWLP_HINSTANCE));
    FollowScreen screen;
    screen.choices = FollowChoices();
    FollowedAnime draft;
    FollowedAnime* existing = nullptr;
    for (FollowedAnime& follow : follows_) {
        if (follow.animeUrl == url) {
            existing = &follow;
        }
    }
    if (existing != nullptr) {
        draft = *existing;
    } else {
        draft.animeUrl = url;
        std::time_t now = std::time(nullptr);
        std::tm local = {};
        localtime_s(&local, &now);
        draft.releaseDay = (local.tm_wday + 6) % 7;
        draft.releaseHour = local.tm_hour;
    }
    screen.follow = &draft;
    screen.editing = true;
    if (!ShowFollowDialog(hwnd_, instance, &screen)) {
        return;
    }
    if (existing != nullptr) {
        bool moved = existing->releaseDay != draft.releaseDay ||
                     existing->releaseHour != draft.releaseHour ||
                     existing->releaseMinute != draft.releaseMinute;
        *existing = draft;
        if (moved && existing->primed) {
            existing->nextCheck = follow::NextRelease(*existing, std::time(nullptr));
            existing->misses = 0;
        }
    } else {
        draft.nextCheck = 0;
        draft.primed = false;
        follows_.push_back(draft);
    }
    follow::Save(follows_);
    CheckFollows();
}

// Starts a check for every follow whose moment has come and that is not
// being checked already.
void MainWindow::CheckFollows() {
    std::time_t now = std::time(nullptr);
    for (const FollowedAnime& follow : follows_) {
        bool busy = std::find(checking_.begin(), checking_.end(), follow.animeUrl) !=
                    checking_.end();
        if (!busy && follow.nextCheck <= now) {
            checking_.push_back(follow.animeUrl);
            CheckFollow(follow);
        }
    }
}

// Asks the source for the episodes of a followed anime, off the interface
// thread, and posts them back.
void MainWindow::CheckFollow(const FollowedAnime& follow) {
    std::wstring library = store_.LibraryPath(follow.addonId);
    std::map<std::string, std::string> config = store_.ReadConfig(follow.addonId);
    std::string url = follow.animeUrl;
    Http* http = &http_;
    HWND window = hwnd_;
    std::thread([library, config, url, http, window] {
        auto* payload = new FollowPayload();
        payload->animeUrl = url;
        std::unique_ptr<Addon> addon = Addon::Load(library, *http, config);
        if (addon) {
            nlohmann::json input = {{"url", url}};
            if (std::optional<nlohmann::json> episodes = addon->Call("adm_episode_list", input)) {
                if (episodes->is_array()) {
                    for (const nlohmann::json& entry : *episodes) {
                        AddRequestEpisode episode;
                        episode.url = entry.value("url", std::string());
                        episode.name = entry.value("name", std::string());
                        episode.number = entry.value("number", 0.0);
                        if (!episode.url.empty()) {
                            payload->episodes.push_back(std::move(episode));
                        }
                    }
                    payload->ok = !payload->episodes.empty();
                }
            }
        }
        if (!PostMessageW(window, kFollowEvent, 0, reinterpret_cast<LPARAM>(payload))) {
            delete payload;
        }
    }).detach();
}

// Takes the episodes a check found. The first check offers the episodes
// already out that the list lacks, those after the last one downloaded ticked,
// and queues the ones the user keeps; the next checks queue whatever is new on
// their own and plan the check after.
void MainWindow::OnFollowEvent(std::unique_ptr<FollowPayload> payload) {
    checking_.erase(std::remove(checking_.begin(), checking_.end(), payload->animeUrl),
                    checking_.end());
    FollowedAnime* follow = nullptr;
    for (FollowedAnime& candidate : follows_) {
        if (candidate.animeUrl == payload->animeUrl) {
            follow = &candidate;
        }
    }
    if (follow == nullptr) {
        return;
    }
    std::time_t now = std::time(nullptr);
    if (!payload->ok) {
        follow::Plan(follow, now, false);
        follow::Save(follows_);
        return;
    }

    AddRequest request;
    request.addonId = follow->addonId;
    request.animeTitle = follow->title;
    request.animeUrl = follow->animeUrl;
    request.destination = follow->destination;
    for (const AddRequestEpisode& episode : payload->episodes) {
        bool known = std::find(follow->known.begin(), follow->known.end(), episode.url) !=
                     follow->known.end();
        bool queued = std::any_of(items_.begin(), items_.end(), [&](const DownloadItem& item) {
            return item.pageUrl == episode.url;
        });
        if (!known) {
            follow->known.push_back(episode.url);
        }
        if (episode.number > follow->lastNumber) {
            follow->lastNumber = episode.number;
        }
        if (!known && !queued && follow->primed) {
            request.episodes.push_back(episode);
        }
    }
    bool first = !follow->primed;
    if (first) {
        // Primed before the offer is shown: the clock keeps ticking behind
        // the modal window and must not start a second first check.
        follow->primed = true;
        follow->nextCheck = follow::NextRelease(*follow, now);
        follow::Save(follows_);
        FollowedAnime offered = *follow;
        OfferMissing(offered, payload->episodes, &request);
        follow = nullptr;
        for (FollowedAnime& candidate : follows_) {
            if (candidate.animeUrl == payload->animeUrl) {
                follow = &candidate;
            }
        }
    }
    bool found = !request.episodes.empty();
    if (found) {
        QueueKind queue = follow != nullptr ? follow->queue : QueueKind::Scheduler;
        bool start = follow != nullptr && follow->startAtOnce;
        AddEpisodes(request, queue, start);
        ApplySort();
        Persist();
        RebuildSidebar();
        UpdateActions();
    }
    if (!first && follow != nullptr) {
        follow::Plan(follow, now, found);
        follow::Save(follows_);
    }
}

// Offers the episodes of a newly followed anime that are out but missing
// from the list, and puts the ones the user keeps into the request. The
// episodes after the last one downloaded come ticked, the older ones not: a
// follow started in the middle of a long series does not fetch its past.
void MainWindow::OfferMissing(const FollowedAnime& follow,
                              const std::vector<AddRequestEpisode>& episodes,
                              AddRequest* request) {
    double last = -1.0;
    for (const DownloadItem& item : items_) {
        if (url::SamePage(item.animeUrl, follow.animeUrl) && item.episodeNumber > last) {
            last = item.episodeNumber;
        }
    }
    std::vector<AddRequestEpisode> missing;
    std::vector<bool> ticked;
    for (const AddRequestEpisode& episode : episodes) {
        bool listed = std::any_of(items_.begin(), items_.end(), [&](const DownloadItem& item) {
            return url::SamePage(item.pageUrl, episode.url);
        });
        if (!listed) {
            missing.push_back(episode);
            ticked.push_back(episode.number > last);
        }
    }
    if (missing.empty()) {
        return;
    }
    HINSTANCE instance = reinterpret_cast<HINSTANCE>(GetWindowLongPtrW(hwnd_, GWLP_HINSTANCE));
    std::wstring caption = std::wstring(Str(STR_FOLLOW_MISSING)) + L" : " + Widen(follow.title);
    PickEpisodes(hwnd_, instance, store_, http_, follow.addonId, caption, missing, ticked,
                 &request->episodes);
}

// Carries out what the schedules name as due, once every tick.
void MainWindow::OnScheduleTick() {
    bool changed = false;
    for (const ScheduleAction& action : scheduler_.Tick(std::time(nullptr))) {
        int slot = action.queue == QueueKind::Scheduler ? 1 : 0;
        if (action.start) {
            scheduledRun_[slot] = true;
            StartQueue(action.queue);
            changed = changed || !scheduler_.Of(action.queue).daily;
        } else {
            scheduledRun_[slot] = false;
            StopQueue(action.queue);
        }
    }
    if (changed) {
        schedule::Save(scheduler_);
    }
    CheckFollows();
}

// Once a queue the clock started has nothing left running, does what its
// schedule asks: quits, or shuts the computer down.
void MainWindow::FinishScheduledRun(QueueKind queue) {
    int slot = queue == QueueKind::Scheduler ? 1 : 0;
    if (!scheduledRun_[slot]) {
        return;
    }
    for (const DownloadItem& item : items_) {
        if (item.queue == queue && IsActive(item.status)) {
            return;
        }
    }
    scheduledRun_[slot] = false;
    switch (scheduler_.Of(queue).whenDone) {
    case QueueSchedule::WhenDone::Quit:
        PostMessageW(hwnd_, WM_COMMAND, ID_TASK_QUIT, 0);
        break;
    case QueueSchedule::WhenDone::Shutdown: {
        HANDLE token = nullptr;
        if (OpenProcessToken(GetCurrentProcess(), TOKEN_ADJUST_PRIVILEGES | TOKEN_QUERY, &token)) {
            TOKEN_PRIVILEGES privileges = {};
            privileges.PrivilegeCount = 1;
            LookupPrivilegeValueW(nullptr, SE_SHUTDOWN_NAME, &privileges.Privileges[0].Luid);
            privileges.Privileges[0].Attributes = SE_PRIVILEGE_ENABLED;
            AdjustTokenPrivileges(token, FALSE, &privileges, 0, nullptr, nullptr);
            CloseHandle(token);
        }
        Persist();
        ExitWindowsEx(EWX_SHUTDOWN | EWX_POWEROFF, SHTDN_REASON_MAJOR_APPLICATION |
                                                       SHTDN_REASON_MINOR_OTHER |
                                                       SHTDN_REASON_FLAG_PLANNED);
        break;
    }
    default:
        break;
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
    case ID_TASK_BATCH:
        OnBatchAdd();
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
    case ID_DOWNLOAD_SCHEDULE:
        OpenScheduler();
        break;
    case ID_SORT_DATE_ADDED:
    case ID_SORT_NAME:
    case ID_SORT_SIZE:
    case ID_SORT_STATUS:
    case ID_SORT_TIME_LEFT:
    case ID_SORT_SPEED:
    case ID_SORT_LAST_TRY:
    case ID_SORT_LOCATION:
    case ID_SORT_ADDRESS:
    case ID_SORT_PARENT_PAGE: {
        // The menu lists the date first, the columns keep it seventh.
        static const int kColumnOf[] = {6, 0, 1, 2, 3, 4, 5, 7, 8, 9};
        OnColumnClick(kColumnOf[commandId - ID_SORT_DATE_ADDED]);
        break;
    }
    case ID_TASK_EXPORT_ADM:
    case ID_TASK_EXPORT_TXT:
    case ID_TASK_EXPORT_JSON:
    case ID_TASK_EXPORT_SHEET:
    case ID_TASK_EXPORT_XLSX:
    case ID_TASK_EXPORT_ODS:
        ExportList(commandId - ID_TASK_EXPORT_ADM);
        break;
    case ID_TASK_IMPORT_ADM:
    case ID_TASK_IMPORT_TXT:
    case ID_TASK_IMPORT_JSON:
    case ID_TASK_IMPORT_SHEET:
    case ID_TASK_IMPORT_XLSX:
    case ID_TASK_IMPORT_ODS:
        ImportList(commandId - ID_TASK_IMPORT_ADM);
        break;
    case ID_QUEUE_START_MAIN:
        StartQueue(QueueKind::Main);
        break;
    case ID_QUEUE_START_SCHEDULER:
        StartQueue(QueueKind::Scheduler);
        break;
    case ID_QUEUE_STOP_MAIN:
        StopQueue(QueueKind::Main);
        break;
    case ID_QUEUE_STOP_SCHEDULER:
        StopQueue(QueueKind::Scheduler);
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
        ShowSearchDialog(hwnd_, instance, items_, [this](uint64_t id) { RevealItem(id); });
        break;
    case ID_VIEW_SETTINGS:
        if (ShowSettingsDialog(hwnd_, instance, &settings_)) {
            settings::Save(settings_);
            ApplySettings();
        }
        break;
    case ID_HELP_SHORTCUTS:
        ShowShortcutsDialog(hwnd_, instance);
        break;
    case ID_HELP_WEBSITE:
        OpenWebsite(hwnd_);
        break;
    case ID_HELP_ABOUT:
        ShowAboutDialog(hwnd_, instance);
        break;
    case ID_VIEW_ADDONS:
        ShowAddonsDialog(hwnd_, instance, store_, http_);
        PublishSources();
        break;
    case ID_VIEW_CATEGORIES:
        sidebarVisible_ = !sidebarVisible_;
        menuBar_.SetCategoriesChecked(sidebarVisible_);
        sidebar_.SetVisible(sidebarVisible_);
        Relayout();
        SavePlacement();
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
    case ID_VIEW_COLUMNS:
        ChooseColumns();
        break;
    case ID_FONT_SELECT:
        ChooseUiFont();
        break;
    case ID_FONT_RESET:
        settings_.fontFace.clear();
        settings::Save(settings_);
        ApplyUiFont();
        break;
    case ID_TASK_QUIT:
        DestroyWindow(hwnd_);
        break;
    case ID_TRAY_RESTORE:
        RestoreFromTray();
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

    RunDownloadsMenu(x, y);
}

// Shows the downloads menu for the selection and carries out its choice.
// The players offered are those of the first selected item; a choice
// applies to the whole selection.
void MainWindow::RunDownloadsMenu(int x, int y) {
    DownloadMenuOptions options;
    std::vector<uint64_t> selected = downloads_.Selected();
    if (!selected.empty()) {
        if (const DownloadItem* first = Find(selected.front())) {
            options.players = first->players;
            options.currentPlayer = first->player;
            options.inScheduler = first->queue == QueueKind::Scheduler;
        }
    }
    for (uint64_t id : selected) {
        const DownloadItem* item = Find(id);
        if (item == nullptr) {
            continue;
        }
        options.canOpen = options.canOpen || item->status == DownloadStatus::Completed;
        options.canResume = options.canResume || item->status == DownloadStatus::Stopped ||
                            item->status == DownloadStatus::Failed ||
                            item->status == DownloadStatus::Completed;
        options.canStop = options.canStop || IsActive(item->status);
    }

    int command = ShowDownloadsContextMenu(hwnd_, x, y, options);
    if (command == 0) {
        return;
    }
    if (command == ID_CTX_QUEUE_MAIN) {
        MoveSelectedTo(QueueKind::Main);
    } else if (command == ID_CTX_QUEUE_SCHEDULER) {
        MoveSelectedTo(QueueKind::Scheduler);
    } else if (command == ID_CTX_PLAYER_AUTO) {
        ResumeSelectedWith(std::string());
    } else if (command >= ID_PLAYER_FIRST &&
               command < ID_PLAYER_FIRST + static_cast<int>(options.players.size())) {
        ResumeSelectedWith(options.players[static_cast<size_t>(command - ID_PLAYER_FIRST)]);
    } else {
        OnCommand(command);
    }
}

// Restarts the stopped and failed items of the selection through a player,
// or through whatever the source prefers when the name is empty. A finished
// episode starts over from nothing: what a player served may stop halfway
// through when another has the whole of it.
void MainWindow::ResumeSelectedWith(const std::string& player) {
    for (uint64_t id : downloads_.Selected()) {
        DownloadItem* item = Find(id);
        if (item == nullptr) {
            continue;
        }
        bool finished = item->status == DownloadStatus::Completed;
        bool halted = item->status == DownloadStatus::Stopped ||
                      item->status == DownloadStatus::Failed;
        if (!finished && !halted) {
            continue;
        }
        item->player = player;
        StartItem(*item, finished);
    }
    Persist();
    UpdateActions();
}

// Applies the font of the interface to the child controls: the one the user
// chose, or the system message font for a native look.
void MainWindow::ApplyUiFont() {
    LOGFONTW font = {};
    if (!settings_.fontFace.empty()) {
        HDC dc = GetDC(hwnd_);
        font.lfHeight = -MulDiv(settings_.fontSize, GetDeviceCaps(dc, LOGPIXELSY), 720);
        ReleaseDC(hwnd_, dc);
        font.lfWeight = settings_.fontWeight;
        font.lfItalic = settings_.fontItalic ? TRUE : FALSE;
        font.lfCharSet = DEFAULT_CHARSET;
        font.lfQuality = CLEARTYPE_QUALITY;
        lstrcpynW(font.lfFaceName, Widen(settings_.fontFace).c_str(), LF_FACESIZE);
    } else {
        NONCLIENTMETRICSW metrics = {};
        metrics.cbSize = sizeof(metrics);
        if (!SystemParametersInfoW(SPI_GETNONCLIENTMETRICS, sizeof(metrics), &metrics, 0)) {
            return;
        }
        font = metrics.lfMessageFont;
    }

    HFONT previous = uiFont_;
    uiFont_ = CreateFontIndirectW(&font);
    toolbar_.SetFont(uiFont_);
    SendMessageW(sidebar_.Handle(), WM_SETFONT, reinterpret_cast<WPARAM>(uiFont_), TRUE);
    SendMessageW(sidebar_.HeaderHandle(), WM_SETFONT, reinterpret_cast<WPARAM>(uiFont_), TRUE);
    SendMessageW(downloads_.Handle(), WM_SETFONT, reinterpret_cast<WPARAM>(uiFont_), TRUE);
    if (previous != nullptr) {
        DeleteObject(previous);
        Relayout();
        InvalidateRect(hwnd_, nullptr, TRUE);
    }
}

// Lets the user pick the font of the interface, starting from the one in use.
void MainWindow::ChooseUiFont() {
    LOGFONTW font = {};
    GetObjectW(uiFont_, sizeof(font), &font);
    CHOOSEFONTW choice = {};
    choice.lStructSize = sizeof(choice);
    choice.hwndOwner = hwnd_;
    choice.lpLogFont = &font;
    choice.Flags = CF_SCREENFONTS | CF_INITTOLOGFONTSTRUCT | CF_NOVERTFONTS | CF_NOSCRIPTSEL;
    if (!ChooseFontW(&choice)) {
        return;
    }
    settings_.fontFace = Narrow(font.lfFaceName);
    settings_.fontSize = choice.iPointSize;
    settings_.fontWeight = static_cast<int>(font.lfWeight);
    settings_.fontItalic = font.lfItalic != 0;
    settings::Save(settings_);
    ApplyUiFont();
}

// Lets the user choose the columns of the file list and their order, then
// rebuilds the list on them.
void MainWindow::ChooseColumns() {
    HINSTANCE instance = reinterpret_cast<HINSTANCE>(GetWindowLongPtrW(hwnd_, GWLP_HINSTANCE));
    std::vector<int> shown = downloads_.Shown();
    if (!ShowColumnsDialog(hwnd_, instance, &shown)) {
        return;
    }
    downloads_.SetShown(shown);
    FillList();
    ApplySort();
    downloads_.SetSortMark(sortColumn_, sortAscending_);
    settings_.columns = downloads_.Shown();
    settings_.columnWidths = downloads_.Widths();
    settings::Save(settings_);
}
