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
#include "core/FolderIcon.h"
#include "core/Http.h"
#include "core/Image.h"
#include "core/Settings.h"
#include "core/Text.h"
#include "ui/AddSelection.h"
#include "ui/ConfirmDialog.h"
#include "ui/NoticeDialog.h"
#include "ui/Paint.h"
#include "ui/PosterDialog.h"
#include "ui/Resource.h"
#include "ui/Strings.h"
#include "ui/TemplateNames.h"
#include "ui/Theme.h"

namespace {

constexpr UINT kLoaded = WM_APP + 1;
constexpr UINT kHosters = WM_APP + 2;
constexpr UINT kHosts = WM_APP + 3;
constexpr float kRadius = 5.0f;
constexpr wchar_t kAuto[] = L"Auto";

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
    std::vector<std::string> players;
};

// The players a source offers for one episode.
struct Players {
    int episode = -1;
    std::vector<std::string> names;
};

// The address every installed source calls home, in the order of the list.
struct Hosts {
    std::vector<std::string> hosts;
};

// What the three windows of the flow share, from the first to the last.
struct Flow {
    const AddonStore* store = nullptr;
    Http* http = nullptr;
    const Settings* settings = nullptr;
    AddRequest* request = nullptr;

    std::vector<InstalledAddon> sources;
    std::vector<std::string> hosts;  // parallel to `sources`, empty until known
    int source = -1;
    std::string url;

    std::string title;
    std::string posterUrl;
    std::vector<uint8_t> posterBytes;
    std::vector<Episode> episodes;
    HBITMAP poster = nullptr;

    std::vector<std::string> templates;
    std::vector<std::string> playerOptions;
    std::wstring player = kAuto;
    std::map<int, std::string> playerByEpisode;
    std::set<int> picked;

