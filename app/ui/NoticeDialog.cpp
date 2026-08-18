#include "ui/NoticeDialog.h"

#include "ui/Resource.h"
#include "ui/Theme.h"
#include "ui/Strings.h"

namespace {

// Dialog procedure: shows the caller's message and closes on OK.
INT_PTR CALLBACK NoticeDialogProc(HWND dialog, UINT msg, WPARAM wParam, LPARAM lParam) {
    INT_PTR colour = 0;
    if (ThemeDialogMessage(msg, wParam, &colour)) {
        return colour;
    }

    switch (msg) {
    case WM_INITDIALOG:
        ActiveTheme().ApplyToDialog(dialog);
        SetDialogTitle(dialog, STR_NOTICE_TITLE);
        SetDialogText(dialog, IDOK, STR_DLG_OK);
        SetDlgItemTextW(dialog, IDC_NOTICE_TEXT, reinterpret_cast<const wchar_t*>(lParam));
        return TRUE;
    case WM_COMMAND:
        if (LOWORD(wParam) == IDOK || LOWORD(wParam) == IDCANCEL) {
            EndDialog(dialog, LOWORD(wParam));
            return TRUE;
        }
        return FALSE;
    case WM_CLOSE:
        EndDialog(dialog, IDCANCEL);
        return TRUE;
    default:
        return FALSE;
    }
}

}  // namespace

// Shows a short message in its own small dialog.
void ShowNotice(HWND owner, HINSTANCE instance, const wchar_t* message) {
    DialogBoxParamW(instance, MAKEINTRESOURCEW(IDD_NOTICE), owner, NoticeDialogProc,
                    reinterpret_cast<LPARAM>(message));
}
