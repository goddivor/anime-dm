#pragma once

#include <string>
#include <vector>

// The zip archives the workbooks of Excel and LibreOffice are made of. The
// writer stores the files as they are, which both read; the reader also
// inflates, since both compress what they save.
namespace zip {

struct Entry {
    std::string name;
    std::string data;
};

// Whether the bytes open like a zip archive.
bool IsZip(const std::string& bytes);

// Packs the entries, in order and without compression.
std::string Write(const std::vector<Entry>& entries);

// Reads every file of an archive, inflated; empty when the bytes are not an
// archive this reader understands.
std::vector<Entry> Read(const std::string& archive);

// The data of one file of an archive, or empty when it is not there.
std::string Find(const std::vector<Entry>& entries, const std::string& name);

}  // namespace zip
