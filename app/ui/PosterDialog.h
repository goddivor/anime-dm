#pragma once

#include <cstdint>
#include <string>
#include <vector>

#include <windows.h>

// Shows the cover at full size, with a way to save it.
void ShowPosterPreview(HWND owner, HINSTANCE instance, const std::vector<uint8_t>& bytes,
                       const std::string& title, const std::string& sourceUrl);
