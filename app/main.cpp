#include <windows.h>
#include <commctrl.h>
#include <objbase.h>

#include <shellapi.h>

#include <string>

#include "core/BridgeProtocol.h"
#include "core/Text.h"
#include "third_party/json.hpp"
#include "ui/IconFactory.h"
#include "ui/MainWindow.h"

namespace {

// The address given on the command line after `--add`, or nothing.
std::wstring AddressToAdd(PWSTR commandLine) {
    int count = 0;
    LPWSTR* arguments = CommandLineToArgvW(commandLine, &count);
    std::wstring url;
    if (arguments != nullptr) {
        for (int i = 0; i + 1 < count; ++i) {
            if (lstrcmpiW(arguments[i], bridge::kAddSwitch) == 0) {
                url = arguments[i + 1];
            }
        }
        LocalFree(arguments);
    }
    return url;
}

// Hands an address to the instance already running; false when there is none.
bool HandToRunningInstance(const std::wstring& url) {
    HWND window = FindWindowW(bridge::kWindowClass, nullptr);
    if (window == nullptr) {
        return false;
    }
    std::string text = nlohmann::json({{"kind", "add"}, {"url", Narrow(url)}}).dump();
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
    std::wstring address = AddressToAdd(commandLine);
    HANDLE single = CreateMutexW(nullptr, TRUE, L"Local\\AnimeDm.Instance");
    if (single != nullptr && GetLastError() == ERROR_ALREADY_EXISTS) {
        if (address.empty()) {
            HWND window = FindWindowW(bridge::kWindowClass, nullptr);
            if (window != nullptr) {
                SetForegroundWindow(window);
            }
        } else {
            HandToRunningInstance(address);
        }
        CloseHandle(single);
        return 0;
    }

    INITCOMMONCONTROLSEX icc = {};
    icc.dwSize = sizeof(icc);
    icc.dwICC = ICC_STANDARD_CLASSES | ICC_BAR_CLASSES | ICC_LISTVIEW_CLASSES | ICC_TREEVIEW_CLASSES;
    InitCommonControlsEx(&icc);
    CoInitializeEx(nullptr, COINIT_APARTMENTTHREADED | COINIT_DISABLE_OLE1DDE);

    GdiPlusRuntime gdiPlus;

    MainWindow window;
    if (!window.Create(instance, L"Anime Download Manager")) {
        return 1;
    }
    window.Show(cmdShow);
    if (!address.empty()) {
        window.AddFromOutside(Narrow(address));
    }

    HACCEL accel = window.Accelerator();
    MSG msg = {};
    while (GetMessageW(&msg, nullptr, 0, 0)) {
        if (!TranslateAcceleratorW(window.Handle(), accel, &msg)) {
            TranslateMessage(&msg);
            DispatchMessageW(&msg);
        }
    }
    CoUninitialize();
    return static_cast<int>(msg.wParam);
}
