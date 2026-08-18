#pragma once

#include <string>

#include <windows.h>

// Owns the report-mode ListView that displays the download queue.
class DownloadsView {
public:
    bool Create(HWND parent, HINSTANCE instance);
    void SetBounds(int x, int y, int width, int height);
    void Retranslate();
    void AddRow(const std::wstring& fileName, const std::wstring& status);
    HWND Handle() const { return hwnd_; }

private:
    void AddColumns();

    HWND hwnd_ = nullptr;
};
