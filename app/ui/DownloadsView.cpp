#include "ui/DownloadsView.h"

#include <commctrl.h>
#include <windowsx.h>

#include "core/Text.h"
#include "ui/Format.h"
#include "ui/Strings.h"

namespace {
struct Column {
    StringId title;
    int width;
};

constexpr Column kColumns[] = {
    {STR_COL_FILENAME, 320},
    {STR_COL_SIZE, 90},
    {STR_COL_STATUS, 140},
    {STR_COL_TIME_LEFT, 110},
    {STR_COL_SPEED, 110},
    {STR_COL_LAST_TRY, 130},
    {STR_COL_ADDED, 130},
    {STR_COL_LOCATION, 220},
    {STR_COL_ADDRESS, 280},
    {STR_COL_PARENT_PAGE, 260},
};

enum ColumnIndex {
    COL_FILENAME,
    COL_SIZE,
    COL_STATUS,
    COL_TIME_LEFT,
    COL_SPEED,
    COL_LAST_TRY,
    COL_ADDED,
    COL_LOCATION,
    COL_ADDRESS,
    COL_PARENT_PAGE,
};

// What the interface says about a failure.
StringId ErrorText(DownloadError error) {
    switch (error) {
    case DownloadError::Source:
        return STR_ERR_SOURCE;
    case DownloadError::NoPlayer:
        return STR_ERR_NO_PLAYER;
    case DownloadError::NoVideo:
        return STR_ERR_NO_VIDEO;
    case DownloadError::Playlist:
        return STR_ERR_PLAYLIST;
    case DownloadError::Key:
        return STR_ERR_KEY;
    case DownloadError::Disk:
        return STR_ERR_DISK;
    default:
        return STR_ERR_NETWORK;
    }
}

// Sets one cell of a row.
void SetCell(HWND list, int row, int column, const std::wstring& text) {
    ListView_SetItemText(list, row, column, const_cast<wchar_t*>(text.c_str()));
}

constexpr int kWheelStep = 40;  // pixels of sideways travel per wheel notch
constexpr int kMinColumnWidth = 40;  // a column can shrink, not vanish

// Whether the pointer of a wheel message rests on this window.
bool WheelIsOver(HWND window, LPARAM lParam) {
    POINT at = {GET_X_LPARAM(lParam), GET_Y_LPARAM(lParam)};
    RECT bounds = {};
    GetWindowRect(window, &bounds);
    return PtInRect(&bounds, at) != FALSE;
}


// Keeps the vertical scroll bar on screen even while the rows fit, the way
// IDM frames its list, and repaints the whole list after any scroll. The
// control scrolls by shifting the pixels it already has and painting only the
// strip that appears, which leaves the rows painted by the window out of step
// with the header for a moment and smears them; a full repaint through the
// double buffer costs nothing visible and settles it. Ctrl or Shift with the
// wheel travels sideways, which the control does not do by itself.
LRESULT CALLBACK KeepScrollBar(HWND list, UINT msg, WPARAM wParam, LPARAM lParam,
                               UINT_PTR, DWORD_PTR) {
    if (msg == WM_NCDESTROY) {
        RemoveWindowSubclass(list, KeepScrollBar, 1);
        return DefSubclassProc(list, msg, wParam, lParam);
    }
    if ((msg == WM_MOUSEWHEEL || msg == WM_MOUSEHWHEEL) && !WheelIsOver(list, lParam)) {
        return SendMessageW(GetParent(list), msg, wParam, lParam);
    }
    if (msg == WM_NOTIFY) {
        auto* header = reinterpret_cast<NMHEADERW*>(lParam);
        if ((header->hdr.code == HDN_ITEMCHANGINGW || header->hdr.code == HDN_ITEMCHANGINGA) &&
            header->pitem != nullptr && (header->pitem->mask & HDI_WIDTH) != 0) {
            // A column being dragged narrower stops at the minimum instead of
            // closing on itself.
            HDC dc = GetDC(list);
            int least = MulDiv(kMinColumnWidth, GetDeviceCaps(dc, LOGPIXELSX), 96);
            ReleaseDC(list, dc);
            if (header->pitem->cxy < least) {
                header->pitem->cxy = least;
            }
        }
    }
    if (msg == WM_MOUSEWHEEL && (GET_KEYSTATE_WPARAM(wParam) & (MK_CONTROL | MK_SHIFT)) != 0) {
        int notches = GET_WHEEL_DELTA_WPARAM(wParam) / WHEEL_DELTA;
        ListView_Scroll(list, -notches * kWheelStep, 0);
        InvalidateRect(list, nullptr, FALSE);
        return 0;
    }

    int horizontal = GetScrollPos(list, SB_HORZ);
    int vertical = GetScrollPos(list, SB_VERT);
    LRESULT result = DefSubclassProc(list, msg, wParam, lParam);
    if (GetScrollPos(list, SB_HORZ) != horizontal || GetScrollPos(list, SB_VERT) != vertical) {
        InvalidateRect(list, nullptr, FALSE);
    }

    static thread_local bool restoring = false;
    if (!restoring && (GetWindowLongPtrW(list, GWL_STYLE) & WS_VSCROLL) == 0) {
        restoring = true;
        // SetScrollInfo only disables a bar that is still there: a bar the
        // control removed has to be shown again before its empty range makes
        // Windows disable it rather than remove it.
        ShowScrollBar(list, SB_VERT, TRUE);
        SCROLLINFO info = {};
        info.cbSize = sizeof(info);
        info.fMask = SIF_RANGE | SIF_PAGE | SIF_DISABLENOSCROLL;
        info.nMin = 0;
        info.nMax = 0;
        info.nPage = 1;
        SetScrollInfo(list, SB_VERT, &info, TRUE);
        restoring = false;
    }
    return result;
}

}  // namespace

