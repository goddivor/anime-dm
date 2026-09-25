#include <windows.h>
#include <commctrl.h>
#include <objbase.h>

#include <shellapi.h>

#include <string>

#include "core/Autostart.h"
#include "core/BridgeProtocol.h"
#include "core/Text.h"
#include "third_party/json.hpp"
#include "ui/AddDialog.h"
#include "ui/IconFactory.h"
#include "ui/MainWindow.h"

#ifndef ADM_VERSION
#define ADM_VERSION L"0.00"
#endif

namespace {

// What the command line hands over: an address after `--add`, and the
// episode wanted on it after `--episode`.
struct Handed {
    std::wstring url;
    std::wstring episode;
    bool tray = false;  // started with the session: the window stays hidden
};

Handed FromCommandLine(PWSTR commandLine) {
    int count = 0;
    LPWSTR* arguments = CommandLineToArgvW(commandLine, &count);
    Handed handed;
    if (arguments != nullptr) {
        for (int i = 0; i < count; ++i) {
            if (lstrcmpiW(arguments[i], autostart::kTraySwitch) == 0) {
                handed.tray = true;
            }
        }
        for (int i = 0; i + 1 < count; ++i) {
            if (lstrcmpiW(arguments[i], bridge::kAddSwitch) == 0) {
                handed.url = arguments[i + 1];
            } else if (lstrcmpiW(arguments[i], bridge::kEpisodeSwitch) == 0) {
                handed.episode = arguments[i + 1];
            }
        }
        LocalFree(arguments);
    }
    return handed;
}

// Hands an address to the instance already running; false when there is none.
bool HandToRunningInstance(const Handed& handed) {
    HWND window = FindWindowW(bridge::kWindowClass, nullptr);
    if (window == nullptr) {
        return false;
    }
    nlohmann::json message = {{"kind", "add"}, {"url", Narrow(handed.url)}};
    if (!handed.episode.empty()) {
        message["episode"] = Narrow(handed.episode);
    }
    std::string text = message.dump();
    COPYDATASTRUCT data = {};
    data.dwData = bridge::kCopyDataMark;
    data.cbData = static_cast<DWORD>(text.size());
    data.lpData = text.data();
    SendMessageW(window, WM_COPYDATA, 0, reinterpret_cast<LPARAM>(&data));
    SetForegroundWindow(window);
    return true;
}

}  // namespace

// Process entry point: enables visual styles, creates the window, pumps messages.
// A second instance hands its address to the first and leaves.
int WINAPI wWinMain(HINSTANCE instance, HINSTANCE, PWSTR commandLine, int cmdShow) {
    Handed handed = FromCommandLine(commandLine);
    HANDLE single = CreateMutexW(nullptr, TRUE, L"Local\\AnimeDm.Instance");
    if (single != nullptr && GetLastError() == ERROR_ALREADY_EXISTS) {
        // The running instance may sit hidden beside the clock: it is asked
        // to show itself, and allowed to come to the front.
        AllowSetForegroundWindow(ASFW_ANY);
        if (handed.url.empty()) {
            HWND window = FindWindowW(bridge::kWindowClass, nullptr);
            if (window != nullptr && !handed.tray) {
                std::string text = nlohmann::json({{"kind", "show"}}).dump();
                COPYDATASTRUCT data = {};
                data.dwData = bridge::kCopyDataMark;
                data.cbData = static_cast<DWORD>(text.size());
                data.lpData = text.data();
                SendMessageW(window, WM_COPYDATA, 0, reinterpret_cast<LPARAM>(&data));
            }
        } else {
            HandToRunningInstance(handed);
        }
        CloseHandle(single);
        return 0;
    }

    INITCOMMONCONTROLSEX icc = {};
    icc.dwSize = sizeof(icc);
    icc.dwICC = ICC_STANDARD_CLASSES | ICC_BAR_CLASSES | ICC_LISTVIEW_CLASSES |
                ICC_TREEVIEW_CLASSES | ICC_DATE_CLASSES | ICC_UPDOWN_CLASS;
    InitCommonControlsEx(&icc);
    CoInitializeEx(nullptr, COINIT_APARTMENTTHREADED | COINIT_DISABLE_OLE1DDE);

    GdiPlusRuntime gdiPlus;

    MainWindow window;
    if (!window.Create(instance, L"Anime Download Manager " ADM_VERSION)) {
        return 1;
    }
    // Started with the session, or by the browser extension for an add, the
    // application stays beside the clock: only the add window shows.
    window.Show(handed.tray || !handed.url.empty() ? SW_HIDE : cmdShow);
    if (!handed.url.empty()) {
        window.AddFromOutside(Narrow(handed.url), Narrow(handed.episode));
    }

    HACCEL accel = window.Accelerator();
    MSG msg = {};
    while (GetMessageW(&msg, nullptr, 0, 0)) {
        // The add windows take their keyboard first; the shortcuts of the
        // main window apply to it alone, or Delete typed in an address would
        // remove the selected downloads.
        if (IsAddWindowMessage(&msg)) {
            continue;
        }
        bool mine = GetAncestor(msg.hwnd, GA_ROOT) == window.Handle();
        if (!mine || !TranslateAcceleratorW(window.Handle(), accel, &msg)) {
            TranslateMessage(&msg);
            DispatchMessageW(&msg);
        }
    }
    CoUninitialize();
    return static_cast<int>(msg.wParam);
}
