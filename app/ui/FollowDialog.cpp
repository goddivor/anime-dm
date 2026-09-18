#include "ui/FollowDialog.h"

#include <commctrl.h>

#include "core/Text.h"
#include "ui/Resource.h"
#include "ui/Strings.h"
#include "ui/Theme.h"

namespace {

// Fills the combos and shows the follow.
void InitControls(HWND dialog, FollowScreen& screen) {
    SetDialogTitle(dialog, STR_FOLLOW_TITLE);
    SetDialogText(dialog, IDC_FOLLOW_LBL_ANIME, STR_FOLLOW_ANIME);
    SetDialogText(dialog, IDC_FOLLOW_LBL_DAY, STR_FOLLOW_DAY);
    SetDialogText(dialog, IDC_FOLLOW_LBL_TIME, STR_FOLLOW_TIME);
    SetDialogText(dialog, IDC_FOLLOW_LBL_QUEUE, STR_FOLLOW_QUEUE);
    SetDialogText(dialog, IDC_FOLLOW_START, STR_FOLLOW_START);
    SetDialogText(dialog, IDC_FOLLOW_HINT, STR_FOLLOW_HINT);
    SetDialogText(dialog, IDOK, STR_DLG_OK);
    SetDialogText(dialog, IDCANCEL, STR_DLG_CANCEL);

    const FollowedAnime& follow = *screen.follow;
    int chosen = 0;
    for (size_t i = 0; i < screen.choices.size(); ++i) {
        SendDlgItemMessageW(dialog, IDC_FOLLOW_ANIME, CB_ADDSTRING, 0,
                            reinterpret_cast<LPARAM>(Widen(screen.choices[i].title).c_str()));
        if (screen.choices[i].animeUrl == follow.animeUrl) {
            chosen = static_cast<int>(i);
        }
    }
    SendDlgItemMessageW(dialog, IDC_FOLLOW_ANIME, CB_SETCURSEL, chosen, 0);
    EnableWindow(GetDlgItem(dialog, IDC_FOLLOW_ANIME), screen.editing ? FALSE : TRUE);

    std::wstring days = Str(STR_DAYS_LONG);
    size_t from = 0;
    while (from <= days.size()) {
        size_t bar = days.find(L'|', from);
        std::wstring day = days.substr(from, bar == std::wstring::npos ? bar : bar - from);
        SendDlgItemMessageW(dialog, IDC_FOLLOW_DAY, CB_ADDSTRING, 0,
                            reinterpret_cast<LPARAM>(day.c_str()));
        if (bar == std::wstring::npos) {
            break;
        }
        from = bar + 1;
    }
    SendDlgItemMessageW(dialog, IDC_FOLLOW_DAY, CB_SETCURSEL, follow.releaseDay, 0);

    HWND clock = GetDlgItem(dialog, IDC_FOLLOW_TIME);
    DateTime_SetFormat(clock, L"HH:mm");
    SYSTEMTIME time = {};
    GetLocalTime(&time);
    time.wHour = static_cast<WORD>(follow.releaseHour);
    time.wMinute = static_cast<WORD>(follow.releaseMinute);
    time.wSecond = 0;
    DateTime_SetSystemtime(clock, GDT_VALID, &time);

    for (StringId id : {STR_QUEUE_MAIN, STR_QUEUE_SCHEDULER}) {
        SendDlgItemMessageW(dialog, IDC_FOLLOW_QUEUE, CB_ADDSTRING, 0,
                            reinterpret_cast<LPARAM>(Str(id)));
    }
    SendDlgItemMessageW(dialog, IDC_FOLLOW_QUEUE, CB_SETCURSEL,
                        follow.queue == QueueKind::Main ? 0 : 1, 0);
    CheckDlgButton(dialog, IDC_FOLLOW_START, follow.startAtOnce ? BST_CHECKED : BST_UNCHECKED);
}

// Reads the controls back into the follow.
void ReadControls(HWND dialog, FollowScreen& screen) {
    FollowedAnime& follow = *screen.follow;
    int chosen = static_cast<int>(SendDlgItemMessageW(dialog, IDC_FOLLOW_ANIME, CB_GETCURSEL, 0, 0));
    if (chosen >= 0 && static_cast<size_t>(chosen) < screen.choices.size()) {
        const FollowChoice& choice = screen.choices[static_cast<size_t>(chosen)];
        follow.animeUrl = choice.animeUrl;
        follow.title = choice.title;
        follow.addonId = choice.addonId;
        follow.destination = choice.destination;
    }
    follow.releaseDay =
        static_cast<int>(SendDlgItemMessageW(dialog, IDC_FOLLOW_DAY, CB_GETCURSEL, 0, 0));
    if (follow.releaseDay < 0) {
        follow.releaseDay = 0;
    }
    SYSTEMTIME time = {};
    if (DateTime_GetSystemtime(GetDlgItem(dialog, IDC_FOLLOW_TIME), &time) == GDT_VALID) {
        follow.releaseHour = time.wHour;
        follow.releaseMinute = time.wMinute;
    }
    follow.queue = SendDlgItemMessageW(dialog, IDC_FOLLOW_QUEUE, CB_GETCURSEL, 0, 0) == 0
                       ? QueueKind::Main
                       : QueueKind::Scheduler;
    follow.startAtOnce = IsDlgButtonChecked(dialog, IDC_FOLLOW_START) == BST_CHECKED;
}

INT_PTR CALLBACK FollowProc(HWND dialog, UINT msg, WPARAM wParam, LPARAM lParam) {
    INT_PTR colour = 0;
    if (ThemeDialogMessage(msg, wParam, &colour)) {
        return colour;
    }
    auto* screen = reinterpret_cast<FollowScreen*>(GetWindowLongPtrW(dialog, GWLP_USERDATA));
    switch (msg) {
    case WM_INITDIALOG:
        SetWindowLongPtrW(dialog, GWLP_USERDATA, lParam);
        screen = reinterpret_cast<FollowScreen*>(lParam);
        ActiveTheme().ApplyToDialog(dialog);
        InitControls(dialog, *screen);
        return TRUE;
    case WM_COMMAND:
        switch (LOWORD(wParam)) {
        case IDOK:
            ReadControls(dialog, *screen);
            EndDialog(dialog, IDOK);
            return TRUE;
        case IDCANCEL:
            EndDialog(dialog, IDCANCEL);
            return TRUE;
        default:
            return FALSE;
        }
    case WM_CLOSE:
        EndDialog(dialog, IDCANCEL);
        return TRUE;
    default:
        return FALSE;
    }
}

}  // namespace

// Shows the follow dialog over the follow; Cancel leaves it alone.
bool ShowFollowDialog(HWND owner, HINSTANCE instance, FollowScreen* screen) {
    if (screen->choices.empty()) {
        return false;
    }
    return DialogBoxParamW(instance, MAKEINTRESOURCEW(IDD_FOLLOW), owner, FollowProc,
                           reinterpret_cast<LPARAM>(screen)) == IDOK;
}
