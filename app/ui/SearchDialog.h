#pragma once

#include <cstdint>
#include <functional>
#include <vector>

#include <windows.h>

#include "core/Download.h"

// Shows the search window over the queue. The rows are filtered as the text
// is typed; a double-click or Enter hands the chosen item to `reveal`, which
// the owner uses to select it in the list.
INT_PTR ShowSearchDialog(HWND owner, HINSTANCE instance, const std::vector<DownloadItem>& items,
                         const std::function<void(uint64_t)>& reveal);
