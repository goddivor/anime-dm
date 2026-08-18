#pragma once

#include <windows.h>

class AddonStore;
class Http;

// Shows the modal addon store window. Returns IDOK or IDCANCEL.
INT_PTR ShowAddonsDialog(HWND owner, HINSTANCE instance, const AddonStore& store, Http& http);
