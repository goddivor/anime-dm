#pragma once

#include <cstdint>
#include <string>
#include <vector>

#include <windows.h>

// An animated picture whose frames lie side by side in one 24-bit BMP, one
// colour standing for transparency, the way the sprite-animator tool writes
// them. A JSON descriptor of the same name, when present, gives the frame
// size, the frame count, the frame duration and the colour key; without it
// the frames are taken square and the corner pixel is the key.
class Sprite {
public:
    bool Load(const std::wstring& bmpPath);
    bool Loaded() const { return !frames_.empty(); }
    int FrameCount() const { return static_cast<int>(frames_.size()); }
    int DurationMs() const { return durationMs_; }

    // Renders one frame fitted into a cell, centred, the colour key made
    // clear. The caller owns the bitmap.
    HBITMAP Render(int frame, int width, int height) const;

private:
    std::vector<std::vector<uint32_t>> frames_;  // straight ARGB, row after row
    int frameWidth_ = 0;
    int frameHeight_ = 0;
    int durationMs_ = 120;
};

// The path of a sprite shipped in `resources\sprites`, next to the executable
// or one level up for a development build; empty when there is none.
std::wstring FindSprite(const wchar_t* fileName);
