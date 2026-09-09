#pragma once

#include <windows.h>

#include "core/Settings.h"

// Shows the options dialog over `settings`, which it updates as the user
// goes. Returns true when anything changed.
bool ShowSettingsDialog(HWND owner, HINSTANCE instance, Settings* settings);