// The text of the status cell.
std::wstring StatusText(const DownloadItem& item) {
    switch (item.status) {
    case DownloadStatus::Queued:
        return Str(STR_STATUS_PENDING);
    case DownloadStatus::Resolving:
        return Str(STR_STATUS_RESOLVING);
    case DownloadStatus::Downloading:
        if (item.fraction < 0.0) {
            return Str(STR_STATUS_STARTING);
        }
        return std::to_wstring(static_cast<int>(item.fraction * 100.0)) + L" %";
    case DownloadStatus::Assembling:
        return Str(STR_STATUS_ASSEMBLING);
    case DownloadStatus::Completed:
        return Str(STR_STATUS_COMPLETED);
    case DownloadStatus::Failed: {
        std::wstring text = Str(STR_STATUS_FAILED);
        text += L" : ";
        text += Str(ErrorText(item.error));
        if (!item.detail.empty()) {
            text += L" (" + Widen(item.detail) + L")";
        }
        return text;
    }
    case DownloadStatus::Stopped:
        return Str(STR_STATUS_STOPPED);
    }
    return std::wstring();
}

// Creates the ListView child and configures its columns and extended styles.
bool DownloadsView::Create(HWND parent, HINSTANCE instance) {
    hwnd_ = CreateWindowExW(
        0, WC_LISTVIEWW, L"",
        WS_CHILD | WS_VISIBLE | WS_BORDER | LVS_REPORT | LVS_SHOWSELALWAYS,
        0, 0, 0, 0,
        parent, nullptr, instance, nullptr);
    if (hwnd_ == nullptr) {
        return false;
    }

    ListView_SetExtendedListViewStyle(
        hwnd_, LVS_EX_FULLROWSELECT | LVS_EX_GRIDLINES | LVS_EX_DOUBLEBUFFER);
    AddColumns();
    SetWindowSubclass(hwnd_, KeepScrollBar, 1, 0);
    return true;
}

// Inserts the report columns in declaration order.
void DownloadsView::AddColumns() {
    LVCOLUMNW col = {};
    col.mask = LVCF_TEXT | LVCF_WIDTH | LVCF_SUBITEM;
    int index = 0;
    for (const Column& column : kColumns) {
        col.iSubItem = index;
        col.cx = column.width;
        col.pszText = const_cast<wchar_t*>(Str(column.title));
        ListView_InsertColumn(hwnd_, index, &col);
        ++index;
    }
}

// Inserts the row of an item, or refreshes it when it is already there.
void DownloadsView::Upsert(const DownloadItem& item) {
    int row = RowOf(item.id);
    if (row < 0) {
        LVITEMW entry = {};
        entry.mask = LVIF_TEXT | LVIF_PARAM;
        entry.iItem = ListView_GetItemCount(hwnd_);
        entry.pszText = const_cast<wchar_t*>(L"");
        entry.lParam = static_cast<LPARAM>(item.id);
        row = ListView_InsertItem(hwnd_, &entry);
    }
    Fill(row, item);
}

