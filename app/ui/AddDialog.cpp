#include "ui/AddDialog.h"

#include <commctrl.h>
#include <shlobj.h>

#include <memory>
#include <string>
#include <thread>
#include <vector>

#include "core/Addon.h"
#include "core/AddonStore.h"
#include "core/Http.h"
#include "core/Text.h"
#include "ui/Resource.h"
#include "ui/Strings.h"
#include "ui/Theme.h"

namespace {

constexpr UINT kLoaded = WM_APP + 1;

// What a load brought back from the source.
struct Listing {
    bool ok = false;
    std::string title;
    std::vector<AddRequestEpisode> episodes;
};

// What the dialog keeps for the whole of its life.
struct Screen {
    const AddonStore* store = nullptr;
    Http* http = nullptr;
    AddRequest* request = nullptr;
    std::vector<InstalledAddon> sources;
    std::vector<AddRequestEpisode> episodes;
    std::string animeTitle;
    std::string animeUrl;
    bool busy = false;
};

// Reads the text of a control.
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

// The folder the system keeps downloads in, as a first destination.
std::wstring DefaultDestination() {
    PWSTR folder = nullptr;
    if (FAILED(SHGetKnownFolderPath(FOLDERID_Downloads, 0, nullptr, &folder))) {
        return std::wstring();
    }
    std::wstring path(folder);
    CoTaskMemFree(folder);
    return path;
}

// Asks the user for a destination folder.
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

// Formats an episode number the way the file names will.
std::wstring EpisodeLabel(double number) {
    wchar_t text[32] = {};
    if (number == static_cast<double>(static_cast<long>(number))) {
        wsprintfW(text, L"Ep %03ld", static_cast<long>(number));
    } else {
        swprintf(text, ARRAYSIZE(text), L"Ep %.1f", number);
    }
    return text;
}

// Fills the source list with what is installed.
void FillSources(HWND dialog, Screen& screen) {
    HWND combo = GetDlgItem(dialog, IDC_ADD_SOURCE);
    for (const InstalledAddon& source : screen.sources) {
        SendMessageW(combo, CB_ADDSTRING, 0, reinterpret_cast<LPARAM>(Widen(source.name).c_str()));
    }
    if (!screen.sources.empty()) {
        SendMessageW(combo, CB_SETCURSEL, 0, 0);
    } else {
        SendMessageW(combo, CB_ADDSTRING, 0, reinterpret_cast<LPARAM>(Str(STR_ADD_NO_SOURCE)));
        SendMessageW(combo, CB_SETCURSEL, 0, 0);
        EnableWindow(GetDlgItem(dialog, IDC_ADD_FETCH), FALSE);
    }
}

// Configures the episodes list with its checkboxes and columns.
void InitEpisodesList(HWND dialog) {
    HWND list = GetDlgItem(dialog, IDC_ADD_EPISODES);
    ListView_SetExtendedListViewStyle(
        list, LVS_EX_CHECKBOXES | LVS_EX_FULLROWSELECT | LVS_EX_DOUBLEBUFFER);

    LVCOLUMNW col = {};
    col.mask = LVCF_TEXT | LVCF_WIDTH | LVCF_SUBITEM;

    col.iSubItem = 0;
    col.cx = 90;
    col.pszText = const_cast<wchar_t*>(Str(STR_DLG_ADD_EPISODES));
    ListView_InsertColumn(list, 0, &col);

    col.iSubItem = 1;
    col.cx = 500;
    col.pszText = const_cast<wchar_t*>(Str(STR_ADD_EPISODE_TITLE));
    ListView_InsertColumn(list, 1, &col);
}

// Shows what the source answered, every episode ticked.
void FillEpisodes(HWND dialog, const Screen& screen) {
    HWND list = GetDlgItem(dialog, IDC_ADD_EPISODES);
    ListView_DeleteAllItems(list);

    int row = 0;
    for (const AddRequestEpisode& episode : screen.episodes) {
        std::wstring label = EpisodeLabel(episode.number);
        LVITEMW item = {};
        item.mask = LVIF_TEXT;
        item.iItem = row;
        item.pszText = label.data();
        ListView_InsertItem(list, &item);

        std::wstring name = Widen(episode.name);
        ListView_SetItemText(list, row, 1, name.data());
        ListView_SetCheckState(list, row, TRUE);
        ++row;
    }
}

void SyncButtons(HWND dialog, const Screen& screen) {
    bool idle = !screen.busy;
    EnableWindow(GetDlgItem(dialog, IDC_ADD_FETCH), idle && !screen.sources.empty());
    EnableWindow(GetDlgItem(dialog, IDC_ADD_BROWSE), idle);
    EnableWindow(GetDlgItem(dialog, IDOK), idle && !screen.episodes.empty());
    EnableWindow(GetDlgItem(dialog, IDCANCEL), idle);
}

// Asks the source for the anime and its episodes, off the interface thread.
void StartLoad(HWND dialog, Screen& screen) {
    int index = static_cast<int>(SendDlgItemMessageW(dialog, IDC_ADD_SOURCE, CB_GETCURSEL, 0, 0));
    if (index < 0 || static_cast<size_t>(index) >= screen.sources.size()) {
        return;
    }
    std::string url = Narrow(ReadText(dialog, IDC_ADD_URL));
    if (url.empty()) {
        return;
    }

    screen.busy = true;
    screen.animeUrl = url;
    SyncButtons(dialog, screen);

    std::string addonId = screen.sources[static_cast<size_t>(index)].id;
    std::wstring library = screen.store->LibraryPath(addonId);
    std::map<std::string, std::string> config = screen.store->ReadConfig(addonId);

    std::thread([dialog, &screen, library, config, url] {
        auto* listing = new Listing();
        std::unique_ptr<Addon> addon = Addon::Load(library, *screen.http, config);
        if (addon) {
            nlohmann::json input = {{"url", url}};
            if (std::optional<nlohmann::json> details = addon->Call("adm_anime_details", input)) {
                listing->title = details->value("title", std::string());
            }
            if (std::optional<nlohmann::json> episodes = addon->Call("adm_episode_list", input)) {
                if (episodes->is_array()) {
                    for (const nlohmann::json& item : *episodes) {
                        AddRequestEpisode episode;
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
        if (!PostMessageW(dialog, kLoaded, 0, reinterpret_cast<LPARAM>(listing))) {
            delete listing;
        }
    }).detach();
}

// Gathers the ticked episodes into the answer handed to the caller.
void Confirm(HWND dialog, Screen& screen) {
    HWND list = GetDlgItem(dialog, IDC_ADD_EPISODES);
    int index = static_cast<int>(SendDlgItemMessageW(dialog, IDC_ADD_SOURCE, CB_GETCURSEL, 0, 0));

    AddRequest& request = *screen.request;
    request.addonId = index >= 0 && static_cast<size_t>(index) < screen.sources.size()
                          ? screen.sources[static_cast<size_t>(index)].id
                          : std::string();
    request.animeTitle = screen.animeTitle;
    request.animeUrl = screen.animeUrl;
    request.destination = ReadText(dialog, IDC_ADD_DEST);
    request.episodes.clear();

    for (size_t row = 0; row < screen.episodes.size(); ++row) {
        if (ListView_GetCheckState(list, static_cast<int>(row))) {
            request.episodes.push_back(screen.episodes[row]);
        }
    }

    if (request.episodes.empty()) {
        return;
    }
    EndDialog(dialog, IDOK);
}

void Retranslate(HWND dialog) {
    SetDialogTitle(dialog, STR_DLG_ADD_TITLE);
    SetDialogText(dialog, IDC_ADD_LBL_SOURCE, STR_DLG_ADD_SOURCE);
    SetDialogText(dialog, IDC_ADD_LBL_URL, STR_DLG_ADD_URL);
    SetDialogText(dialog, IDC_ADD_FETCH, STR_DLG_ADD_FETCH);
    SetDialogText(dialog, IDC_ADD_LBL_EPISODES, STR_DLG_ADD_EPISODES);
    SetDialogText(dialog, IDC_ADD_LBL_DEST, STR_DLG_ADD_DEST);
    SetDialogText(dialog, IDC_ADD_BROWSE, STR_DLG_BROWSE);
    SetDialogText(dialog, IDOK, STR_DLG_ADD_START);
    SetDialogText(dialog, IDCANCEL, STR_DLG_CANCEL);
}

INT_PTR CALLBACK AddDialogProc(HWND dialog, UINT msg, WPARAM wParam, LPARAM lParam) {
    INT_PTR colour = 0;
    if (ThemeDialogMessage(msg, wParam, &colour)) {
        return colour;
    }

    auto* screen = reinterpret_cast<Screen*>(GetWindowLongPtrW(dialog, GWLP_USERDATA));

    switch (msg) {
    case WM_INITDIALOG:
        SetWindowLongPtrW(dialog, GWLP_USERDATA, lParam);
        screen = reinterpret_cast<Screen*>(lParam);
        Retranslate(dialog);
        FillSources(dialog, *screen);
        InitEpisodesList(dialog);
        SetDlgItemTextW(dialog, IDC_ADD_DEST, DefaultDestination().c_str());
        ActiveTheme().ApplyToDialog(dialog);
        SyncButtons(dialog, *screen);
        return TRUE;

    case kLoaded: {
        std::unique_ptr<Listing> listing(reinterpret_cast<Listing*>(lParam));
        screen->busy = false;
        if (listing->ok) {
            screen->animeTitle = listing->title;
            screen->episodes = std::move(listing->episodes);
            FillEpisodes(dialog, *screen);
            if (!screen->animeTitle.empty()) {
                SetWindowTextW(dialog, Widen(screen->animeTitle).c_str());
            }
        } else {
            screen->episodes.clear();
            ListView_DeleteAllItems(GetDlgItem(dialog, IDC_ADD_EPISODES));
            MessageBoxW(dialog, Str(STR_ADD_LOAD_FAILED), Str(STR_DLG_ADD_TITLE),
                        MB_OK | MB_ICONWARNING);
        }
        SyncButtons(dialog, *screen);
        return TRUE;
    }

    case WM_COMMAND:
        if (screen == nullptr || screen->busy) {
            return TRUE;
        }
        switch (LOWORD(wParam)) {
        case IDC_ADD_FETCH:
            StartLoad(dialog, *screen);
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
