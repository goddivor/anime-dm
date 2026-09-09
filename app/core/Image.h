#pragma once

#include <cstdint>
#include <vector>

#include <windows.h>

// The store publishes an icon per source; they arrive as encoded bytes and have
// to become bitmaps the common controls can show.
namespace image {

// Decodes an encoded image and scales it to a square bitmap with an alpha
// channel. Returns nullptr when the bytes cannot be read.
HBITMAP DecodeSquare(const std::vector<uint8_t>& bytes, int size);

// Same, at a free width and height, for artwork that is not square.
HBITMAP Decode(const std::vector<uint8_t>& bytes, int width, int height);

// Same, but keeping the proportions of the source inside the box given.
HBITMAP Fit(const std::vector<uint8_t>& bytes, int width, int height);

// Fills a box with the source, cropping what overflows, and rounds the
// corners by `radius` pixels: what a poster thumbnail looks like.
HBITMAP Cover(const std::vector<uint8_t>& bytes, int width, int height, int radius);

// A fully transparent square, to keep a row without icon in step with the
// others in an image list.
HBITMAP Transparent(int size);

}  // namespace image
