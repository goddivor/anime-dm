#pragma once

#include <cstdint>
#include <map>
#include <string>
#include <vector>

#include <windows.h>

class AddonStore;
class Http;

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
    std::wstring destination;
    std::vector<AddRequestEpisode> episodes;
};

// Shows the add dialog. Returns IDOK and fills `request` when confirmed.
INT_PTR ShowAddDialog(HWND owner, HINSTANCE instance, const AddonStore& store, Http& http,
                      AddRequest* request);
