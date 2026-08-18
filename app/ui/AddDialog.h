#pragma once

#include <string>
#include <vector>

#include <windows.h>

class AddonStore;
class Http;

// One episode the user picked.
struct AddRequestEpisode {
    double number = 0.0;
    std::string name;
    std::string url;
};

// What the dialog hands back when the user confirms.
struct AddRequest {
    std::string addonId;
    std::string animeTitle;
    std::string animeUrl;
    std::wstring destination;
    std::vector<AddRequestEpisode> episodes;
};

// Shows the add dialog. Returns IDOK and fills `request` when confirmed.
INT_PTR ShowAddDialog(HWND owner, HINSTANCE instance, const AddonStore& store, Http& http,
                      AddRequest* request);
