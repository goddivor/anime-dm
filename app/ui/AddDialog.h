#pragma once

#include <cstdint>
#include <map>
#include <string>
#include <vector>

#include <windows.h>

class AddonStore;
class Http;
struct Settings;

// One episode the user picked, with the player it should use.
struct AddRequestEpisode {
    double number = 0.0;
    std::string name;
    std::string url;
    std::string player;  // empty means the source decides
};

// What an add window hands to the main window when the user confirms.
struct AddRequest {
    std::string addonId;
    std::string animeTitle;
    std::string animeUrl;
    std::string posterUrl;
    std::vector<uint8_t> posterBytes;  // what the window already fetched, if anything
    std::string folderTemplate;        // a recipe id; empty means the default one
    std::wstring destination;
    bool rememberPath = false;  // keep the destination as the default of the next adds
    bool later = false;         // queued without being handed to the engine
    std::vector<AddRequestEpisode> episodes;
};

// Where an add window reports: the confirmed request goes to `window` as the
// lParam of `doneMessage`, a heap AddRequest the receiver then owns.
struct AddTarget {
    HWND window = nullptr;
    UINT doneMessage = 0;
};

// Opens an add window of its own, which lives beside the main window and
// any other add window, with a button in the taskbar. An `initialUrl`, as the
// browser extension or a batch hands one, fills the address and reads the
// page at once, from `sourceId` when the source is already known; the
// `initialEpisodes` alone are ticked once the list is read, all of them
// otherwise.
void OpenAddWindow(HINSTANCE instance, const AddonStore& store, Http& http,
                   const Settings& settings, const AddTarget& target,
                   const std::string& initialUrl = std::string(),
                   const std::vector<std::string>& initialEpisodes = {},
                   const std::string& sourceId = std::string());

// Hands a message to the add window it belongs to, for its keyboard; true
// when one took it.
bool IsAddWindowMessage(MSG* msg);

// Closes every add window, as the application quits.
void CloseAddWindows();

// Offers a list of episodes in the episodes window of the add flow, those
// `ticked` names already ticked, under `caption`. On OK, `chosen` holds the
// episodes kept, each with the player picked for it. True when the user kept
// at least one.
bool PickEpisodes(HWND owner, HINSTANCE instance, const AddonStore& store, Http& http,
                  const std::string& addonId, const std::wstring& caption,
                  const std::vector<AddRequestEpisode>& offered, const std::vector<bool>& ticked,
                  std::vector<AddRequestEpisode>* chosen);
