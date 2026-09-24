#pragma once

#include <cstdint>
#include <optional>
#include <string>
#include <vector>

#include <windows.h>

// An animated picture whose frames lie side by side in one strip: a 24-bit
// BMP with one colour standing for transparency, the way the sprite-animator
// tool writes them, or a PNG, whose own transparency is kept as it is (a
// halo needs more than a colour key). A JSON descriptor of the same name,
// when present, gives the frame
// size, the frame count, the frame durations (one for all, or one per frame)
// and the colour key; without it the frames are taken square and the corner
// pixel is the key.
class Sprite {
public:
    bool Load(const std::wstring& stripPath);
    bool Loaded() const { return !frames_.empty(); }
    int FrameCount() const { return static_cast<int>(frames_.size()); }
    // How long a frame stays on screen before the next one.
    int DurationOf(int frame) const;

    // Renders one frame fitted into a cell, centred, the colour key made
    // clear; `greyTo` greys it out, halfway toward that theme colour so it
    // stays readable on a dark toolbar as on a light one. The caller owns the
    // bitmap.
    HBITMAP Render(int frame, int width, int height,
                   std::optional<COLORREF> greyTo = std::nullopt) const;

private:
    std::vector<std::vector<uint32_t>> frames_;  // straight ARGB, row after row
    int frameWidth_ = 0;
    int frameHeight_ = 0;
    int durationMs_ = 120;
    std::vector<int> durations_;
};

