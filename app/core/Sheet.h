#pragma once

#include <string>
#include <vector>

// Workbooks of Excel (.xlsx) and of LibreOffice (.ods), reduced to what the
// export and the import need: one sheet of text and numbers, its first row
// in bold as the headings.
namespace sheet {

struct Cell {
    std::string text;     // UTF-8; for a number, its decimal writing
    bool number = false;
};

using Rows = std::vector<std::vector<Cell>>;

std::string WriteXlsx(const Rows& rows);
std::string WriteOds(const Rows& rows);

// The first sheet of a workbook, either kind, as rows of text, a cell at
// its column; empty when the bytes are not a workbook.
std::vector<std::vector<std::string>> Read(const std::string& bytes);

}  // namespace sheet
