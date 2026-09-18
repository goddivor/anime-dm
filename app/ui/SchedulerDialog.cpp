#include "ui/SchedulerDialog.h"

#include <commctrl.h>

#include <string>

#include "ui/DownloadsView.h"
#include "ui/Resource.h"
#include "ui/Strings.h"
#include "ui/Theme.h"

namespace {

// The dialog and the copy of the schedules it edits until OK.
struct Editing {
    SchedulerScreen* screen = nullptr;
    Scheduler draft;
    QueueKind shown = QueueKind::Main;
};

QueueKind QueueAt(int index) {
    return index == 1 ? QueueKind::Scheduler : QueueKind::Main;
}

int IndexOf(QueueKind queue) {
    return queue == QueueKind::Scheduler ? 1 : 0;
}

// Sets a date picker to a local date, or to today when the schedule has none.
void SetDate(HWND picker, int year, int month, int day) {
    SYSTEMTIME time = {};
    GetLocalTime(&time);
    if (year > 0) {
        time.wYear = static_cast<WORD>(year);
        time.wMonth = static_cast<WORD>(month);
        time.wDay = static_cast<WORD>(day);
    }
    DateTime_SetSystemtime(picker, GDT_VALID, &time);
}

// Sets a time picker to an hour of the day.
void SetClock(HWND picker, int hour, int minute) {
    SYSTEMTIME time = {};
    GetLocalTime(&time);
    time.wHour = static_cast<WORD>(hour);
    time.wMinute = static_cast<WORD>(minute);
    time.wSecond = 0;
    DateTime_SetSystemtime(picker, GDT_VALID, &time);
}

// Greys the schedule controls that the switches above them turn off.
void SyncEnabled(HWND dialog) {
    bool enabled = IsDlgButtonChecked(dialog, IDC_SCH_ENABLED) == BST_CHECKED;
    bool daily = IsDlgButtonChecked(dialog, IDC_SCH_DAILY) == BST_CHECKED;
    bool stop = IsDlgButtonChecked(dialog, IDC_SCH_STOP) == BST_CHECKED;
    for (int id : {IDC_SCH_ONCE, IDC_SCH_DAILY, IDC_SCH_LBL_START, IDC_SCH_START, IDC_SCH_STOP,
                   IDC_SCH_LBL_DONE, IDC_SCH_DONE}) {
        EnableWindow(GetDlgItem(dialog, id), enabled);
    }
    EnableWindow(GetDlgItem(dialog, IDC_SCH_DATE), enabled && !daily);
    for (int id = IDC_SCH_DAY0; id <= IDC_SCH_DAY6; ++id) {
        EnableWindow(GetDlgItem(dialog, id), enabled && daily);
    }
    EnableWindow(GetDlgItem(dialog, IDC_SCH_STOP_TIME), enabled && stop);
}

// Shows one schedule in the controls.
void ShowSchedule(HWND dialog, const QueueSchedule& schedule) {
    CheckDlgButton(dialog, IDC_SCH_ENABLED, schedule.enabled ? BST_CHECKED : BST_UNCHECKED);
    CheckRadioButton(dialog, IDC_SCH_ONCE, IDC_SCH_DAILY,
                     schedule.daily ? IDC_SCH_DAILY : IDC_SCH_ONCE);
    SetDate(GetDlgItem(dialog, IDC_SCH_DATE), schedule.year, schedule.month, schedule.day);
    for (int i = 0; i < 7; ++i) {
        CheckDlgButton(dialog, IDC_SCH_DAY0 + i, schedule.days[i] ? BST_CHECKED : BST_UNCHECKED);
    }
    SetClock(GetDlgItem(dialog, IDC_SCH_START), schedule.startHour, schedule.startMinute);
    CheckDlgButton(dialog, IDC_SCH_STOP, schedule.stopEnabled ? BST_CHECKED : BST_UNCHECKED);
    SetClock(GetDlgItem(dialog, IDC_SCH_STOP_TIME), schedule.stopHour, schedule.stopMinute);
    SendDlgItemMessageW(dialog, IDC_SCH_DONE, CB_SETCURSEL, static_cast<int>(schedule.whenDone), 0);
    SyncEnabled(dialog);
}

// Reads the controls back into one schedule.
void ReadSchedule(HWND dialog, QueueSchedule* schedule) {
    schedule->enabled = IsDlgButtonChecked(dialog, IDC_SCH_ENABLED) == BST_CHECKED;
    schedule->daily = IsDlgButtonChecked(dialog, IDC_SCH_DAILY) == BST_CHECKED;
    SYSTEMTIME time = {};
    if (DateTime_GetSystemtime(GetDlgItem(dialog, IDC_SCH_DATE), &time) == GDT_VALID) {
        schedule->year = time.wYear;
        schedule->month = time.wMonth;
        schedule->day = time.wDay;
    }
    for (int i = 0; i < 7; ++i) {
        schedule->days[i] = IsDlgButtonChecked(dialog, IDC_SCH_DAY0 + i) == BST_CHECKED;
    }
    if (DateTime_GetSystemtime(GetDlgItem(dialog, IDC_SCH_START), &time) == GDT_VALID) {
        schedule->startHour = time.wHour;
        schedule->startMinute = time.wMinute;
    }
    schedule->stopEnabled = IsDlgButtonChecked(dialog, IDC_SCH_STOP) == BST_CHECKED;
    if (DateTime_GetSystemtime(GetDlgItem(dialog, IDC_SCH_STOP_TIME), &time) == GDT_VALID) {
        schedule->stopHour = time.wHour;
        schedule->stopMinute = time.wMinute;
    }
    int done = static_cast<int>(SendDlgItemMessageW(dialog, IDC_SCH_DONE, CB_GETCURSEL, 0, 0));
    schedule->whenDone = done == 1   ? QueueSchedule::WhenDone::Quit
                         : done == 2 ? QueueSchedule::WhenDone::Shutdown
                                     : QueueSchedule::WhenDone::Nothing;
}

// Whether an item of the queue is running, which turns Start now into Stop.
bool QueueRunning(const Editing& editing, QueueKind queue) {
    for (const DownloadItem& item : *editing.screen->items) {
        if (item.queue == queue && IsActive(item.status)) {
            return true;
        }
    }
    return false;
}

// Lists the files of the shown queue with their status.
void FillFiles(HWND dialog, const Editing& editing) {
    HWND list = GetDlgItem(dialog, IDC_SCH_FILES);
    ListView_DeleteAllItems(list);
    int row = 0;
    for (const DownloadItem& item : *editing.screen->items) {
        if (item.queue != editing.shown) {
            continue;
        }
        size_t cut = item.outPath.find_last_of(L"\\/");
        std::wstring name = cut == std::wstring::npos ? item.outPath : item.outPath.substr(cut + 1);
        LVITEMW entry = {};
        entry.mask = LVIF_TEXT;
        entry.iItem = row;
        entry.pszText = name.data();
        ListView_InsertItem(list, &entry);
        std::wstring status = StatusText(item);
        ListView_SetItemText(list, row, 1, status.data());
        ++row;
    }
    if (row == 0) {
        LVITEMW entry = {};
        entry.mask = LVIF_TEXT;
        entry.pszText = const_cast<wchar_t*>(Str(STR_SCH_EMPTY));
        ListView_InsertItem(list, &entry);
    }
    SetDlgItemTextW(dialog, IDC_SCH_RUN,
                    Str(QueueRunning(editing, editing.shown) ? STR_SCH_HALT : STR_SCH_RUN));
}

// Switches the window to another queue, keeping what was typed for the one
// it leaves.
void ShowQueue(HWND dialog, Editing& editing, QueueKind queue) {
    ReadSchedule(dialog, &editing.draft.Of(editing.shown));
    editing.shown = queue;
    ShowSchedule(dialog, editing.draft.Of(queue));
    FillFiles(dialog, editing);
}

// Applies the captions, fills the lists and shows the first queue.
void InitControls(HWND dialog, Editing& editing) {
    SetDialogTitle(dialog, STR_SCH_TITLE);
    SetDialogText(dialog, IDC_SCH_LBL_QUEUE, STR_SCH_QUEUE);
    SetDialogText(dialog, IDC_SCH_ENABLED, STR_SCH_ENABLED);
    SetDialogText(dialog, IDC_SCH_ONCE, STR_SCH_ONCE);
    SetDialogText(dialog, IDC_SCH_DAILY, STR_SCH_DAILY);
    SetDialogText(dialog, IDC_SCH_LBL_START, STR_SCH_START);
    SetDialogText(dialog, IDC_SCH_STOP, STR_SCH_STOP);
    SetDialogText(dialog, IDC_SCH_LBL_DONE, STR_SCH_DONE);
    SetDialogText(dialog, IDC_SCH_LBL_FILES, STR_SCH_FILES);
    SetDialogText(dialog, IDOK, STR_DLG_OK);
    SetDialogText(dialog, IDCANCEL, STR_DLG_CANCEL);

    std::wstring days = Str(STR_SCH_DAYS);
    size_t from = 0;
    for (int i = 0; i < 7; ++i) {
        size_t bar = days.find(L'|', from);
        std::wstring day = days.substr(from, bar == std::wstring::npos ? bar : bar - from);
        SetDlgItemTextW(dialog, IDC_SCH_DAY0 + i, day.c_str());
        from = bar == std::wstring::npos ? days.size() : bar + 1;
    }

    for (StringId id : {STR_QUEUE_MAIN, STR_QUEUE_SCHEDULER}) {
        SendDlgItemMessageW(dialog, IDC_SCH_QUEUE, CB_ADDSTRING, 0,
                            reinterpret_cast<LPARAM>(Str(id)));
    }
    for (StringId id : {STR_SCH_DONE_NOTHING, STR_SCH_DONE_QUIT, STR_SCH_DONE_SHUTDOWN}) {
        SendDlgItemMessageW(dialog, IDC_SCH_DONE, CB_ADDSTRING, 0,
                            reinterpret_cast<LPARAM>(Str(id)));
    }

    for (int id : {IDC_SCH_START, IDC_SCH_STOP_TIME}) {
        DateTime_SetFormat(GetDlgItem(dialog, id), L"HH:mm");
    }

    HWND list = GetDlgItem(dialog, IDC_SCH_FILES);
    ListView_SetExtendedListViewStyle(list, LVS_EX_FULLROWSELECT);
    RECT area = {};
    GetClientRect(list, &area);
    LVCOLUMNW column = {};
    column.mask = LVCF_WIDTH;
    column.cx = area.right * 2 / 3;
    ListView_InsertColumn(list, 0, &column);
    ListView_InsertColumn(list, 1, &column);
    ListView_SetColumnWidth(list, 1, LVSCW_AUTOSIZE_USEHEADER);

    editing.shown = editing.screen->initial;
    SendDlgItemMessageW(dialog, IDC_SCH_QUEUE, CB_SETCURSEL, IndexOf(editing.shown), 0);
    ShowSchedule(dialog, editing.draft.Of(editing.shown));
    FillFiles(dialog, editing);
}

// Dialog procedure: one queue at a time, OK keeps both.
INT_PTR CALLBACK SchedulerProc(HWND dialog, UINT msg, WPARAM wParam, LPARAM lParam) {
    INT_PTR colour = 0;
    if (ThemeDialogMessage(msg, wParam, &colour)) {
        return colour;
    }
    auto* editing = reinterpret_cast<Editing*>(GetWindowLongPtrW(dialog, GWLP_USERDATA));
    switch (msg) {
    case WM_INITDIALOG:
        SetWindowLongPtrW(dialog, GWLP_USERDATA, lParam);
        editing = reinterpret_cast<Editing*>(lParam);
        ActiveTheme().ApplyToDialog(dialog);
        InitControls(dialog, *editing);
        return TRUE;
    case WM_COMMAND:
        switch (LOWORD(wParam)) {
        case IDC_SCH_QUEUE:
            if (HIWORD(wParam) == CBN_SELCHANGE) {
                int index = static_cast<int>(
                    SendDlgItemMessageW(dialog, IDC_SCH_QUEUE, CB_GETCURSEL, 0, 0));
                ShowQueue(dialog, *editing, QueueAt(index));
            }
            return TRUE;
        case IDC_SCH_ENABLED:
        case IDC_SCH_ONCE:
        case IDC_SCH_DAILY:
        case IDC_SCH_STOP:
            SyncEnabled(dialog);
            return TRUE;
        case IDC_SCH_RUN:
            editing->screen->run(editing->shown, !QueueRunning(*editing, editing->shown));
            FillFiles(dialog, *editing);
            return TRUE;
        case IDOK:
            ReadSchedule(dialog, &editing->draft.Of(editing->shown));
            *editing->screen->scheduler = editing->draft;
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

// Shows the scheduler window over the schedules; Cancel leaves them alone.
bool ShowSchedulerDialog(HWND owner, HINSTANCE instance, SchedulerScreen* screen) {
    Editing editing;
    editing.screen = screen;
    editing.draft = *screen->scheduler;
    return DialogBoxParamW(instance, MAKEINTRESOURCEW(IDD_SCHEDULER), owner, SchedulerProc,
                           reinterpret_cast<LPARAM>(&editing)) == IDOK;
}
