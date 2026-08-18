#pragma once

#include <string>
#include <vector>

// The episode selection can be typed as a compact range list. These translate
// between that text and the set of numbers behind it.
namespace selection {

// Reads "1-20,25" into the numbers it names, bounded by the episode count.
std::vector<int> Parse(const std::wstring& text, int count);

// Writes a sorted list of numbers back as the shortest ranges that cover it.
std::wstring Collapse(std::vector<int> numbers);

}  // namespace selection
