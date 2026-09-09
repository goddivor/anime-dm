#include "ui/AddDialog.h"

#include <commctrl.h>
#include <objbase.h>
#include <shlobj.h>
#include <shobjidl.h>
#include <windowsx.h>

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
#include "core/FolderIcon.h"
#include "core/Settings.h"
#include "ui/PosterDialog.h"
#include "ui/TemplateNames.h"
#include "ui/Resource.h"
#include "ui/Strings.h"
#include "ui/Theme.h"

namespace {

constexpr UINT kLoaded = WM_APP + 1;
constexpr UINT kHosters = WM_APP + 2;
constexpr float kRadius = 5.0f;
constexpr int kGap = 14;
constexpr wchar_t kAuto[] = L"Auto";

// The controls that only make sense once an anime has answered.
constexpr int kRevealed[] = {
    IDC_ADD_POSTER, IDC_ADD_TITLE,     IDC_ADD_COUNT,     IDC_ADD_SEP1,
    IDC_ADD_LBL_EPISODES, IDC_ADD_PLAYER, IDC_ADD_MODE_TEXT, IDC_ADD_MODE_LIST,
    IDC_ADD_SELECTION, IDC_ADD_EPISODES, IDC_ADD_HINT,      IDC_ADD_COUNTER,
};

// The controls that sit under it and slide up while it is hidden.
constexpr int kBottom[] = {
    IDC_ADD_SEP2, IDC_ADD_LBL_DEST,     IDC_ADD_DEST, IDC_ADD_BROWSE,
    IDOK,         IDC_ADD_LBL_TEMPLATE, IDC_ADD_TEMPLATE, IDCANCEL,
};

// One episode as the source described it.
struct Episode {
    double number = 0.0;
    std::string name;
    std::string url;
};

// What a load brought back.
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
    const Settings* settings = nullptr;
    AddRequest* request = nullptr;
    std::vector<std::string> templates;

    std::vector<InstalledAddon> sources;

    std::vector<std::string> playerOptions;
    std::wstring player = kAuto;
    std::map<int, std::string> playerByEpisode;

    std::vector<Episode> episodes;
    std::set<int> picked;
    std::string title;
    std::string url;

    std::vector<uint8_t> posterBytes;
    std::string posterUrl;
    HBITMAP poster = nullptr;

