#pragma once

#include <cstdint>
#include <ctime>
#include <string>

// Renders the figures of the downloads table in the active language.
namespace format {

// "12,3 MB"; a dash when zero.
std::wstring Size(uint64_t bytes);

// "1,2 MB/s"; a dash when zero.
std::wstring Speed(double bytesPerSecond);

// "3 min 12 s"; a dash when negative.
std::wstring Duration(double seconds);

// "2026-09-08 14:05"; a dash when zero.
std::wstring Date(std::time_t when);


}  // namespace format
