#pragma once

#include <string>
#include <vector>

#include "core/Download.h"

// The shapes the queue can be written to: the file of the application,
// which keeps everything and comes back whole; a list of pages, one per
// line; a JSON list of pages for other tools; and a sheet, as CSV, as an
// Excel workbook or as an OpenDocument one.
namespace exporting {

enum class Format { Adm, Text, Json, Csv, Xlsx, Ods };

// The extension a format is saved under.
const wchar_t* Extension(Format format);

// Renders the items in a format: UTF-8 text, or the bytes of a workbook. `groups` supplies the posters
// and titles of the animes the items belong to.
std::string Render(Format format, const std::vector<DownloadItem>& items,
                   const std::vector<AnimeGroup>& groups);

}  // namespace exporting
