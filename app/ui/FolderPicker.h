#pragma once

#include <windows.h>

#include <string>

// Lets the user pick a folder; empty when they gave up.
std::wstring PickFolder(HWND owner);
