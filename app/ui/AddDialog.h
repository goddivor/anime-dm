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

// What the dialog hands back when the user confirms.
struct AddRequest {
    std::string addonId;
    std::string animeTitle;
    std::string animeUrl;
    std::string posterUrl;
    std::vector<uint8_t> posterBytes;  // what the dialog already fetched, if anything
    std::string folderTemplate;        // a recipe id; empty means the default one
    std::wstring destination;
    bool rememberPath = false;  // keep the destination as the default of the next adds
    bool later = false;         // queued without being handed to the engine
    std::vector<AddRequestEpisode> episodes;
};

// Shows the add dialog. Returns IDOK and fills `request` when confirmed. An
// `initialUrl`, as the browser extension hands one, fills the address and
// starts the reading of the page as soon as the source is known; an
// `initialEpisode` picks that one episode alone once the list is read.
INT_PTR ShowAddDialog(HWND owner, HINSTANCE instance, const AddonStore& store, Http& http,
                      const Settings& settings, AddRequest* request,
                      const std::string& initialUrl = std::string(),
                      const std::string& initialEpisode = std::string());

// Offers a list of episodes in the episodes window of the add flow, those
// `ticked` names already ticked, under `caption`. On OK, `chosen` holds the
// episodes kept, each with the player picked for it. True when the user kept
// at least one.
bool PickEpisodes(HWND owner, HINSTANCE instance, const AddonStore& store, Http& http,
                  const std::string& addonId, const std::wstring& caption,
                  const std::vector<AddRequestEpisode>& offered, const std::vector<bool>& ticked,
                  std::vector<AddRequestEpisode>* chosen);