// Writes every cell of a row from its item.
void DownloadsView::Fill(int row, const DownloadItem& item) {
    SetCell(hwnd_, row, COL_FILENAME, FileNameOf(item.outPath));
    SetCell(hwnd_, row, COL_SIZE, format::Size(item.total > 0 ? item.total : item.done));
    SetCell(hwnd_, row, COL_STATUS, StatusText(item));

    bool downloading = item.status == DownloadStatus::Downloading;
    double remaining = -1.0;
    if (downloading && item.speed > 0.0) {
        if (item.total > item.done) {
            remaining = static_cast<double>(item.total - item.done) / item.speed;
        } else if (item.fraction > 0.0 && item.done > 0) {
            double estimated = static_cast<double>(item.done) / item.fraction;
            remaining = (estimated - static_cast<double>(item.done)) / item.speed;
        }
    }
    // Nothing is left to say about the time and the rate of a transfer that
    // is not running: the cells stay empty rather than holding a dash.
    SetCell(hwnd_, row, COL_TIME_LEFT, remaining < 0.0 ? L"" : format::Duration(remaining));
    SetCell(hwnd_, row, COL_SPEED,
            downloading && item.speed > 0.0 ? format::Speed(item.speed) : L"");
    SetCell(hwnd_, row, COL_LAST_TRY, format::Date(item.lastTry));
    SetCell(hwnd_, row, COL_ADDED, format::Date(item.addedAt));
    size_t cut = item.outPath.find_last_of(L"\\/");
    SetCell(hwnd_, row, COL_LOCATION, cut == std::wstring::npos ? L"" : item.outPath.substr(0, cut));
    SetCell(hwnd_, row, COL_ADDRESS, Widen(item.pageUrl));
    SetCell(hwnd_, row, COL_PARENT_PAGE, Widen(item.animeUrl));
}

// Marks the column the rows are sorted by, or none, in the header.
void DownloadsView::SetSortMark(int column, bool ascending) {
    HWND header = ListView_GetHeader(hwnd_);
    int count = Header_GetItemCount(header);
    for (int index = 0; index < count; ++index) {
        HDITEMW item = {};
        item.mask = HDI_FORMAT;
        Header_GetItem(header, index, &item);
        item.fmt &= ~(HDF_SORTUP | HDF_SORTDOWN);
        if (index == column) {
            item.fmt |= ascending ? HDF_SORTUP : HDF_SORTDOWN;
        }
        Header_SetItem(header, index, &item);
    }
    InvalidateRect(header, nullptr, FALSE);
}

namespace {
// What the sort callback of the list is handed: the caller's ordering.
struct SortContext {
    const std::function<bool(uint64_t, uint64_t)>* before;
};

int CALLBACK CompareRows(LPARAM first, LPARAM second, LPARAM data) {
    const auto* context = reinterpret_cast<const SortContext*>(data);
    uint64_t a = static_cast<uint64_t>(first);
    uint64_t b = static_cast<uint64_t>(second);
    if ((*context->before)(a, b)) {
        return -1;
    }
    return (*context->before)(b, a) ? 1 : 0;
}
}  // namespace

// Reorders the rows; the items of the rows are handed to `before` by id.
void DownloadsView::Sort(const std::function<bool(uint64_t, uint64_t)>& before) {
    SortContext context = {&before};
    ListView_SortItems(hwnd_, CompareRows, reinterpret_cast<LPARAM>(&context));
    InvalidateRect(hwnd_, nullptr, FALSE);
}

// Drops the row of an item.
void DownloadsView::Remove(uint64_t id) {
    int row = RowOf(id);
    if (row >= 0) {
        ListView_DeleteItem(hwnd_, row);
    }
}

// Drops every row.
void DownloadsView::Clear() {
    ListView_DeleteAllItems(hwnd_);
}

// The ids of the selected rows, top to bottom.
std::vector<uint64_t> DownloadsView::Selected() const {
    std::vector<uint64_t> ids;
    int row = -1;
    while ((row = ListView_GetNextItem(hwnd_, row, LVNI_SELECTED)) >= 0) {
        ids.push_back(IdAt(row));
    }
    return ids;
}

// The row of an item, or -1.
int DownloadsView::RowOf(uint64_t id) const {
    LVFINDINFOW find = {};
    find.flags = LVFI_PARAM;
    find.lParam = static_cast<LPARAM>(id);
    return ListView_FindItem(hwnd_, -1, &find);
}

// The id of a row, or zero.
uint64_t DownloadsView::IdAt(int row) const {
    LVITEMW entry = {};
    entry.mask = LVIF_PARAM;
    entry.iItem = row;
    if (!ListView_GetItem(hwnd_, &entry)) {
        return 0;
    }
    return static_cast<uint64_t>(entry.lParam);
}

int DownloadsView::Count() const {
    return ListView_GetItemCount(hwnd_);
}

// Refreshes the column captions after a language change.
void DownloadsView::Retranslate() {
    LVCOLUMNW col = {};
    col.mask = LVCF_TEXT;
    int index = 0;
    for (const Column& column : kColumns) {
        col.pszText = const_cast<wchar_t*>(Str(column.title));
        ListView_SetColumn(hwnd_, index, &col);
        ++index;
    }
}

// Queues the move of the list into a deferred batch, without copying its
// old pixels, so the frame and the rules are painted afresh where it lands.
HDWP DownloadsView::Place(HDWP batch, int x, int y, int width, int height) {
    return DeferWindowPos(batch, hwnd_, nullptr, x, y, width, height,
                          SWP_NOZORDER | SWP_NOACTIVATE | SWP_NOCOPYBITS);
}
