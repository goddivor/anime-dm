#pragma once

#include <cstdint>
#include <functional>
#include <optional>
#include <string>
#include <vector>

#include <windows.h>

#include "core/Download.h"

// Owns the report-mode ListView that displays the download queue. Every row
// carries the id of its item, so the window finds the item behind a row and
// the row behind an event.
class DownloadsView {
public:
    bool Create(HWND parent, HINSTANCE instance);
    // Queues the move of the list into a deferred batch.
    HDWP Place(HDWP batch, int x, int y, int width, int height);
    void Retranslate();

    // Inserts the row of an item, or refreshes it when it is already there.
    void Upsert(const DownloadItem& item);
    void Remove(uint64_t id);
    void Clear();

    // Marks the column the rows are sorted by, or none, in the header.
    void SetSortMark(int column, bool ascending);
    // Reorders the rows; `before` says whether the first id ranks first.
    void Sort(const std::function<bool(uint64_t, uint64_t)>& before);

    std::vector<uint64_t> Selected() const;
    int RowOf(uint64_t id) const;
    uint64_t IdAt(int row) const;
    int Count() const;
    // The row under the pointer, lit up by the custom draw, or -1.
    int HotRow() const;

    HWND Handle() const { return hwnd_; }

    // The widths the columns take, kept between two sessions.
    void SetWidths(const std::vector<int>& widths);
    std::vector<int> Widths() const;
    // Called once a column has been resized by hand.
    void OnColumnsResized(std::function<void()> callback) { onResized_ = std::move(callback); }
    void ColumnsResized();

private:
    void AddColumns();
    void Fill(int row, const DownloadItem& item);

    HWND hwnd_ = nullptr;
    std::function<void()> onResized_;
};

// The text of the status cell, shared with the custom draw of the window.
std::wstring StatusText(const DownloadItem& item);
