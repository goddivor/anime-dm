#pragma once

#include <windows.h>

#include <string>
#include <vector>

#include "core/Follow.h"

// One anime the follow dialog can offer.
struct FollowChoice {
    std::string animeUrl;
    std::string title;
    std::string addonId;
    std::wstring destination;  // the folder the anime folder lives in
};

// What the follow dialog edits: the follow, and the animes to pick from when
// the follow names none yet.
struct FollowScreen {
    FollowedAnime* follow = nullptr;
    std::vector<FollowChoice> choices;
    bool editing = false;  // the anime is fixed, only the rest changes
};

// Shows the follow dialog; true when OK filled the follow.
bool ShowFollowDialog(HWND owner, HINSTANCE instance, FollowScreen* screen);
