#pragma once

#include <string>

#include <windows.h>

class AddonStore;
class Http;

// Shows the settings a source declares, as a form built at runtime.
// Returns IDOK when the user saved.
INT_PTR ShowAddonConfigDialog(HWND owner, HINSTANCE instance, const AddonStore& store, Http& http,
                              const std::string& addonId);