    bool listMode = true;
    bool busy = false;
    bool expanded = false;
    int offset = 0;
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

RECT ClientRectOf(HWND dialog, int control) {
    RECT bounds = {};
    GetWindowRect(GetDlgItem(dialog, control), &bounds);
    MapWindowPoints(nullptr, dialog, reinterpret_cast<POINT*>(&bounds), 2);
    return bounds;
}

int RoundNumber(double number) {
    return static_cast<int>(number + (number < 0 ? -0.5 : 0.5));
}

std::wstring Format(StringId id, int value) {
    wchar_t text[128] = {};
    wsprintfW(text, Str(id), value);
    return text;
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

}  // namespace

// --- painting ---------------------------------------------------------------

namespace {

HFONT FontOf(HWND control) {
    return reinterpret_cast<HFONT>(SendMessageW(control, WM_GETFONT, 0, 0));
}

// Draws the chip that opens the player menu.
void DrawPlayer(const DRAWITEMSTRUCT& draw, const Screen& screen) {
    const ThemeColors& colours = ActiveTheme().Colors();
    bool pressed = (draw.itemState & ODS_SELECTED) != 0;

    paint::RoundedRect(draw.hDC, draw.rcItem, kRadius, pressed ? colours.hover : colours.surface,
                       colours.line);

    HFONT old = static_cast<HFONT>(SelectObject(draw.hDC, FontOf(draw.hwndItem)));
    RECT text = draw.rcItem;
    text.left += 10;
    text.right -= 20;
    std::wstring label = std::wstring(Str(STR_ADD_PLAYER_LABEL)) + L" " + screen.player;
    paint::Label(draw.hDC, text, label, colours.text, DT_LEFT | DT_VCENTER | DT_END_ELLIPSIS);

    RECT arrow = draw.rcItem;
    arrow.left = arrow.right - 18;
    paint::Label(draw.hDC, arrow, L"▾", colours.text, DT_CENTER | DT_VCENTER);
    SelectObject(draw.hDC, old);
}

// Draws one half of the mode switch, the active one filled.
void DrawSegment(const DRAWITEMSTRUCT& draw, bool active, StringId label) {
    const ThemeColors& colours = ActiveTheme().Colors();
    paint::RoundedRect(draw.hDC, draw.rcItem, kRadius, active ? colours.accent : colours.surface,
                       active ? colours.accent : colours.line);

    HFONT old = static_cast<HFONT>(SelectObject(draw.hDC, FontOf(draw.hwndItem)));
    paint::Label(draw.hDC, draw.rcItem, Str(label), active ? colours.accentText : colours.text,
                 DT_CENTER | DT_VCENTER);
    SelectObject(draw.hDC, old);
}

// Draws the cover, which a click opens at full size.
void DrawPoster(const DRAWITEMSTRUCT& draw, const Screen& screen) {
    const ThemeColors& colours = ActiveTheme().Colors();
    if (screen.poster == nullptr) {
        paint::RoundedRect(draw.hDC, draw.rcItem, kRadius, colours.surface, colours.line);
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

}  // namespace

// --- episodes ---------------------------------------------------------------

namespace {

// One column, no header: only the episode matters here.
void InitEpisodeList(HWND dialog) {
    HWND list = GetDlgItem(dialog, IDC_ADD_EPISODES);
    ListView_SetExtendedListViewStyle(
        list, LVS_EX_CHECKBOXES | LVS_EX_FULLROWSELECT | LVS_EX_DOUBLEBUFFER);

    RECT bounds = {};
    GetClientRect(list, &bounds);

    LVCOLUMNW column = {};
    column.mask = LVCF_TEXT | LVCF_WIDTH | LVCF_SUBITEM;
    column.iSubItem = 0;
    column.cx = bounds.right - 4;
    column.pszText = const_cast<wchar_t*>(Str(STR_DLG_ADD_EPISODES));
    ListView_InsertColumn(list, 0, &column);
}

// The label of a row: the episode, and its own player when it has one.
std::wstring RowLabel(const Screen& screen, size_t index) {
    wchar_t text[128] = {};
    auto tuned = screen.playerByEpisode.find(static_cast<int>(index));
    if (tuned != screen.playerByEpisode.end()) {
        wsprintfW(text, L"Ep %03d   ·   %s", RoundNumber(screen.episodes[index].number),
                  Widen(tuned->second).c_str());
    } else {
        wsprintfW(text, L"Ep %03d", RoundNumber(screen.episodes[index].number));
    }
    return text;
}

// Shows the episodes the source answered.
void FillEpisodeList(HWND dialog, const Screen& screen) {
    HWND list = GetDlgItem(dialog, IDC_ADD_EPISODES);
    ListView_DeleteAllItems(list);

    for (size_t index = 0; index < screen.episodes.size(); ++index) {
        std::wstring label = RowLabel(screen, index);
        LVITEMW item = {};
        item.mask = LVIF_TEXT;
        item.iItem = static_cast<int>(index);
        item.pszText = label.data();
        ListView_InsertItem(list, &item);
    }
}

// Rewrites the labels after a per-episode player changed.
void RefreshLabels(HWND dialog, const Screen& screen) {
    HWND list = GetDlgItem(dialog, IDC_ADD_EPISODES);
    for (size_t index = 0; index < screen.episodes.size(); ++index) {
        std::wstring label = RowLabel(screen, index);
        ListView_SetItemText(list, static_cast<int>(index), 0, label.data());
    }
}

// Reads the ticked rows back into the picked set.
void ReadChecks(HWND dialog, Screen& screen) {
    HWND list = GetDlgItem(dialog, IDC_ADD_EPISODES);
    screen.picked.clear();
    for (size_t index = 0; index < screen.episodes.size(); ++index) {
        if (ListView_GetCheckState(list, static_cast<int>(index))) {
            screen.picked.insert(static_cast<int>(index));
        }
    }
}

// Ticks the rows the picked set names.
void ApplyChecks(HWND dialog, const Screen& screen) {
    HWND list = GetDlgItem(dialog, IDC_ADD_EPISODES);
    for (size_t index = 0; index < screen.episodes.size(); ++index) {
        ListView_SetCheckState(list, static_cast<int>(index),
                               screen.picked.count(static_cast<int>(index)) > 0);
    }
}

// Reads the typed ranges into the picked set.
void ReadTyped(HWND dialog, Screen& screen) {
    std::vector<int> numbers = selection::Parse(ReadText(dialog, IDC_ADD_SELECTION),
                                                static_cast<int>(screen.episodes.size()));
    std::set<int> wanted(numbers.begin(), numbers.end());

    screen.picked.clear();
    for (size_t index = 0; index < screen.episodes.size(); ++index) {
        if (wanted.count(RoundNumber(screen.episodes[index].number)) > 0) {
            screen.picked.insert(static_cast<int>(index));
        }
    }
}

// Writes the picked set back as ranges.
void WriteTyped(HWND dialog, const Screen& screen) {
    std::vector<int> numbers;
    for (int index : screen.picked) {
        numbers.push_back(RoundNumber(screen.episodes[static_cast<size_t>(index)].number));
    }
    SetDlgItemTextW(dialog, IDC_ADD_SELECTION, selection::Collapse(numbers).c_str());
}

}  // namespace

// --- layout -----------------------------------------------------------------

namespace {

// The dialog opens on the link alone; everything the anime brings only appears
// once a source has answered, and the window grows to make room for it.
void SetExpanded(HWND dialog, Screen& screen, bool expanded) {
    if (screen.expanded == expanded) {
        return;
    }
    screen.expanded = expanded;

    int shift = expanded ? screen.offset : -screen.offset;
    for (int control : kBottom) {
        RECT bounds = ClientRectOf(dialog, control);
        SetWindowPos(GetDlgItem(dialog, control), nullptr, bounds.left, bounds.top + shift, 0, 0,
                     SWP_NOSIZE | SWP_NOZORDER);
    }
    for (int control : kRevealed) {
        ShowWindow(GetDlgItem(dialog, control), expanded ? SW_SHOW : SW_HIDE);
    }

    RECT frame = {};
    GetWindowRect(dialog, &frame);
    SetWindowPos(dialog, nullptr, 0, 0, frame.right - frame.left,
                 frame.bottom - frame.top + shift, SWP_NOMOVE | SWP_NOZORDER);
    InvalidateRect(dialog, nullptr, TRUE);
}

// Offers the folder-icon recipes, or hides the row when icons are off.
void FillTemplates(HWND dialog, Screen& screen) {
    bool shown = screen.settings != nullptr && screen.settings->folderIcons;
    ShowWindow(GetDlgItem(dialog, IDC_ADD_LBL_TEMPLATE), shown ? SW_SHOW : SW_HIDE);
    ShowWindow(GetDlgItem(dialog, IDC_ADD_TEMPLATE), shown ? SW_SHOW : SW_HIDE);
    if (!shown) {
        return;
    }
    screen.templates = foldericon::TemplateIds();
    SendDlgItemMessageW(dialog, IDC_ADD_TEMPLATE, CB_ADDSTRING, 0,
                        reinterpret_cast<LPARAM>(Str(STR_ADD_FOLDER_ICON_DEFAULT)));
    for (const std::string& id : screen.templates) {
        SendDlgItemMessageW(dialog, IDC_ADD_TEMPLATE, CB_ADDSTRING, 0,
                            reinterpret_cast<LPARAM>(Str(TemplateName(id))));
    }
    SendDlgItemMessageW(dialog, IDC_ADD_TEMPLATE, CB_SETCURSEL, 0, 0);
}

// Matches every control to the state.
void Refresh(HWND dialog, Screen& screen) {
    bool loaded = !screen.episodes.empty();
    bool idle = !screen.busy;

    SetExpanded(dialog, screen, loaded);

    if (loaded) {
        ShowWindow(GetDlgItem(dialog, IDC_ADD_SELECTION),
                   screen.listMode ? SW_HIDE : SW_SHOW);
        ShowWindow(GetDlgItem(dialog, IDC_ADD_EPISODES), screen.listMode ? SW_SHOW : SW_HIDE);
        ShowWindow(GetDlgItem(dialog, IDC_ADD_HINT), screen.listMode ? SW_SHOW : SW_HIDE);
        ShowWindow(GetDlgItem(dialog, IDC_ADD_PLAYER),
                   screen.playerOptions.empty() ? SW_HIDE : SW_SHOW);

        SetDlgItemTextW(dialog, IDC_ADD_COUNT,
                        Format(STR_ADD_EPISODE_COUNT, static_cast<int>(screen.episodes.size()))
                            .c_str());
        wchar_t counter[64] = {};
        wsprintfW(counter, L"%d / %d", static_cast<int>(screen.picked.size()),
                  static_cast<int>(screen.episodes.size()));
        SetDlgItemTextW(dialog, IDC_ADD_COUNTER, counter);

        for (int control : {IDC_ADD_PLAYER, IDC_ADD_MODE_TEXT, IDC_ADD_MODE_LIST}) {
            InvalidateRect(GetDlgItem(dialog, control), nullptr, TRUE);
        }
        InvalidateRect(GetDlgItem(dialog, IDC_ADD_POSTER), nullptr, TRUE);
    }

    int chosen = static_cast<int>(screen.picked.size());
    SetDlgItemTextW(dialog, IDOK, Format(STR_ADD_DOWNLOAD_COUNT, chosen).c_str());

    EnableWindow(GetDlgItem(dialog, IDC_ADD_FETCH), idle && !screen.sources.empty());
    EnableWindow(GetDlgItem(dialog, IDC_ADD_BROWSE), idle);
    EnableWindow(GetDlgItem(dialog, IDOK), idle && chosen > 0);
    EnableWindow(GetDlgItem(dialog, IDCANCEL), idle);
}

// Fills the source list with what is installed.
void FillSources(HWND dialog, const Screen& screen) {
    HWND combo = GetDlgItem(dialog, IDC_ADD_SOURCE);
    for (const InstalledAddon& source : screen.sources) {
        std::wstring label = Widen(source.name) + L"  (" + Widen(source.lang) + L")";
        SendMessageW(combo, CB_ADDSTRING, 0, reinterpret_cast<LPARAM>(label.c_str()));
    }
    if (!screen.sources.empty()) {
        SendMessageW(combo, CB_SETCURSEL, 0, 0);
    } else {
        SendMessageW(combo, CB_ADDSTRING, 0, reinterpret_cast<LPARAM>(Str(STR_ADD_NO_SOURCE)));
        SendMessageW(combo, CB_SETCURSEL, 0, 0);
    }
}

int ChosenSource(HWND dialog, const Screen& screen) {
    int index = static_cast<int>(SendDlgItemMessageW(dialog, IDC_ADD_SOURCE, CB_GETCURSEL, 0, 0));
    return index >= 0 && static_cast<size_t>(index) < screen.sources.size() ? index : -1;
}

// Reads the players the chosen source declares, to fill the global menu.
void LoadPlayerOptions(HWND dialog, Screen& screen) {
    screen.playerOptions.clear();
    screen.player = kAuto;

    int index = ChosenSource(dialog, screen);
    if (index < 0) {
        return;
    }

    const std::string& id = screen.sources[static_cast<size_t>(index)].id;
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

}  // namespace

// --- work off the interface thread ------------------------------------------

namespace {

// Asks the source for the anime, its episodes and its cover.
void StartLoad(HWND dialog, Screen& screen) {
    std::string url = Narrow(ReadText(dialog, IDC_ADD_URL));
    int index = ChosenSource(dialog, screen);
    if (url.empty() || index < 0) {
        return;
    }

    screen.busy = true;
    screen.url = url;
    Refresh(dialog, screen);

    const std::string& id = screen.sources[static_cast<size_t>(index)].id;
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
    int source = ChosenSource(dialog, screen);
    if (source < 0) {
        return;
    }

    const std::string& id = screen.sources[static_cast<size_t>(source)].id;
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
            AppendMenuW(menu, label == current ? (MF_STRING | MF_CHECKED) : MF_STRING, id,
                        label.c_str());
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
    SetDialogText(dialog, IDC_ADD_HINT, STR_ADD_PLAYER_HINT);
    SetDialogText(dialog, IDC_ADD_LBL_DEST, STR_DLG_ADD_DEST);
    SetDialogText(dialog, IDC_ADD_LBL_TEMPLATE, STR_ADD_FOLDER_ICON);
    SetDialogText(dialog, IDC_ADD_BROWSE, STR_DLG_BROWSE);
    SetDialogText(dialog, IDCANCEL, STR_DLG_CANCEL);
}

// Gathers what the user picked into the answer handed to the caller.
void Confirm(HWND dialog, Screen& screen) {
    if (!screen.listMode) {
        ReadTyped(dialog, screen);
    }
    if (screen.picked.empty()) {
        return;
    }
    int source = ChosenSource(dialog, screen);
    if (source < 0) {
        return;
    }

    AddRequest& request = *screen.request;
    request.addonId = screen.sources[static_cast<size_t>(source)].id;
    request.animeTitle = screen.title;
    request.animeUrl = screen.url;
    request.posterUrl = screen.posterUrl;
    request.posterBytes = screen.posterBytes;
    request.destination = ReadText(dialog, IDC_ADD_DEST);
    request.folderTemplate.clear();
    int template_ = static_cast<int>(SendDlgItemMessageW(dialog, IDC_ADD_TEMPLATE, CB_GETCURSEL, 0, 0));
    if (template_ > 0 && static_cast<size_t>(template_) <= screen.templates.size()) {
        request.folderTemplate = screen.templates[static_cast<size_t>(template_ - 1)];
    }
    request.episodes.clear();

    for (int index : screen.picked) {
        const Episode& source_episode = screen.episodes[static_cast<size_t>(index)];
        AddRequestEpisode episode;
        episode.number = source_episode.number;
        episode.name = source_episode.name;
        episode.url = source_episode.url;

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
        FillSources(dialog, *screen);
        InitEpisodeList(dialog);
        SetDlgItemTextW(dialog, IDC_ADD_DEST, DefaultDestination().c_str());
        LoadPlayerOptions(dialog, *screen);
        FillTemplates(dialog, *screen);

        // How far the bottom of the dialog rides up while the anime block is
        // hidden: the gap between the link row and the rule above the folder.
        RECT link = ClientRectOf(dialog, IDC_ADD_URL);
        RECT rule = ClientRectOf(dialog, IDC_ADD_SEP2);
        screen->offset = rule.top - link.bottom - kGap;
        screen->expanded = true;
        SetExpanded(dialog, *screen, false);

        ActiveTheme().ApplyToDialog(dialog);
        Refresh(dialog, *screen);
        return TRUE;
    }

    case WM_DRAWITEM: {
        auto* draw = reinterpret_cast<DRAWITEMSTRUCT*>(lParam);
        if (screen == nullptr) {
            return FALSE;
        }
        if (draw->CtlID == IDC_ADD_POSTER) {
            DrawPoster(*draw, *screen);
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

        if (screen->poster != nullptr) {
            DeleteObject(screen->poster);
            screen->poster = nullptr;
        }

        if (listing->ok) {
            screen->title = listing->title;
            screen->posterUrl = listing->posterUrl;
            screen->posterBytes = std::move(listing->poster);
            screen->episodes = std::move(listing->episodes);
            screen->playerByEpisode.clear();

            screen->picked.clear();
            for (size_t index = 0; index < screen->episodes.size(); ++index) {
                screen->picked.insert(static_cast<int>(index));
            }

            if (!screen->posterBytes.empty()) {
                RECT slot = {};
                GetClientRect(GetDlgItem(dialog, IDC_ADD_POSTER), &slot);
                screen->poster = image::Decode(screen->posterBytes, slot.right, slot.bottom);
            }

            SetDlgItemTextW(dialog, IDC_ADD_TITLE, Widen(screen->title).c_str());
            FillEpisodeList(dialog, *screen);
            ApplyChecks(dialog, *screen);
            WriteTyped(dialog, *screen);
        } else {
            screen->episodes.clear();
            screen->picked.clear();
            screen->posterBytes.clear();
            MessageBoxW(dialog, Str(STR_ADD_LOAD_FAILED), Str(STR_DLG_ADD_TITLE),
                        MB_OK | MB_ICONWARNING);
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
            RefreshLabels(dialog, *screen);
        }
        return TRUE;
    }

    case WM_NOTIFY: {
        auto* notify = reinterpret_cast<NMHDR*>(lParam);
        if (screen == nullptr || notify->idFrom != IDC_ADD_EPISODES) {
            return FALSE;
        }
        if (notify->code == LVN_ITEMCHANGED) {
            auto* changed = reinterpret_cast<NMLISTVIEW*>(lParam);
            if ((changed->uChanged & LVIF_STATE) != 0 &&
                ((changed->uOldState ^ changed->uNewState) & LVIS_STATEIMAGEMASK) != 0) {
                ReadChecks(dialog, *screen);
                Refresh(dialog, *screen);
            }
        } else if (notify->code == NM_RCLICK) {
            auto* clicked = reinterpret_cast<NMITEMACTIVATE*>(lParam);
            StartHosters(dialog, *screen, clicked->iItem);
        }
        return FALSE;
    }

    case WM_COMMAND: {
        if (screen == nullptr) {
            return FALSE;
        }
        int control = LOWORD(wParam);

        if (control == IDC_ADD_POSTER && HIWORD(wParam) == STN_CLICKED) {
            HINSTANCE instance =
                reinterpret_cast<HINSTANCE>(GetWindowLongPtrW(dialog, GWLP_HINSTANCE));
            ShowPosterPreview(dialog, instance, screen->posterBytes, screen->title,
                              screen->posterUrl);
            return TRUE;
        }
        if (control == IDC_ADD_SOURCE && HIWORD(wParam) == CBN_SELCHANGE) {
            LoadPlayerOptions(dialog, *screen);
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
                ReadChecks(dialog, *screen);
                WriteTyped(dialog, *screen);
                screen->listMode = false;
                Refresh(dialog, *screen);
            }
            return TRUE;
        case IDC_ADD_MODE_LIST:
            if (!screen->listMode) {
                ReadTyped(dialog, *screen);
                ApplyChecks(dialog, *screen);
                screen->listMode = true;
                Refresh(dialog, *screen);
            }
            return TRUE;
        case IDC_ADD_SELECTION:
            if (HIWORD(wParam) == EN_CHANGE && !screen->listMode) {
                ReadTyped(dialog, *screen);
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
        if (screen != nullptr && screen->poster != nullptr) {
            DeleteObject(screen->poster);
            screen->poster = nullptr;
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
                      const Settings& settings, AddRequest* request) {
    Screen screen;
    screen.store = &store;
    screen.http = &http;
    screen.settings = &settings;
    screen.request = request;
    screen.sources = store.Installed();

    return DialogBoxParamW(instance, MAKEINTRESOURCEW(IDD_ADD_DOWNLOAD), owner, AddDialogProc,
                           reinterpret_cast<LPARAM>(&screen));
}
