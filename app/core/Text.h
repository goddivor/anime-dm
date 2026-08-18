#pragma once

#include <string>

// The application speaks UTF-16 to Windows and UTF-8 to the addons, the store
// index and every file it writes.
std::wstring Widen(const std::string& utf8);
std::string Narrow(const std::wstring& text);
