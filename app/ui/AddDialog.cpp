#include "ui/AddDialog.h"

#include <commctrl.h>
#include <objbase.h>
#include <windowsx.h>
#include <shlobj.h>
#include <shobjidl.h>

#include <algorithm>
#include <memory>
#include <set>
#include <string>
#include <thread>
#include <vector>

#include "core/Addon.h"
#include "core/AddonStore.h"
#include "core/Http.h"
#include "core/Image.h"
#include "core/Text.h"
#include "ui/AddSelection.h"
#include "ui/Paint.h"
#include "ui/Resource.h"
#include "ui/Strings.h"
#include "ui/Theme.h"

namespace {

constexpr UINT kLoaded = WM_APP + 1;
constexpr UINT kHosters = WM_APP + 2;

constexpr int kSourceIcon = 32;
constexpr int kSourceCell = 44;
constexpr int kCellWidth = 42;
constexpr int kCellHeight = 28;
constexpr int kCellGap = 6;
constexpr wchar_t kAuto[] = L"Auto";

// One episode as the source described it.
struct Episode {
    double number = 0.0;
    std::string name;
    std::string url;
};

// What a load brought back from the source.
struct Listing {
    bool ok = false;
    std::string title;
    std::string posterUrl;
    std::vector<Episode> episodes;
    std::vector<uint8_t> poster;
};

// The players a source offers for one episode.
struct Players {
    int episode = -1;
    std::vector<std::string> names;
};

// What the dialog keeps for the whole of its life.
struct Screen {
    const AddonStore* store = nullptr;
    Http* http = nullptr;
    AddRequest* request = nullptr;

    std::vector<InstalledAddon> sources;
    std::vector<HBITMAP> sourceIcons;
    int source = 0;

    std::vector<std::string> playerOptions;
    std::wstring player = kAuto;
    std::map<int, std::string> playerByEpisode;

    std::vector<Episode> episodes;
    std::set<int> picked;
    std::string title;
    std::string url;
    HBITMAP poster = nullptr;

    bool listMode = false;
    bool busy = false;
    int scroll = 0;
    int columns = 1;
    int hover = -1;
};

std::wstring ReadText(HWND dialog, int control) {
    HWND field = GetDlgItem(dialog, control);
    int length = GetWindowTextLengthW(field);
    if (length <= 0) {
        return std::wstring();
    }
    std::wstring text(static_cast<size_t>(length), L'\0');
    GetWindowTextW(field, text.data(), length + 1);
    return text;
}

std::wstring DefaultDestination() {
    PWSTR folder = nullptr;
    if (FAILED(SHGetKnownFolderPath(FOLDERID_Downloads, 0, nullptr, &folder))) {
        return std::wstring();
    }
    std::wstring path(folder);
    CoTaskMemFree(folder);
    return path;
}

std::wstring PickFolder(HWND owner) {
    IFileDialog* dialog = nullptr;
    if (FAILED(CoCreateInstance(CLSID_FileOpenDialog, nullptr, CLSCTX_INPROC_SERVER,
                                IID_PPV_ARGS(&dialog)))) {
        return std::wstring();
    }

    std::wstring chosen;
    DWORD options = 0;
    if (SUCCEEDED(dialog->GetOptions(&options)) &&
        SUCCEEDED(dialog->SetOptions(options | FOS_PICKFOLDERS | FOS_FORCEFILESYSTEM)) &&
        SUCCEEDED(dialog->Show(owner))) {
        IShellItem* item = nullptr;
        if (SUCCEEDED(dialog->GetResult(&item))) {
            PWSTR path = nullptr;
            if (SUCCEEDED(item->GetDisplayName(SIGDN_FILESYSPATH, &path))) {
                chosen = path;
                CoTaskMemFree(path);
            }
            item->Release();
        }
    }
    dialog->Release();
    return chosen;
}

// The origin of a page, which image hosts ask for as a referer.
std::string OriginOf(const std::string& url) {
    size_t scheme = url.find("://");
    if (scheme == std::string::npos) {
        return std::string();
    }
    size_t slash = url.find('/', scheme + 3);
    return slash == std::string::npos ? url : url.substr(0, slash);
}

std::wstring Format(StringId id, int value) {
    wchar_t text[128] = {};
    wsprintfW(text, Str(id), value);
    return text;
}

int RoundNumber(double number) {
    return static_cast<int>(number + (number < 0 ? -0.5 : 0.5));
}

}  // namespace

// --- painting ---------------------------------------------------------------