    bool listMode = true;
    bool busy = false;
    bool filling = false;  // the rows are being written, their state means nothing
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

int RoundNumber(double number) {
    return static_cast<int>(number + (number < 0 ? -0.5 : 0.5));
}

std::wstring Format(StringId id, int value) {
    wchar_t text[128] = {};
    wsprintfW(text, Str(id), value);
    return text;
}

std::wstring Format(StringId id, int first, int second) {
    wchar_t text[128] = {};
    wsprintfW(text, Str(id), first, second);
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

// The host of an address, lowercased and without its `www.`.
std::string HostOf(const std::string& url) {
    size_t start = url.find("://");
    start = start == std::string::npos ? 0 : start + 3;
    size_t end = url.find_first_of("/?#", start);
    std::string host = url.substr(start, end == std::string::npos ? std::string::npos : end - start);
    std::transform(host.begin(), host.end(), host.begin(),
                   [](unsigned char letter) { return static_cast<char>(tolower(letter)); });
    if (host.rfind("www.", 0) == 0) {
        host = host.substr(4);
    }
    return host;
}

// Whether two hosts belong to the same site, a subdomain counting as one.
bool SameSite(const std::string& one, const std::string& other) {
    if (one.empty() || other.empty()) {
        return false;
    }
    if (one == other) {
        return true;
    }
    return one.size() > other.size() ? one.compare(one.size() - other.size() - 1, other.size() + 1,
                                                   "." + other) == 0
                                     : other.compare(other.size() - one.size() - 1, one.size() + 1,
                                                     "." + one) == 0;
}

// What the clipboard holds, when it holds an address.
std::wstring ClipboardUrl(HWND owner) {
    if (!IsClipboardFormatAvailable(CF_UNICODETEXT) || !OpenClipboard(owner)) {
        return std::wstring();
    }
    std::wstring text;
    HANDLE data = GetClipboardData(CF_UNICODETEXT);
    if (data != nullptr) {
        auto* locked = static_cast<const wchar_t*>(GlobalLock(data));
        if (locked != nullptr) {
            text = locked;
            GlobalUnlock(data);
        }
    }
    CloseClipboard();

    size_t start = text.find_first_not_of(L" \t\r\n");
    if (start == std::wstring::npos) {
        return std::wstring();
    }
    size_t end = text.find_last_not_of(L" \t\r\n");
    text = text.substr(start, end - start + 1);
    if (text.rfind(L"http://", 0) != 0 && text.rfind(L"https://", 0) != 0) {
        return std::wstring();
    }
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

}  // namespace

// --- painting ---------------------------------------------------------------

namespace {

HFONT FontOf(HWND control) {
    return reinterpret_cast<HFONT>(SendMessageW(control, WM_GETFONT, 0, 0));
}

// Draws the chip that opens the player menu.
void DrawPlayer(const DRAWITEMSTRUCT& draw, const Flow& flow) {
    const ThemeColors& colours = ActiveTheme().Colors();
    bool pressed = (draw.itemState & ODS_SELECTED) != 0;

    paint::RoundedRect(draw.hDC, draw.rcItem, kRadius, pressed ? colours.hover : colours.surface,
                       colours.line);

    HFONT old = static_cast<HFONT>(SelectObject(draw.hDC, FontOf(draw.hwndItem)));
    RECT text = draw.rcItem;
    text.left += 10;
    text.right -= 20;
    std::wstring label = std::wstring(Str(STR_ADD_PLAYER_LABEL)) + L" " + flow.player;
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
void DrawPoster(const DRAWITEMSTRUCT& draw, const Flow& flow) {
    const ThemeColors& colours = ActiveTheme().Colors();
    if (flow.poster == nullptr) {
        paint::RoundedRect(draw.hDC, draw.rcItem, kRadius, colours.surface, colours.line);
        return;
    }

    HDC memory = CreateCompatibleDC(draw.hDC);
    HBITMAP old = static_cast<HBITMAP>(SelectObject(memory, flow.poster));
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
std::wstring RowLabel(const Flow& flow, size_t index) {
    wchar_t text[128] = {};
    auto tuned = flow.playerByEpisode.find(static_cast<int>(index));
    if (tuned != flow.playerByEpisode.end()) {
        wsprintfW(text, L"Ep %03d   ·   %s", RoundNumber(flow.episodes[index].number),
                  Widen(tuned->second).c_str());
    } else {
        wsprintfW(text, L"Ep %03d", RoundNumber(flow.episodes[index].number));
    }
    return text;
}

// Shows the episodes the source answered.
void FillEpisodeList(HWND dialog, const Flow& flow) {
    HWND list = GetDlgItem(dialog, IDC_ADD_EPISODES);
    ListView_DeleteAllItems(list);

    for (size_t index = 0; index < flow.episodes.size(); ++index) {
        std::wstring label = RowLabel(flow, index);
        LVITEMW item = {};
        item.mask = LVIF_TEXT;
        item.iItem = static_cast<int>(index);
        item.pszText = label.data();
        ListView_InsertItem(list, &item);
    }
}

// Rewrites the labels after a per-episode player changed.
void RefreshLabels(HWND dialog, const Flow& flow) {
    HWND list = GetDlgItem(dialog, IDC_ADD_EPISODES);
    for (size_t index = 0; index < flow.episodes.size(); ++index) {
        std::wstring label = RowLabel(flow, index);
        ListView_SetItemText(list, static_cast<int>(index), 0, label.data());
    }
}

// Reads the ticked rows back into the picked set.
void ReadChecks(HWND dialog, Flow& flow) {
    HWND list = GetDlgItem(dialog, IDC_ADD_EPISODES);
    flow.picked.clear();
    for (size_t index = 0; index < flow.episodes.size(); ++index) {
        if (ListView_GetCheckState(list, static_cast<int>(index))) {
            flow.picked.insert(static_cast<int>(index));
        }
    }
}

// Ticks the rows the picked set names.
void ApplyChecks(HWND dialog, const Flow& flow) {
    HWND list = GetDlgItem(dialog, IDC_ADD_EPISODES);
    for (size_t index = 0; index < flow.episodes.size(); ++index) {
        ListView_SetCheckState(list, static_cast<int>(index),
                               flow.picked.count(static_cast<int>(index)) > 0);
    }
}

// Reads the typed ranges into the picked set.
void ReadTyped(HWND dialog, Flow& flow) {
    std::vector<int> numbers = selection::Parse(ReadText(dialog, IDC_ADD_SELECTION),
                                                static_cast<int>(flow.episodes.size()));
    std::set<int> wanted(numbers.begin(), numbers.end());

    flow.picked.clear();
    for (size_t index = 0; index < flow.episodes.size(); ++index) {
        if (wanted.count(RoundNumber(flow.episodes[index].number)) > 0) {
            flow.picked.insert(static_cast<int>(index));
        }
    }
}

// Writes the picked set back as ranges.
void WriteTyped(HWND dialog, const Flow& flow) {
    std::vector<int> numbers;
    for (int index : flow.picked) {
        numbers.push_back(RoundNumber(flow.episodes[static_cast<size_t>(index)].number));
    }
    SetDlgItemTextW(dialog, IDC_ADD_SELECTION, selection::Collapse(numbers).c_str());
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

// Asks the source which players it has for one episode.
void StartHosters(HWND dialog, Flow& flow, int index) {
    if (index < 0 || static_cast<size_t>(index) >= flow.episodes.size() || flow.source < 0) {
        return;
    }

    const std::string& id = flow.sources[static_cast<size_t>(flow.source)].id;
    std::wstring library = flow.store->LibraryPath(id);
    std::map<std::string, std::string> config = flow.store->ReadConfig(id);
    std::string url = flow.episodes[static_cast<size_t>(index)].url;
    Http* http = flow.http;

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

}  // namespace

// --- the episodes window ----------------------------------------------------

namespace {

// Matches every control of the episodes window to the state.
void RefreshEpisodes(HWND dialog, const Flow& flow) {
    ShowWindow(GetDlgItem(dialog, IDC_ADD_SELECTION), flow.listMode ? SW_HIDE : SW_SHOW);
    ShowWindow(GetDlgItem(dialog, IDC_ADD_EPISODES), flow.listMode ? SW_SHOW : SW_HIDE);
    ShowWindow(GetDlgItem(dialog, IDC_ADD_HINT), flow.listMode ? SW_SHOW : SW_HIDE);
    ShowWindow(GetDlgItem(dialog, IDC_ADD_PLAYER),
               flow.playerOptions.empty() ? SW_HIDE : SW_SHOW);

    SetDlgItemTextW(dialog, IDC_ADD_COUNTER,
                    Format(STR_ADD_PICKED, static_cast<int>(flow.picked.size()),
                           static_cast<int>(flow.episodes.size()))
                        .c_str());
    for (int control : {IDC_ADD_PLAYER, IDC_ADD_MODE_TEXT, IDC_ADD_MODE_LIST}) {
        InvalidateRect(GetDlgItem(dialog, control), nullptr, TRUE);
    }
    EnableWindow(GetDlgItem(dialog, IDOK), !flow.picked.empty());
}

INT_PTR CALLBACK EpisodesProc(HWND dialog, UINT msg, WPARAM wParam, LPARAM lParam) {
    INT_PTR colour = 0;
    if (ThemeDialogMessage(msg, wParam, &colour)) {
        return colour;
    }
    auto* flow = reinterpret_cast<Flow*>(GetWindowLongPtrW(dialog, GWLP_USERDATA));

    switch (msg) {
    case WM_INITDIALOG: {
        SetWindowLongPtrW(dialog, GWLP_USERDATA, lParam);
        flow = reinterpret_cast<Flow*>(lParam);

        SetDialogTitle(dialog, STR_DLG_ADD_EPISODES_TITLE);
        SetDialogText(dialog, IDC_ADD_LBL_EPISODES, STR_DLG_ADD_EPISODES);
        SetDialogText(dialog, IDC_ADD_HINT, STR_ADD_PLAYER_HINT);
        SetDialogText(dialog, IDCANCEL, STR_DLG_CANCEL);

        // The palette goes on before the rows: theming a list resets the
        // state images its check boxes hang on.
        InitEpisodeList(dialog);
        ActiveTheme().ApplyToDialog(dialog);
        flow->filling = true;
        FillEpisodeList(dialog, *flow);
        ApplyChecks(dialog, *flow);
        flow->filling = false;
        WriteTyped(dialog, *flow);
        RefreshEpisodes(dialog, *flow);
        return TRUE;
    }

    case WM_DRAWITEM: {
        auto* draw = reinterpret_cast<DRAWITEMSTRUCT*>(lParam);
        if (flow == nullptr) {
            return FALSE;
        }
        if (draw->CtlID == IDC_ADD_PLAYER) {
            DrawPlayer(*draw, *flow);
            return TRUE;
        }
        if (draw->CtlID == IDC_ADD_MODE_TEXT) {
            DrawSegment(*draw, !flow->listMode, STR_ADD_MODE_TEXT);
            return TRUE;
        }
        if (draw->CtlID == IDC_ADD_MODE_LIST) {
            DrawSegment(*draw, flow->listMode, STR_ADD_MODE_LIST);
            return TRUE;
        }
        return FALSE;
    }

    case kHosters: {
        std::unique_ptr<Players> players(reinterpret_cast<Players*>(lParam));
        POINT where = {};
        GetCursorPos(&where);
        auto tuned = flow->playerByEpisode.find(players->episode);
        std::wstring current =
            tuned != flow->playerByEpisode.end() ? Widen(tuned->second) : flow->player;
        std::wstring chosen = PickPlayer(dialog, players->names, current, where);
        if (!chosen.empty()) {
            flow->playerByEpisode[players->episode] = Narrow(chosen);
            RefreshLabels(dialog, *flow);
        }
        return TRUE;
    }

    case WM_NOTIFY: {
        auto* notify = reinterpret_cast<NMHDR*>(lParam);
        if (flow == nullptr || flow->filling || notify->idFrom != IDC_ADD_EPISODES) {
            return FALSE;
        }
        if (notify->code == LVN_ITEMCHANGED) {
            auto* changed = reinterpret_cast<NMLISTVIEW*>(lParam);
            if ((changed->uChanged & LVIF_STATE) != 0 &&
                ((changed->uOldState ^ changed->uNewState) & LVIS_STATEIMAGEMASK) != 0) {
                ReadChecks(dialog, *flow);
                RefreshEpisodes(dialog, *flow);
            }
        } else if (notify->code == NM_RCLICK) {
            auto* clicked = reinterpret_cast<NMITEMACTIVATE*>(lParam);
            StartHosters(dialog, *flow, clicked->iItem);
        }
        return FALSE;
    }

    case WM_COMMAND: {
        if (flow == nullptr) {
            return FALSE;
        }
        switch (LOWORD(wParam)) {
        case IDC_ADD_PLAYER: {
            RECT bounds = {};
            GetWindowRect(GetDlgItem(dialog, IDC_ADD_PLAYER), &bounds);
            std::vector<std::string> names = {Narrow(kAuto)};
            for (const std::string& option : flow->playerOptions) {
                if (option != Narrow(kAuto)) {
                    names.push_back(option);
                }
            }
            POINT where = {bounds.left, bounds.bottom};
            std::wstring chosen = PickPlayer(dialog, names, flow->player, where);
            if (!chosen.empty()) {
                flow->player = chosen;
                RefreshEpisodes(dialog, *flow);
            }
            return TRUE;
        }
        case IDC_ADD_MODE_TEXT:
            if (flow->listMode) {
                ReadChecks(dialog, *flow);
                WriteTyped(dialog, *flow);
                flow->listMode = false;
                RefreshEpisodes(dialog, *flow);
            }
            return TRUE;
        case IDC_ADD_MODE_LIST:
            if (!flow->listMode) {
                ReadTyped(dialog, *flow);
                flow->filling = true;
                ApplyChecks(dialog, *flow);
                flow->filling = false;
                flow->listMode = true;
                RefreshEpisodes(dialog, *flow);
            }
            return TRUE;
        case IDC_ADD_SELECTION:
            if (HIWORD(wParam) == EN_CHANGE && !flow->listMode) {
                ReadTyped(dialog, *flow);
                RefreshEpisodes(dialog, *flow);
            }
            return TRUE;
        case IDOK:
            if (flow->listMode) {
                ReadChecks(dialog, *flow);
            } else {
                ReadTyped(dialog, *flow);
            }
            EndDialog(dialog, IDOK);
            return TRUE;
        case IDCANCEL:
            EndDialog(dialog, IDCANCEL);
            return TRUE;
        default:
            return FALSE;
        }
    }

    case WM_CLOSE:
        EndDialog(dialog, IDCANCEL);
        return TRUE;

    default:
        return FALSE;
    }
}

}  // namespace

// --- the information window -------------------------------------------------

namespace {

// Offers the folder-icon recipes; the row is dead while the option is off.
void FillTemplates(HWND dialog, Flow& flow) {
    bool on = flow.settings != nullptr && flow.settings->folderIcons;
    EnableWindow(GetDlgItem(dialog, IDC_ADD_TEMPLATE), on);
    SendDlgItemMessageW(dialog, IDC_ADD_TEMPLATE, CB_ADDSTRING, 0,
                        reinterpret_cast<LPARAM>(Str(STR_ADD_FOLDER_ICON_DEFAULT)));
    if (!on) {
        SendDlgItemMessageW(dialog, IDC_ADD_TEMPLATE, CB_SETCURSEL, 0, 0);
        return;
    }
    flow.templates = foldericon::TemplateIds();
    for (const std::string& id : flow.templates) {
        SendDlgItemMessageW(dialog, IDC_ADD_TEMPLATE, CB_ADDSTRING, 0,
                            reinterpret_cast<LPARAM>(Str(TemplateName(id))));
    }
    SendDlgItemMessageW(dialog, IDC_ADD_TEMPLATE, CB_SETCURSEL, 0, 0);
}

// Matches every control of the information window to the state.
void RefreshInfo(HWND dialog, const Flow& flow) {
    bool remember = IsDlgButtonChecked(dialog, IDC_ADD_REMEMBER) == BST_CHECKED;
    EnableWindow(GetDlgItem(dialog, IDC_ADD_REMEMBER_PATH), remember);
    if (remember) {
        SetDlgItemTextW(dialog, IDC_ADD_REMEMBER_PATH, ReadText(dialog, IDC_ADD_DEST).c_str());
    }

    SetDlgItemTextW(dialog, IDC_ADD_PICK,
                    Format(STR_ADD_PICKED, static_cast<int>(flow.picked.size()),
                           static_cast<int>(flow.episodes.size()))
                        .c_str());
    SetDlgItemTextW(dialog, IDC_ADD_COUNT,
                    Format(STR_ADD_EPISODE_COUNT, static_cast<int>(flow.episodes.size())).c_str());

    bool ready = !flow.picked.empty();
    EnableWindow(GetDlgItem(dialog, IDOK), ready);
    EnableWindow(GetDlgItem(dialog, IDC_ADD_LATER), ready);
}

// Gathers what the user chose into the answer handed to the caller.
void Accept(HWND dialog, Flow& flow, bool later) {
    if (flow.picked.empty() || flow.source < 0) {
        return;
    }

    AddRequest& request = *flow.request;
    request.addonId = flow.sources[static_cast<size_t>(flow.source)].id;
    request.animeTitle = flow.title;
    request.animeUrl = flow.url;
    request.posterUrl = flow.posterUrl;
    request.posterBytes = flow.posterBytes;
    request.destination = ReadText(dialog, IDC_ADD_DEST);
    request.rememberPath = IsDlgButtonChecked(dialog, IDC_ADD_REMEMBER) == BST_CHECKED;
    request.later = later;
    request.folderTemplate.clear();
    int recipe = static_cast<int>(SendDlgItemMessageW(dialog, IDC_ADD_TEMPLATE, CB_GETCURSEL, 0, 0));
    if (recipe > 0 && static_cast<size_t>(recipe) <= flow.templates.size()) {
        request.folderTemplate = flow.templates[static_cast<size_t>(recipe - 1)];
    }
    request.episodes.clear();

    for (int index : flow.picked) {
        const Episode& chosen = flow.episodes[static_cast<size_t>(index)];
        AddRequestEpisode episode;
        episode.number = chosen.number;
        episode.name = chosen.name;
        episode.url = chosen.url;

        auto tuned = flow.playerByEpisode.find(index);
        if (tuned != flow.playerByEpisode.end()) {
            episode.player = tuned->second;
        } else if (flow.player != kAuto) {
            episode.player = Narrow(flow.player);
        }
        request.episodes.push_back(std::move(episode));
    }
    EndDialog(dialog, IDOK);
}

INT_PTR CALLBACK InfoProc(HWND dialog, UINT msg, WPARAM wParam, LPARAM lParam) {
    INT_PTR colour = 0;
    if (ThemeDialogMessage(msg, wParam, &colour)) {
        return colour;
    }
    auto* flow = reinterpret_cast<Flow*>(GetWindowLongPtrW(dialog, GWLP_USERDATA));

    switch (msg) {
    case WM_INITDIALOG: {
        SetWindowLongPtrW(dialog, GWLP_USERDATA, lParam);
        flow = reinterpret_cast<Flow*>(lParam);

        SetDialogTitle(dialog, STR_DLG_ADD_INFO_TITLE);
        SetDialogText(dialog, IDC_ADD_LBL_INFO_URL, STR_ADD_URL_SHORT);
        SetDialogText(dialog, IDC_ADD_LBL_TEMPLATE, STR_ADD_FOLDER_ICON);
        SetDialogText(dialog, IDC_ADD_LBL_DEST, STR_ADD_SAVE_AS);
        SetDialogText(dialog, IDC_ADD_LBL_EPISODES, STR_DLG_ADD_EPISODES);
        SetDialogText(dialog, IDC_ADD_REMEMBER, STR_ADD_REMEMBER);
        SetDialogText(dialog, IDC_ADD_LATER, STR_ADD_LATER);
        SetDialogText(dialog, IDOK, STR_ADD_START);
        SetDialogText(dialog, IDCANCEL, STR_DLG_CANCEL);

        SetDlgItemTextW(dialog, IDC_ADD_INFO_URL, Widen(flow->url).c_str());
        FillTemplates(dialog, *flow);

        std::wstring folder = flow->settings != nullptr && flow->settings->rememberPath &&
                                      !flow->settings->savePath.empty()
                                  ? Widen(flow->settings->savePath)
                                  : DefaultDestination();
        SetDlgItemTextW(dialog, IDC_ADD_DEST, folder.c_str());
        CheckDlgButton(dialog, IDC_ADD_REMEMBER,
                       flow->settings != nullptr && flow->settings->rememberPath ? BST_CHECKED
                                                                                 : BST_UNCHECKED);

        if (!flow->posterBytes.empty() && flow->poster == nullptr) {
            RECT slot = {};
            GetClientRect(GetDlgItem(dialog, IDC_ADD_POSTER), &slot);
            flow->poster = image::Decode(flow->posterBytes, slot.right, slot.bottom);
        }

        ActiveTheme().ApplyToDialog(dialog);
        RefreshInfo(dialog, *flow);
        return TRUE;
    }

    case WM_DRAWITEM: {
        auto* draw = reinterpret_cast<DRAWITEMSTRUCT*>(lParam);
        if (flow != nullptr && draw->CtlID == IDC_ADD_POSTER) {
            DrawPoster(*draw, *flow);
            return TRUE;
        }
        return FALSE;
    }

    case WM_COMMAND: {
        if (flow == nullptr) {
            return FALSE;
        }
        int control = LOWORD(wParam);
        HINSTANCE instance =
            reinterpret_cast<HINSTANCE>(GetWindowLongPtrW(dialog, GWLP_HINSTANCE));

        if (control == IDC_ADD_POSTER && HIWORD(wParam) == STN_CLICKED) {
            ShowPosterPreview(dialog, instance, flow->posterBytes, flow->title, flow->posterUrl);
            return TRUE;
        }

        switch (control) {
        case IDC_ADD_BROWSE: {
            std::wstring folder = PickFolder(dialog);
            if (!folder.empty()) {
                SetDlgItemTextW(dialog, IDC_ADD_DEST, folder.c_str());
            }
            RefreshInfo(dialog, *flow);
            return TRUE;
        }
        case IDC_ADD_REMEMBER:
        case IDC_ADD_DEST:
            RefreshInfo(dialog, *flow);
            return TRUE;
        case IDC_ADD_PICK:
            DialogBoxParamW(instance, MAKEINTRESOURCEW(IDD_ADD_EPISODES), dialog, EpisodesProc,
                            reinterpret_cast<LPARAM>(flow));
            RefreshInfo(dialog, *flow);
            return TRUE;
        case IDC_ADD_LATER:
            Accept(dialog, *flow, true);
            return TRUE;
        case IDOK:
            Accept(dialog, *flow, false);
            return TRUE;
        case IDCANCEL:
            EndDialog(dialog, IDCANCEL);
            return TRUE;
        default:
            return FALSE;
        }
    }

    case WM_CLOSE:
        EndDialog(dialog, IDCANCEL);
        return TRUE;

    default:
        return FALSE;
    }
}

}  // namespace

// --- the address window -----------------------------------------------------

namespace {

// Fills the source list with what is installed.
void FillSources(HWND dialog, const Flow& flow) {
    HWND combo = GetDlgItem(dialog, IDC_ADD_SOURCE);
    for (const InstalledAddon& source : flow.sources) {
        std::wstring label = Widen(source.name) + L"  (" + Widen(source.lang) + L")";
        SendMessageW(combo, CB_ADDSTRING, 0, reinterpret_cast<LPARAM>(label.c_str()));
    }
    if (!flow.sources.empty()) {
        SendMessageW(combo, CB_SETCURSEL, 0, 0);
    } else {
        SendMessageW(combo, CB_ADDSTRING, 0, reinterpret_cast<LPARAM>(Str(STR_ADD_NO_SOURCE)));
        SendMessageW(combo, CB_SETCURSEL, 0, 0);
    }
}

int ChosenSource(HWND dialog, const Flow& flow) {
    int index = static_cast<int>(SendDlgItemMessageW(dialog, IDC_ADD_SOURCE, CB_GETCURSEL, 0, 0));
    return index >= 0 && static_cast<size_t>(index) < flow.sources.size() ? index : -1;
}

// Selects the source the address belongs to, when one claims that site.
void MatchSource(HWND dialog, const Flow& flow) {
    if (flow.hosts.size() != flow.sources.size()) {
        return;
    }
    std::string host = HostOf(Narrow(ReadText(dialog, IDC_ADD_URL)));
    for (size_t index = 0; index < flow.hosts.size(); ++index) {
        if (SameSite(host, flow.hosts[index])) {
            SendDlgItemMessageW(dialog, IDC_ADD_SOURCE, CB_SETCURSEL, index, 0);
            return;
        }
    }
}

// Whether the chosen source claims the site of the address; true as well when
// nothing is known about the sources yet, which forbids nothing.
bool SourceFits(HWND dialog, const Flow& flow) {
    int index = ChosenSource(dialog, flow);
    if (index < 0 || flow.hosts.size() != flow.sources.size()) {
        return true;
    }
    const std::string& host = flow.hosts[static_cast<size_t>(index)];
    return host.empty() || SameSite(HostOf(Narrow(ReadText(dialog, IDC_ADD_URL))), host);
}

// Reads the address every source declares, off the interface thread: each
// library has to be loaded to be asked.
void StartHosts(HWND dialog, const Flow& flow) {
    std::vector<std::wstring> libraries;
    std::vector<std::map<std::string, std::string>> configs;
    for (const InstalledAddon& source : flow.sources) {
        libraries.push_back(flow.store->LibraryPath(source.id));
        configs.push_back(flow.store->ReadConfig(source.id));
    }
    Http* http = flow.http;

    std::thread([dialog, libraries, configs, http] {
        auto* answer = new Hosts();
        for (size_t index = 0; index < libraries.size(); ++index) {
            std::string host;
            std::unique_ptr<Addon> addon = Addon::Load(libraries[index], *http, configs[index]);
            if (addon) {
                host = HostOf(addon->Meta().baseUrl);
            }
            answer->hosts.push_back(host);
        }
        if (!PostMessageW(dialog, kHosts, 0, reinterpret_cast<LPARAM>(answer))) {
            delete answer;
        }
    }).detach();
}

// Asks the source for the anime, its episodes, its cover and its players.
void StartLoad(HWND dialog, Flow& flow) {
    std::string url = Narrow(ReadText(dialog, IDC_ADD_URL));
    int index = ChosenSource(dialog, flow);
    if (url.empty() || index < 0) {
        return;
    }

    flow.busy = true;
    flow.url = url;
    flow.source = index;
    for (int control : {IDOK, IDCANCEL, IDC_ADD_URL, IDC_ADD_SOURCE}) {
        EnableWindow(GetDlgItem(dialog, control), FALSE);
    }
    SetDialogText(dialog, IDOK, STR_ADD_RESOLVING);

    const std::string& id = flow.sources[static_cast<size_t>(index)].id;
    std::wstring library = flow.store->LibraryPath(id);
    std::map<std::string, std::string> config = flow.store->ReadConfig(id);
    Http* http = flow.http;

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
            for (const AddonPreference& preference : addon->Preferences()) {
                if (preference.key == "preferred_player") {
                    listing->players = preference.options;
                    break;
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

INT_PTR CALLBACK UrlProc(HWND dialog, UINT msg, WPARAM wParam, LPARAM lParam) {
    INT_PTR colour = 0;
    if (ThemeDialogMessage(msg, wParam, &colour)) {
        return colour;
    }
    auto* flow = reinterpret_cast<Flow*>(GetWindowLongPtrW(dialog, GWLP_USERDATA));

    switch (msg) {
    case WM_INITDIALOG: {
        SetWindowLongPtrW(dialog, GWLP_USERDATA, lParam);
        flow = reinterpret_cast<Flow*>(lParam);

        SetDialogTitle(dialog, STR_DLG_ADD_TITLE);
        SetDialogText(dialog, IDC_ADD_LBL_URL, STR_ADD_ADDRESS);
        SetDialogText(dialog, IDC_ADD_LBL_SOURCE, STR_ADD_SOURCE_SHORT);
        SetDialogText(dialog, IDCANCEL, STR_DLG_CANCEL);

        FillSources(dialog, *flow);
        if (flow->settings != nullptr && flow->settings->clipboardUrl) {
            std::wstring pasted = ClipboardUrl(dialog);
            if (!pasted.empty()) {
                SetDlgItemTextW(dialog, IDC_ADD_URL, pasted.c_str());
            }
        }
        StartHosts(dialog, *flow);

        ActiveTheme().ApplyToDialog(dialog);
        EnableWindow(GetDlgItem(dialog, IDOK), !flow->sources.empty());
        return TRUE;
    }

    case kHosts: {
        std::unique_ptr<Hosts> answer(reinterpret_cast<Hosts*>(lParam));
        flow->hosts = std::move(answer->hosts);
        MatchSource(dialog, *flow);
        return TRUE;
    }

    case kLoaded: {
        std::unique_ptr<Listing> listing(reinterpret_cast<Listing*>(lParam));
        flow->busy = false;

        if (!listing->ok) {
            for (int control : {IDOK, IDCANCEL, IDC_ADD_URL, IDC_ADD_SOURCE}) {
                EnableWindow(GetDlgItem(dialog, control), TRUE);
            }
            SetDlgItemTextW(dialog, IDOK, L"OK");
            ShowNotice(dialog, reinterpret_cast<HINSTANCE>(
                                   GetWindowLongPtrW(dialog, GWLP_HINSTANCE)),
                       Str(STR_ADD_LOAD_FAILED));
            return TRUE;
        }

        flow->title = listing->title;
        flow->posterUrl = listing->posterUrl;
        flow->posterBytes = std::move(listing->poster);
        flow->episodes = std::move(listing->episodes);
        flow->playerOptions = std::move(listing->players);
        flow->playerByEpisode.clear();
        flow->picked.clear();
        for (size_t index = 0; index < flow->episodes.size(); ++index) {
            flow->picked.insert(static_cast<int>(index));
        }
        EndDialog(dialog, IDOK);
        return TRUE;
    }

    case WM_COMMAND: {
        if (flow == nullptr || flow->busy) {
            return FALSE;
        }
        int control = LOWORD(wParam);
        if (control == IDC_ADD_URL && HIWORD(wParam) == EN_CHANGE) {
            MatchSource(dialog, *flow);
            return TRUE;
        }
        if (control == IDOK) {
            HINSTANCE instance =
                reinterpret_cast<HINSTANCE>(GetWindowLongPtrW(dialog, GWLP_HINSTANCE));
            if (ReadText(dialog, IDC_ADD_URL).empty() || flow->sources.empty()) {
                ShowNotice(dialog, instance, Str(STR_ADD_NO_ADDRESS));
                return TRUE;
            }
            if (!SourceFits(dialog, *flow)) {
                Confirm question = {STR_DLG_ADD_TITLE, STR_ADD_SOURCE_MISMATCH, STR_DLG_ADD_START,
                                    STR_COUNT};
                if (!ShowConfirm(dialog, instance, &question)) {
                    return TRUE;
                }
            }
            StartLoad(dialog, *flow);
            return TRUE;
        }
        if (control == IDCANCEL) {
            EndDialog(dialog, IDCANCEL);
            return TRUE;
        }
        return FALSE;
    }

    case WM_CLOSE:
        if (flow == nullptr || !flow->busy) {
            EndDialog(dialog, IDCANCEL);
        }
        return TRUE;

    default:
        return FALSE;
    }
}

}  // namespace

// Runs the add flow: the address, then what the source answered about it.
INT_PTR ShowAddDialog(HWND owner, HINSTANCE instance, const AddonStore& store, Http& http,
                      const Settings& settings, AddRequest* request) {
    Flow flow;
    flow.store = &store;
    flow.http = &http;
    flow.settings = &settings;
    flow.request = request;
    flow.sources = store.Installed();

    INT_PTR answer = IDCANCEL;
    if (DialogBoxParamW(instance, MAKEINTRESOURCEW(IDD_ADD_URL), owner, UrlProc,
                        reinterpret_cast<LPARAM>(&flow)) == IDOK) {
        answer = DialogBoxParamW(instance, MAKEINTRESOURCEW(IDD_ADD_INFO), owner, InfoProc,
                                 reinterpret_cast<LPARAM>(&flow));
    }
    if (flow.poster != nullptr) {
        DeleteObject(flow.poster);
    }
    return answer;
}
