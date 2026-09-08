// Runs the download engine on one episode and prints what it reports, so the
// whole chain can be checked without launching the application:
//
//   download-smoke <addon id> <episode URL> <output path> [player]
//   download-smoke --url <video URL> <output path> [referer]
//
// The first form resolves the episode through an installed source; the second
// fetches a video the caller already knows, playlist or file.

#include <atomic>
#include <cstdio>
#include <memory>
#include <string>

#include <windows.h>

#include "core/AddonStore.h"
#include "core/Download.h"
#include "core/Downloader.h"
#include "core/Http.h"
#include "core/Paths.h"
#include "core/Text.h"
#include "core/Transfer.h"

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

// Fetches a known video URL through the transfer layer alone.
int FetchUrl(const std::string& url, const std::wstring& outPath, const std::string& referer) {
    Http http;
    std::atomic<bool> stop{false};
    std::atomic<bool> discard{false};
    TransferControl control{stop, discard};
    TransferSource source;
    source.url = url;
    if (!referer.empty()) {
        source.headers["Referer"] = referer;
    }

    std::wstring parts = paths::PartsDir(0);
    std::unique_ptr<Transfer> transfer = OpenTransfer(http, source, parts, control);
    if (!transfer) {
        std::printf("the host refused the video\n");
        return 2;
    }
    std::printf("kind: %s total=%llu\n", transfer->Extension().empty() ? "ranges" : "playlist",
                static_cast<unsigned long long>(transfer->Total()));

    bool fetched = transfer->Run(
        Downloader::kConnections, [](uint64_t done, uint64_t total, double fraction, double speed) {
            std::printf("downloading  done=%llu total=%llu fraction=%.3f speed=%.0f KB/s\n",
                        static_cast<unsigned long long>(done),
                        static_cast<unsigned long long>(total), fraction, speed / 1024.0);
            std::fflush(stdout);
        });
    if (!fetched) {
        std::printf("failed: error=%d %s\n", static_cast<int>(control.error),
                    control.detail.c_str());
        return 2;
    }

    std::wstring extension = transfer->Extension();
    std::wstring finalPath = outPath;
    if (!extension.empty()) {
        size_t dot = finalPath.find_last_of(L'.');
        finalPath = (dot == std::wstring::npos ? finalPath : finalPath.substr(0, dot)) + extension;
    }
    std::printf("assembling\n");
    if (!transfer->Assemble(finalPath)) {
        std::printf("assembly failed\n");
        return 2;
    }
    paths::RemoveTree(parts);
    std::printf("completed\npath: %s\n", Narrow(finalPath).c_str());
    return 0;
}

}  // namespace

int wmain(int argc, wchar_t** argv) {
    if (argc >= 4 && std::wstring(argv[1]) == L"--url") {
        return FetchUrl(Narrow(argv[2]), argv[3], argc > 4 ? Narrow(argv[4]) : std::string());
    }
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
