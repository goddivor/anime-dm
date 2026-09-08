// Runs the download engine on one episode and prints what it reports, so the
// whole chain can be checked without launching the application:
//
//   download-smoke <addon id> <episode URL> <output path> [player]
//
// The source must be installed in the registry the application uses.

#include <cstdio>
#include <memory>
#include <string>

#include <windows.h>

#include "core/AddonStore.h"
#include "core/Download.h"
#include "core/Downloader.h"
#include "core/Http.h"
#include "core/Text.h"

namespace {

constexpr UINT kEvent = WM_APP + 1;
constexpr wchar_t kClass[] = L"DownloadSmoke";

const char* Name(DownloadStatus status) {
    switch (status) {
    case DownloadStatus::Queued:
        return "queued";
    case DownloadStatus::Resolving:
        return "resolving";
    case DownloadStatus::Downloading:
        return "downloading";
    case DownloadStatus::Assembling:
        return "assembling";
    case DownloadStatus::Completed:
        return "completed";
    case DownloadStatus::Failed:
        return "failed";
    case DownloadStatus::Stopped:
        return "stopped";
    }
    return "?";
}

// Prints every event and stops the loop once the download settled.
LRESULT CALLBACK Proc(HWND window, UINT msg, WPARAM wParam, LPARAM lParam) {
    if (msg != kEvent) {
        return DefWindowProcW(window, msg, wParam, lParam);
    }
    std::unique_ptr<DownloadEvent> event(reinterpret_cast<DownloadEvent*>(lParam));
    std::printf("%-12s done=%llu total=%llu fraction=%.3f speed=%.0f KB/s %s %s\n",
                Name(event->status), static_cast<unsigned long long>(event->done),
                static_cast<unsigned long long>(event->total), event->fraction,
                event->speed / 1024.0, event->address.c_str(), event->detail.c_str());
    std::fflush(stdout);
    if (event->status == DownloadStatus::Completed || event->status == DownloadStatus::Failed ||
        event->status == DownloadStatus::Stopped) {
        std::printf("path: %s\n", Narrow(event->outPath).c_str());
        PostQuitMessage(event->status == DownloadStatus::Completed ? 0 : 2);
    }
    return 0;
}

}  // namespace

int wmain(int argc, wchar_t** argv) {
    if (argc < 4) {
        std::fprintf(stderr, "usage: download-smoke <addon id> <episode URL> <output> [player]\n");
        return 1;
    }

    WNDCLASSW wc = {};
    wc.lpfnWndProc = Proc;
    wc.lpszClassName = kClass;
    wc.hInstance = GetModuleHandleW(nullptr);
    RegisterClassW(&wc);
    HWND window = CreateWindowExW(0, kClass, L"", 0, 0, 0, 0, 0, HWND_MESSAGE, nullptr,
                                  wc.hInstance, nullptr);

    Http http;
    AddonStore store(http);
    Downloader downloader(http, store);
    downloader.Attach(window, kEvent);

    DownloadTask task;
    task.id = 1;
    task.addonId = Narrow(argv[1]);
    task.pageUrl = Narrow(argv[2]);
    task.outPath = argv[3];
    task.player = argc > 4 ? Narrow(argv[4]) : std::string();
    downloader.Start(task);

    MSG msg = {};
    while (GetMessageW(&msg, nullptr, 0, 0)) {
        DispatchMessageW(&msg);
    }
    return static_cast<int>(msg.wParam);
}