namespace {

constexpr float kRadius = 5.0f;

HFONT FontOf(HWND control) {
    return reinterpret_cast<HFONT>(SendMessageW(control, WM_GETFONT, 0, 0));
}

// Draws one source of the strip: its icon on a tile, filled when it is the one
// the download will go through.
void DrawSource(const DRAWITEMSTRUCT& draw, const Screen& screen) {
    int index = static_cast<int>(draw.CtlID) - IDC_ADD_SOURCE_FIRST;
    if (index < 0 || static_cast<size_t>(index) >= screen.sources.size()) {
        return;
    }

    const ThemeColors& colors = ActiveTheme().Colors();
    bool chosen = index == screen.source;
    bool focused = (draw.itemState & ODS_FOCUS) != 0;

    paint::RoundedRect(draw.hDC, draw.rcItem, kRadius, chosen ? colors.accent : colors.surface,
                       chosen || focused ? colors.accent : colors.line, chosen ? 1.0f : 1.0f);

    HBITMAP icon = static_cast<size_t>(index) < screen.sourceIcons.size()
                       ? screen.sourceIcons[static_cast<size_t>(index)]
                       : nullptr;
    if (icon == nullptr) {
        return;
    }

    HDC memory = CreateCompatibleDC(draw.hDC);
    HBITMAP old = static_cast<HBITMAP>(SelectObject(memory, icon));
    BLENDFUNCTION blend = {AC_SRC_OVER, 0, 255, AC_SRC_ALPHA};
    int x = draw.rcItem.left + (draw.rcItem.right - draw.rcItem.left - kSourceIcon) / 2;
    int y = draw.rcItem.top + (draw.rcItem.bottom - draw.rcItem.top - kSourceIcon) / 2;
    AlphaBlend(draw.hDC, x, y, kSourceIcon, kSourceIcon, memory, 0, 0, kSourceIcon, kSourceIcon,
               blend);
    SelectObject(memory, old);
    DeleteDC(memory);
}

// Draws the chip that opens the player menu.
void DrawPlayer(const DRAWITEMSTRUCT& draw, const Screen& screen) {
    const ThemeColors& colors = ActiveTheme().Colors();
    bool pressed = (draw.itemState & ODS_SELECTED) != 0;

    paint::RoundedRect(draw.hDC, draw.rcItem, kRadius, pressed ? colors.hover : colors.surface,
                       colors.line);

    HFONT old = static_cast<HFONT>(SelectObject(draw.hDC, FontOf(draw.hwndItem)));
    RECT text = draw.rcItem;
    text.left += 10;
    text.right -= 20;
    std::wstring label = std::wstring(Str(STR_ADD_PLAYER_LABEL)) + L" " + screen.player;
    paint::Label(draw.hDC, text, label, colors.text, DT_LEFT | DT_VCENTER | DT_END_ELLIPSIS);

    RECT arrow = draw.rcItem;
    arrow.left = arrow.right - 18;
    paint::Label(draw.hDC, arrow, L"\u25BE", colors.text, DT_CENTER | DT_VCENTER);
    SelectObject(draw.hDC, old);
}

// Draws one half of the mode switch, the active one filled.
void DrawSegment(const DRAWITEMSTRUCT& draw, bool active, StringId label) {
    const ThemeColors& colors = ActiveTheme().Colors();
    paint::RoundedRect(draw.hDC, draw.rcItem, kRadius, active ? colors.accent : colors.surface,
                       active ? colors.accent : colors.line);

    HFONT old = static_cast<HFONT>(SelectObject(draw.hDC, FontOf(draw.hwndItem)));
    paint::Label(draw.hDC, draw.rcItem, Str(label), active ? colors.accentText : colors.text,
                 DT_CENTER | DT_VCENTER);
    SelectObject(draw.hDC, old);
}

// Draws the poster, or an empty slot until one arrives.
void DrawPoster(const DRAWITEMSTRUCT& draw, const Screen& screen) {
    const ThemeColors& colors = ActiveTheme().Colors();
    if (screen.poster == nullptr) {
        paint::RoundedRect(draw.hDC, draw.rcItem, kRadius, colors.surface, colors.line);
        return;
    }

    HDC memory = CreateCompatibleDC(draw.hDC);
    HBITMAP old = static_cast<HBITMAP>(SelectObject(memory, screen.poster));
    int width = draw.rcItem.right - draw.rcItem.left;
    int height = draw.rcItem.bottom - draw.rcItem.top;
    SetStretchBltMode(draw.hDC, HALFTONE);
    StretchBlt(draw.hDC, draw.rcItem.left, draw.rcItem.top, width, height, memory, 0, 0, width,
               height, SRCCOPY);
    SelectObject(memory, old);
    DeleteDC(memory);
}

// Draws the episode grid: one tile per episode, the picked ones filled, the ones
// carrying their own player outlined.
void DrawGrid(const DRAWITEMSTRUCT& draw, Screen& screen) {
    const ThemeColors& colors = ActiveTheme().Colors();
    RECT bounds = draw.rcItem;

    HBRUSH background = CreateSolidBrush(colors.window);
    FillRect(draw.hDC, &bounds, background);
    DeleteObject(background);

    int width = bounds.right - bounds.left;
    screen.columns = std::max(1, (width + kCellGap) / (kCellWidth + kCellGap));

    HFONT old = static_cast<HFONT>(SelectObject(draw.hDC, FontOf(draw.hwndItem)));

    for (size_t index = 0; index < screen.episodes.size(); ++index) {
        int row = static_cast<int>(index) / screen.columns - screen.scroll;
        int column = static_cast<int>(index) % screen.columns;
        if (row < 0) {
            continue;
        }

        RECT cell = {};
        cell.left = bounds.left + column * (kCellWidth + kCellGap);
        cell.top = bounds.top + row * (kCellHeight + kCellGap);
        cell.right = cell.left + kCellWidth;
        cell.bottom = cell.top + kCellHeight;
        if (cell.top >= bounds.bottom) {
            break;
        }

        bool chosen = screen.picked.count(static_cast<int>(index)) > 0;
        bool hovered = screen.hover == static_cast<int>(index);
        bool tuned = screen.playerByEpisode.count(static_cast<int>(index)) > 0;

        COLORREF fill = chosen ? colors.accent : (hovered ? colors.hover : colors.surface);
        COLORREF border = tuned ? colors.text : (chosen ? colors.accent : colors.line);
        paint::RoundedRect(draw.hDC, cell, kRadius, fill, border, tuned ? 2.0f : 1.0f);

        std::wstring label = std::to_wstring(RoundNumber(screen.episodes[index].number));
        paint::Label(draw.hDC, cell, label, chosen ? colors.accentText : colors.text,
                     DT_CENTER | DT_VCENTER);
    }

    SelectObject(draw.hDC, old);
}

// Which episode a point of the grid falls on, or -1.
int HitTest(HWND grid, const Screen& screen, POINT point) {
    RECT bounds = {};
    GetClientRect(grid, &bounds);
    int column = point.x / (kCellWidth + kCellGap);
    int row = point.y / (kCellHeight + kCellGap) + screen.scroll;
    if (column < 0 || column >= screen.columns || row < 0) {
        return -1;
    }
    if (point.x % (kCellWidth + kCellGap) > kCellWidth) {
        return -1;
    }
    int index = row * screen.columns + column;
    return static_cast<size_t>(index) < screen.episodes.size() ? index : -1;
}

}  // namespace

