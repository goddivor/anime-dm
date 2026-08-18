#include <windows.h>
#include <commctrl.h>

#include "ui/IconFactory.h"
#include "ui/MainWindow.h"

// Process entry point: enables visual styles, creates the window, pumps messages.
int WINAPI wWinMain(HINSTANCE instance, HINSTANCE, PWSTR, int cmdShow) {
    INITCOMMONCONTROLSEX icc = {};
    icc.dwSize = sizeof(icc);
    icc.dwICC = ICC_STANDARD_CLASSES | ICC_BAR_CLASSES | ICC_LISTVIEW_CLASSES | ICC_TREEVIEW_CLASSES;
    InitCommonControlsEx(&icc);

    GdiPlusRuntime gdiPlus;

    MainWindow window;
    if (!window.Create(instance, L"Anime Download Manager")) {
        return 1;
    }
    window.Show(cmdShow);

    HACCEL accel = window.Accelerator();
    MSG msg = {};
    while (GetMessageW(&msg, nullptr, 0, 0)) {
        if (!TranslateAcceleratorW(window.Handle(), accel, &msg)) {
            TranslateMessage(&msg);
            DispatchMessageW(&msg);
        }
    }
    return static_cast<int>(msg.wParam);
}
