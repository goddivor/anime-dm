#include "ui/AddSelection.h"

#include <algorithm>

namespace selection {

// Reads "1-20,25" into the numbers it names.
std::vector<int> Parse(const std::wstring& text, int count) {
    std::vector<int> numbers;
    size_t at = 0;

    while (at < text.size()) {
        while (at < text.size() && (text[at] == L' ' || text[at] == L',')) {
            ++at;
        }
        if (at >= text.size()) {
            break;
        }

        int first = 0;
        bool read = false;
        while (at < text.size() && text[at] >= L'0' && text[at] <= L'9') {
            first = first * 10 + (text[at] - L'0');
            ++at;
            read = true;
        }
        if (!read) {
            ++at;
            continue;
        }

        int last = first;
        if (at < text.size() && text[at] == L'-') {
            ++at;
            int second = 0;
            bool tail = false;
            while (at < text.size() && text[at] >= L'0' && text[at] <= L'9') {
                second = second * 10 + (text[at] - L'0');
                ++at;
                tail = true;
            }
            if (tail) {
                last = second;
            }
        }

        if (first > last) {
            std::swap(first, last);
        }
        for (int value = first; value <= last; ++value) {
            if (value >= 1 && value <= count) {
                numbers.push_back(value);
            }
        }
    }

    std::sort(numbers.begin(), numbers.end());
    numbers.erase(std::unique(numbers.begin(), numbers.end()), numbers.end());
    return numbers;
}

// Writes a sorted list of numbers back as the shortest ranges that cover it.
std::wstring Collapse(std::vector<int> numbers) {
    std::sort(numbers.begin(), numbers.end());
    numbers.erase(std::unique(numbers.begin(), numbers.end()), numbers.end());
    if (numbers.empty()) {
        return std::wstring();
    }

    std::wstring text;
    size_t at = 0;
    while (at < numbers.size()) {
        size_t end = at;
        while (end + 1 < numbers.size() && numbers[end + 1] == numbers[end] + 1) {
            ++end;
        }
        if (!text.empty()) {
            text += L',';
        }
        text += std::to_wstring(numbers[at]);
        if (end > at) {
            text += L'-';
            text += std::to_wstring(numbers[end]);
        }
        at = end + 1;
    }
    return text;
}

}  // namespace selection