// --- state ------------------------------------------------------------------

namespace {

// Keeps the typed selection and the picked tiles saying the same thing.
void SyncFromText(HWND dialog, Screen& screen) {
    std::vector<int> numbers =
        selection::Parse(ReadText(dialog, IDC_ADD_SELECTION),
                         static_cast<int>(screen.episodes.size()));
    std::set<int> wanted(numbers.begin(), numbers.end());

    screen.picked.clear();
    for (size_t index = 0; index < screen.episodes.size(); ++index) {
        if (wanted.count(RoundNumber(screen.episodes[index].number)) > 0) {
            screen.picked.insert(static_cast<int>(index));
        }
    }
}

void SyncFromPicked(HWND dialog, const Screen& screen) {
    std::vector<int> numbers;
    for (int index : screen.picked) {
        numbers.push_back(RoundNumber(screen.episodes[static_cast<size_t>(index)].number));
    }
    SetDlgItemTextW(dialog, IDC_ADD_SELECTION, selection::Collapse(numbers).c_str());
}

// Matches every control to the state: mode, counters, availability.
void Refresh(HWND dialog, Screen& screen) {
    bool loaded = !screen.episodes.empty();
    bool idle = !screen.busy;

    ShowWindow(GetDlgItem(dialog, IDC_ADD_SELECTION), !screen.listMode && loaded ? SW_SHOW : SW_HIDE);
    ShowWindow(GetDlgItem(dialog, IDC_ADD_GRID), screen.listMode && loaded ? SW_SHOW : SW_HIDE);
    for (int control : {IDC_ADD_ALL, IDC_ADD_NONE, IDC_ADD_HINT}) {
        ShowWindow(GetDlgItem(dialog, control), screen.listMode && loaded ? SW_SHOW : SW_HIDE);
    }
    for (int control : {IDC_ADD_MODE_TEXT, IDC_ADD_MODE_LIST, IDC_ADD_COUNTER}) {
        ShowWindow(GetDlgItem(dialog, control), loaded ? SW_SHOW : SW_HIDE);
    }

    if (loaded) {
        SetDlgItemTextW(dialog, IDC_ADD_COUNT,
                        Format(STR_ADD_EPISODE_COUNT, static_cast<int>(screen.episodes.size()))
                            .c_str());
        wchar_t counter[64] = {};
        wsprintfW(counter, L"%d / %d", static_cast<int>(screen.picked.size()),
                  static_cast<int>(screen.episodes.size()));
        SetDlgItemTextW(dialog, IDC_ADD_COUNTER, counter);
    }

    int chosen = screen.listMode ? static_cast<int>(screen.picked.size())
                                 : static_cast<int>(selection::Parse(
                                       ReadText(dialog, IDC_ADD_SELECTION),
                                       static_cast<int>(screen.episodes.size()))
                                                        .size());
    SetDlgItemTextW(dialog, IDOK, Format(STR_ADD_DOWNLOAD_COUNT, chosen).c_str());

    ShowWindow(GetDlgItem(dialog, IDC_ADD_PLAYER),
               screen.playerOptions.empty() ? SW_HIDE : SW_SHOW);
    for (int control : {IDC_ADD_PLAYER, IDC_ADD_MODE_TEXT, IDC_ADD_MODE_LIST}) {
        InvalidateRect(GetDlgItem(dialog, control), nullptr, TRUE);
    }

    EnableWindow(GetDlgItem(dialog, IDC_ADD_FETCH), idle && !screen.sources.empty());
    EnableWindow(GetDlgItem(dialog, IDC_ADD_BROWSE), idle);
    EnableWindow(GetDlgItem(dialog, IDOK), idle && chosen > 0);
    EnableWindow(GetDlgItem(dialog, IDCANCEL), idle);

    InvalidateRect(GetDlgItem(dialog, IDC_ADD_GRID), nullptr, TRUE);
    InvalidateRect(GetDlgItem(dialog, IDC_ADD_POSTER), nullptr, TRUE);
}

// Reads the players the chosen source declares, to fill the global menu.
void LoadPlayerOptions(Screen& screen) {
    screen.playerOptions.clear();
    screen.player = kAuto;
    if (screen.sources.empty()) {
        return;
    }

    const std::string& id = screen.sources[static_cast<size_t>(screen.source)].id;
    std::unique_ptr<Addon> addon =
        Addon::Load(screen.store->LibraryPath(id), *screen.http, screen.store->ReadConfig(id));
    if (!addon) {
        return;
    }
    for (const AddonPreference& preference : addon->Preferences()) {
        if (preference.key == "preferred_player") {
            screen.playerOptions = preference.options;
            break;
        }
    }
}

// Builds one owner-drawn button per installed source.
void BuildSourceStrip(HWND dialog, Screen& screen) {
    HWND area = GetDlgItem(dialog, IDC_ADD_SOURCES);
    RECT bounds = {};
    GetWindowRect(area, &bounds);
    MapWindowPoints(nullptr, dialog, reinterpret_cast<POINT*>(&bounds), 2);
    ShowWindow(area, SW_HIDE);

    HINSTANCE instance = reinterpret_cast<HINSTANCE>(GetWindowLongPtrW(dialog, GWLP_HINSTANCE));
    int x = bounds.left;
    int id = IDC_ADD_SOURCE_FIRST;

    for (const InstalledAddon& source : screen.sources) {
        HWND button = CreateWindowExW(0, WC_BUTTONW, L"", WS_CHILD | WS_VISIBLE | WS_TABSTOP |
                                                              BS_OWNERDRAW,
                                      x, bounds.top, kSourceCell, kSourceCell, dialog,
                                      reinterpret_cast<HMENU>(static_cast<UINT_PTR>(id)), instance,
                                      nullptr);
        std::wstring tip = Widen(source.name);
        SendMessageW(button, WM_SETTEXT, 0, reinterpret_cast<LPARAM>(tip.c_str()));
        x += kSourceCell + 6;
        ++id;
    }
}

// Decodes the icons of the installed sources once, for the strip.
void LoadSourceIcons(Screen& screen) {
    for (const InstalledAddon& source : screen.sources) {
        StoreEntry entry;
        entry.id = source.id;
        entry.installed = true;
        screen.sourceIcons.push_back(
            image::DecodeSquare(screen.store->IconBytes(entry), kSourceIcon));
    }
}

}  // namespace

// --- work off the interface thread ------------------------------------------

namespace {

// Asks the source for the anime, its episodes and its poster.
void StartLoad(HWND dialog, Screen& screen) {
    std::string url = Narrow(ReadText(dialog, IDC_ADD_URL));
    if (url.empty() || screen.sources.empty()) {
        return;
    }

    screen.busy = true;
    screen.url = url;
    Refresh(dialog, screen);

    const std::string& id = screen.sources[static_cast<size_t>(screen.source)].id;
    std::wstring library = screen.store->LibraryPath(id);
    std::map<std::string, std::string> config = screen.store->ReadConfig(id);
    Http* http = screen.http;

    std::thread([dialog, library, config, url, http] {
        auto* listing = new Listing();
        std::unique_ptr<Addon> addon = Addon::Load(library, *http, config);
        if (addon) {
            nlohmann::json input = {{"url", url}};
            if (std::optional<nlohmann::json> details = addon->Call("adm_anime_details", input)) {
                listing->title = details->value("title", std::string());
                auto poster = details->find("posterUrl");
                if (poster != details->end() && poster->is_string()) {
                    listing->posterUrl = poster->get<std::string>();
                }
            }
            if (std::optional<nlohmann::json> episodes = addon->Call("adm_episode_list", input)) {
                if (episodes->is_array()) {
                    for (const nlohmann::json& item : *episodes) {
                        Episode episode;
                        episode.url = item.value("url", std::string());
                        episode.name = item.value("name", std::string());
                        episode.number = item.value("number", 0.0);
                        if (!episode.url.empty()) {
                            listing->episodes.push_back(std::move(episode));
                        }
                    }
                    listing->ok = !listing->episodes.empty();
                }
            }
        }
        if (!listing->posterUrl.empty()) {
            // Image hosts often refuse a hotlink without the page it belongs to.
            std::map<std::string, std::string> headers = {{"Referer", OriginOf(url)}};
            if (std::optional<std::vector<uint8_t>> bytes =
                    http->GetBytes(listing->posterUrl, headers)) {
                listing->poster = std::move(*bytes);
            }
        }
        if (!PostMessageW(dialog, kLoaded, 0, reinterpret_cast<LPARAM>(listing))) {
            delete listing;
        }
    }).detach();
}

// Asks the source which players it has for one episode.
void StartHosters(HWND dialog, Screen& screen, int index) {
    if (index < 0 || static_cast<size_t>(index) >= screen.episodes.size()) {
        return;
    }

    const std::string& id = screen.sources[static_cast<size_t>(screen.source)].id;
    std::wstring library = screen.store->LibraryPath(id);
    std::map<std::string, std::string> config = screen.store->ReadConfig(id);
    std::string url = screen.episodes[static_cast<size_t>(index)].url;
    Http* http = screen.http;

    std::thread([dialog, library, config, url, http, index] {
        auto* players = new Players();
        players->episode = index;
        std::unique_ptr<Addon> addon = Addon::Load(library, *http, config);
        if (addon) {
            if (std::optional<nlohmann::json> hosters =
                    addon->Call("adm_hoster_list", {{"url", url}})) {
                if (hosters->is_array()) {
                    for (const nlohmann::json& hoster : *hosters) {
                        std::string name = hoster.value("name", std::string());
                        if (!name.empty()) {
                            players->names.push_back(name);
                        }
                    }
                }
            }
        }
        if (!PostMessageW(dialog, kHosters, 0, reinterpret_cast<LPARAM>(players))) {
            delete players;
        }
    }).detach();
}

// Shows a list of players at a point and returns the one chosen, or empty.
std::wstring PickPlayer(HWND dialog, const std::vector<std::string>& names,
                        const std::wstring& current, POINT where) {
    HMENU menu = CreatePopupMenu();
    if (names.empty()) {
        AppendMenuW(menu, MF_STRING | MF_GRAYED, 1, Str(STR_ADD_NO_PLAYER));
    } else {
        UINT id = 1;
        for (const std::string& name : names) {
            std::wstring label = Widen(name);
            UINT flags = MF_STRING;
            if (label == current) {
                flags |= MF_CHECKED;
            }
            AppendMenuW(menu, flags, id, label.c_str());
            ++id;
        }
    }

    int chosen = static_cast<int>(TrackPopupMenu(menu, TPM_RIGHTBUTTON | TPM_RETURNCMD, where.x,
                                                 where.y, 0, dialog, nullptr));
    DestroyMenu(menu);

    if (chosen <= 0 || names.empty() || static_cast<size_t>(chosen) > names.size()) {
        return std::wstring();
    }
    return Widen(names[static_cast<size_t>(chosen - 1)]);
}

}  // namespace

// --- dialog -----------------------------------------------------------------

namespace {

void Retranslate(HWND dialog) {
    SetDialogTitle(dialog, STR_DLG_ADD_TITLE);
    SetDialogText(dialog, IDC_ADD_LBL_SOURCE, STR_DLG_ADD_SOURCE);
    SetDialogText(dialog, IDC_ADD_LBL_URL, STR_DLG_ADD_URL);
    SetDialogText(dialog, IDC_ADD_FETCH, STR_DLG_ADD_FETCH);
    SetDialogText(dialog, IDC_ADD_LBL_EPISODES, STR_DLG_ADD_EPISODES);
    SetDialogText(dialog, IDC_ADD_ALL, STR_ADD_SELECT_ALL);
    SetDialogText(dialog, IDC_ADD_NONE, STR_ADD_SELECT_NONE);
    SetDialogText(dialog, IDC_ADD_HINT, STR_ADD_PLAYER_HINT);
    SetDialogText(dialog, IDC_ADD_LBL_DEST, STR_DLG_ADD_DEST);
    SetDialogText(dialog, IDC_ADD_BROWSE, STR_DLG_BROWSE);
    SetDialogText(dialog, IDCANCEL, STR_DLG_CANCEL);
}

// Gathers what the user picked into the answer handed to the caller.
void Confirm(HWND dialog, Screen& screen) {
    std::set<int> chosen = screen.picked;
    if (!screen.listMode) {
        SyncFromText(dialog, screen);
        chosen = screen.picked;
    }
    if (chosen.empty()) {
        return;
    }

    AddRequest& request = *screen.request;
    request.addonId = screen.sources[static_cast<size_t>(screen.source)].id;
    request.animeTitle = screen.title;
    request.animeUrl = screen.url;
    request.destination = ReadText(dialog, IDC_ADD_DEST);
    request.episodes.clear();

    for (int index : chosen) {
        const Episode& source = screen.episodes[static_cast<size_t>(index)];
        AddRequestEpisode episode;
        episode.number = source.number;
        episode.name = source.name;
        episode.url = source.url;

        auto tuned = screen.playerByEpisode.find(index);
        if (tuned != screen.playerByEpisode.end()) {
            episode.player = tuned->second;
        } else if (screen.player != kAuto) {
            episode.player = Narrow(screen.player);
        }
        request.episodes.push_back(std::move(episode));
    }
    EndDialog(dialog, IDOK);
}

// Frees the bitmaps the dialog decoded.
void ReleaseArtwork(Screen& screen) {
    for (HBITMAP icon : screen.sourceIcons) {
        if (icon != nullptr) {
            DeleteObject(icon);
        }
    }
    screen.sourceIcons.clear();
    if (screen.poster != nullptr) {
        DeleteObject(screen.poster);
        screen.poster = nullptr;
    }
}

// How many rows of tiles fit, and how far the grid may scroll.
int MaxScroll(HWND grid, const Screen& screen) {
    RECT bounds = {};
    GetClientRect(grid, &bounds);
    int rows = (static_cast<int>(screen.episodes.size()) + screen.columns - 1) / screen.columns;
    int visible = std::max(1, static_cast<int>(bounds.bottom + kCellGap) /
                                  (kCellHeight + kCellGap));
    return std::max(0, rows - visible);
}

// A static draws the grid but says nothing of the mouse, so it is subclassed:
// a click toggles an episode, a right-click chooses its player.
LRESULT CALLBACK GridProc(HWND grid, UINT msg, WPARAM wParam, LPARAM lParam, UINT_PTR id,
                          DWORD_PTR data) {
    auto* screen = reinterpret_cast<Screen*>(data);
    HWND dialog = GetParent(grid);

    switch (msg) {
    // A static answers HTTRANSPARENT by default, so the mouse would go straight
    // through the grid and never reach the clicks below.
    case WM_NCHITTEST:
        return HTCLIENT;

    case WM_MOUSEMOVE: {
        POINT point = {GET_X_LPARAM(lParam), GET_Y_LPARAM(lParam)};
        int index = HitTest(grid, *screen, point);
        if (index != screen->hover) {
            screen->hover = index;
            InvalidateRect(grid, nullptr, TRUE);
        }
        TRACKMOUSEEVENT track = {};
        track.cbSize = sizeof(track);
        track.dwFlags = TME_LEAVE;
        track.hwndTrack = grid;
        TrackMouseEvent(&track);
        return 0;
    }

    case WM_MOUSELEAVE:
        if (screen->hover != -1) {
            screen->hover = -1;
            InvalidateRect(grid, nullptr, TRUE);
        }
        return 0;

    case WM_LBUTTONDOWN: {
        POINT point = {GET_X_LPARAM(lParam), GET_Y_LPARAM(lParam)};
        int index = HitTest(grid, *screen, point);
        if (index >= 0) {
            if (screen->picked.count(index) > 0) {
                screen->picked.erase(index);
            } else {
                screen->picked.insert(index);
            }
            Refresh(dialog, *screen);
        }
        return 0;
    }
    case WM_RBUTTONDOWN: {
        POINT point = {GET_X_LPARAM(lParam), GET_Y_LPARAM(lParam)};
        int index = HitTest(grid, *screen, point);
        if (index >= 0) {
            StartHosters(dialog, *screen, index);
        }
        return 0;
    }
    case WM_MOUSEWHEEL: {
        int step = GET_WHEEL_DELTA_WPARAM(wParam) > 0 ? -1 : 1;
        screen->scroll = std::max(0, std::min(screen->scroll + step, MaxScroll(grid, *screen)));
        InvalidateRect(grid, nullptr, TRUE);
        return 0;
    }
    case WM_NCDESTROY:
        RemoveWindowSubclass(grid, GridProc, id);
        break;
    default:
        break;
    }
    return DefSubclassProc(grid, msg, wParam, lParam);
}

INT_PTR CALLBACK AddDialogProc(HWND dialog, UINT msg, WPARAM wParam, LPARAM lParam) {
    INT_PTR colour = 0;
    if (ThemeDialogMessage(msg, wParam, &colour)) {
        return colour;
    }

    auto* screen = reinterpret_cast<Screen*>(GetWindowLongPtrW(dialog, GWLP_USERDATA));

    switch (msg) {
    case WM_INITDIALOG: {
        SetWindowLongPtrW(dialog, GWLP_USERDATA, lParam);
        screen = reinterpret_cast<Screen*>(lParam);
        Retranslate(dialog);
        LoadSourceIcons(*screen);
        BuildSourceStrip(dialog, *screen);
        LoadPlayerOptions(*screen);
        SetDlgItemTextW(dialog, IDC_ADD_DEST, DefaultDestination().c_str());
        SetDlgItemTextW(dialog, IDC_ADD_SELECTION, L"");
        SetWindowSubclass(GetDlgItem(dialog, IDC_ADD_GRID), GridProc, 1,
                          reinterpret_cast<DWORD_PTR>(screen));
        ActiveTheme().ApplyToDialog(dialog);
        Refresh(dialog, *screen);
        if (screen->sources.empty()) {
            SetDlgItemTextW(dialog, IDC_ADD_TITLE, Str(STR_ADD_NO_SOURCE));
        }
        return TRUE;
    }

    case WM_DRAWITEM: {
        auto* draw = reinterpret_cast<DRAWITEMSTRUCT*>(lParam);
        if (screen == nullptr) {
            return FALSE;
        }
        if (draw->CtlID >= IDC_ADD_SOURCE_FIRST) {
            DrawSource(*draw, *screen);
            return TRUE;
        }
        if (draw->CtlID == IDC_ADD_POSTER) {
            DrawPoster(*draw, *screen);
            return TRUE;
        }
        if (draw->CtlID == IDC_ADD_GRID) {
            DrawGrid(*draw, *screen);
            return TRUE;
        }
        if (draw->CtlID == IDC_ADD_PLAYER) {
            DrawPlayer(*draw, *screen);
            return TRUE;
        }
        if (draw->CtlID == IDC_ADD_MODE_TEXT) {
            DrawSegment(*draw, !screen->listMode, STR_ADD_MODE_TEXT);
            return TRUE;
        }
        if (draw->CtlID == IDC_ADD_MODE_LIST) {
            DrawSegment(*draw, screen->listMode, STR_ADD_MODE_LIST);
            return TRUE;
        }
        return FALSE;
    }

    case kLoaded: {
        std::unique_ptr<Listing> listing(reinterpret_cast<Listing*>(lParam));
        screen->busy = false;
        if (listing->ok) {
            screen->title = listing->title;
            screen->episodes.clear();
            for (Episode& episode : listing->episodes) {
                screen->episodes.push_back(std::move(episode));
            }
            screen->picked.clear();
            for (size_t index = 0; index < screen->episodes.size(); ++index) {
                screen->picked.insert(static_cast<int>(index));
            }
            screen->playerByEpisode.clear();
            screen->scroll = 0;

            if (screen->poster != nullptr) {
                DeleteObject(screen->poster);
                screen->poster = nullptr;
            }
            if (!listing->poster.empty()) {
                RECT slot = {};
                GetClientRect(GetDlgItem(dialog, IDC_ADD_POSTER), &slot);
                screen->poster = image::Decode(listing->poster, slot.right, slot.bottom);
            }

            SetDlgItemTextW(dialog, IDC_ADD_TITLE, Widen(screen->title).c_str());
            SyncFromPicked(dialog, *screen);
        } else {
            screen->episodes.clear();
            screen->picked.clear();
            SetDlgItemTextW(dialog, IDC_ADD_TITLE, Str(STR_ADD_LOAD_FAILED));
        }
        Refresh(dialog, *screen);
        return TRUE;
    }

    case kHosters: {
        std::unique_ptr<Players> players(reinterpret_cast<Players*>(lParam));
        POINT where = {};
        GetCursorPos(&where);
        auto tuned = screen->playerByEpisode.find(players->episode);
        std::wstring current =
            tuned != screen->playerByEpisode.end() ? Widen(tuned->second) : screen->player;
        std::wstring chosen = PickPlayer(dialog, players->names, current, where);
        if (!chosen.empty()) {
            screen->playerByEpisode[players->episode] = Narrow(chosen);
            Refresh(dialog, *screen);
        }
        return TRUE;
    }

    case WM_MOUSEWHEEL: {
        if (screen == nullptr || !screen->listMode) {
            return FALSE;
        }
        HWND grid = GetDlgItem(dialog, IDC_ADD_GRID);
        POINT where = {GET_X_LPARAM(lParam), GET_Y_LPARAM(lParam)};
        RECT bounds = {};
        GetWindowRect(grid, &bounds);
        if (!PtInRect(&bounds, where)) {
            return FALSE;
        }
        int step = GET_WHEEL_DELTA_WPARAM(wParam) > 0 ? -1 : 1;
        screen->scroll = std::max(0, std::min(screen->scroll + step, MaxScroll(grid, *screen)));
        InvalidateRect(grid, nullptr, TRUE);
        return TRUE;
    }

    case WM_COMMAND: {
        if (screen == nullptr) {
            return FALSE;
        }
        int control = LOWORD(wParam);

        if (control >= IDC_ADD_SOURCE_FIRST &&
            control < IDC_ADD_SOURCE_FIRST + static_cast<int>(screen->sources.size())) {
            screen->source = control - IDC_ADD_SOURCE_FIRST;
            LoadPlayerOptions(*screen);
            for (size_t index = 0; index < screen->sources.size(); ++index) {
                InvalidateRect(GetDlgItem(dialog, IDC_ADD_SOURCE_FIRST + static_cast<int>(index)),
                               nullptr, TRUE);
            }
            Refresh(dialog, *screen);
            return TRUE;
        }

        if (screen->busy) {
            return TRUE;
        }

        switch (control) {
        case IDC_ADD_FETCH:
            StartLoad(dialog, *screen);
            return TRUE;
        case IDC_ADD_PLAYER: {
            RECT bounds = {};
            GetWindowRect(GetDlgItem(dialog, IDC_ADD_PLAYER), &bounds);
            std::vector<std::string> names = {Narrow(kAuto)};
            for (const std::string& option : screen->playerOptions) {
                if (option != Narrow(kAuto)) {
                    names.push_back(option);
                }
            }
            POINT where = {bounds.left, bounds.bottom};
            std::wstring chosen = PickPlayer(dialog, names, screen->player, where);
            if (!chosen.empty()) {
                screen->player = chosen;
                Refresh(dialog, *screen);
            }
            return TRUE;
        }
        case IDC_ADD_MODE_TEXT:
            if (screen->listMode) {
                SyncFromPicked(dialog, *screen);
                screen->listMode = false;
                Refresh(dialog, *screen);
            }
            return TRUE;
        case IDC_ADD_MODE_LIST:
            if (!screen->listMode) {
                SyncFromText(dialog, *screen);
                screen->listMode = true;
                Refresh(dialog, *screen);
            }
            return TRUE;
        case IDC_ADD_ALL:
            for (size_t index = 0; index < screen->episodes.size(); ++index) {
                screen->picked.insert(static_cast<int>(index));
            }
            Refresh(dialog, *screen);
            return TRUE;
        case IDC_ADD_NONE:
            screen->picked.clear();
            Refresh(dialog, *screen);
            return TRUE;
        case IDC_ADD_SELECTION:
            if (HIWORD(wParam) == EN_CHANGE) {
                Refresh(dialog, *screen);
            }
            return TRUE;
        case IDC_ADD_BROWSE: {
            std::wstring folder = PickFolder(dialog);
            if (!folder.empty()) {
                SetDlgItemTextW(dialog, IDC_ADD_DEST, folder.c_str());
            }
            return TRUE;
        }
        case IDOK:
            Confirm(dialog, *screen);
            return TRUE;
        case IDCANCEL:
            EndDialog(dialog, IDCANCEL);
            return TRUE;
        default:
            return FALSE;
        }
    }

    case WM_DESTROY:
        if (screen != nullptr) {
            ReleaseArtwork(*screen);
        }
        return FALSE;

    case WM_CLOSE:
        if (screen == nullptr || !screen->busy) {
            EndDialog(dialog, IDCANCEL);
        }
        return TRUE;

    default:
        return FALSE;
    }
}

}  // namespace

// Runs the add dialog modally against its owner window.
INT_PTR ShowAddDialog(HWND owner, HINSTANCE instance, const AddonStore& store, Http& http,
                      AddRequest* request) {
    Screen screen;
    screen.store = &store;
    screen.http = &http;
    screen.request = request;
    screen.sources = store.Installed();

    return DialogBoxParamW(instance, MAKEINTRESOURCEW(IDD_ADD_DOWNLOAD), owner, AddDialogProc,
                           reinterpret_cast<LPARAM>(&screen));
}
