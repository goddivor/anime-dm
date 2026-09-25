#include "core/Export.h"

#include <cstdio>
#include <map>

#include "core/Sheet.h"
#include "core/Text.h"
#include "third_party/json.hpp"

namespace {

constexpr int kAdmVersion = 1;

// A number as the sheet and the JSON list write it: "12", or "12.5".
std::string NumberText(double number) {
    char text[32] = {};
    if (number == static_cast<double>(static_cast<long>(number))) {
        snprintf(text, sizeof(text), "%ld", static_cast<long>(number));
    } else {
        snprintf(text, sizeof(text), "%.1f", number);
    }
    return text;
}

const AnimeGroup* GroupOf(const std::vector<AnimeGroup>& groups, const std::string& url) {
    for (const AnimeGroup& group : groups) {
        if (group.url == url) {
            return &group;
        }
    }
    return nullptr;
}

// The items gathered by anime, in the order the animes first appear.
std::vector<std::pair<std::string, std::vector<const DownloadItem*>>> ByAnime(
    const std::vector<DownloadItem>& items) {
    std::vector<std::pair<std::string, std::vector<const DownloadItem*>>> animes;
    for (const DownloadItem& item : items) {
        auto found = std::find_if(animes.begin(), animes.end(),
                                  [&](const auto& pair) { return pair.first == item.animeUrl; });
        if (found == animes.end()) {
            animes.push_back({item.animeUrl, {&item}});
        } else {
            found->second.push_back(&item);
        }
    }
    return animes;
}

// The file of the application: animes, then their episodes with all that
// the queue knows of them.
std::string RenderAdm(const std::vector<DownloadItem>& items,
                      const std::vector<AnimeGroup>& groups) {
    nlohmann::json animes = nlohmann::json::array();
    for (const auto& [url, episodes] : ByAnime(items)) {
        const AnimeGroup* group = GroupOf(groups, url);
        nlohmann::json list = nlohmann::json::array();
        for (const DownloadItem* item : episodes) {
            list.push_back({
                {"pageUrl", item->pageUrl},
                {"number", item->episodeNumber},
                {"movie", item->movie},
                {"player", item->player},
                {"fileName", Narrow(item->outPath.substr(item->outPath.find_last_of(L"\\/") + 1))},
                {"queue", item->queue == QueueKind::Scheduler ? "scheduler" : "main"},
            });
        }
        animes.push_back({
            {"url", url},
            {"title", episodes.front()->animeTitle},
            {"posterUrl", group != nullptr ? group->posterUrl : std::string()},
            {"addonId", episodes.front()->addonId},
            {"episodes", list},
        });
    }
    nlohmann::json root = {
        {"format", "anime-dm"},
        {"version", kAdmVersion},
        {"animes", animes},
    };
    return root.dump(2);
}

// One page per line: the episodes, each on its own line.
std::string RenderText(const std::vector<DownloadItem>& items) {
    std::string text;
    for (const DownloadItem& item : items) {
        text += item.pageUrl + "\r\n";
    }
    return text;
}

// A flat list of pages for other tools.
std::string RenderJson(const std::vector<DownloadItem>& items) {
    nlohmann::json list = nlohmann::json::array();
    for (const DownloadItem& item : items) {
        list.push_back({
            {"anime", item.animeTitle},
            {"animeUrl", item.animeUrl},
            {"episode", item.movie ? nlohmann::json() : nlohmann::json(item.episodeNumber)},
            {"url", item.pageUrl},
        });
    }
    return list.dump(2);
}

// One CSV field, quoted when it holds a separator, a quote or a line break.
std::string Field(const std::string& value) {
    if (value.find_first_of(";\"\r\n") == std::string::npos) {
        return value;
    }
    std::string quoted = "\"";
    for (char letter : value) {
        quoted += letter == '"' ? "\"\"" : std::string(1, letter);
    }
    return quoted + "\"";
}

// The rows every sheet holds: the headings, then one line per item.
sheet::Rows SheetRows(const std::vector<DownloadItem>& items) {
    sheet::Rows rows = {{{"Anime"}, {"Episode"}, {"Page de l'anime"}, {"Page de l'episode"},
                         {"Fichier"}}};
    for (const DownloadItem& item : items) {
        std::string file = Narrow(item.outPath.substr(item.outPath.find_last_of(L"\\/") + 1));
        sheet::Cell number = {item.movie ? std::string() : NumberText(item.episodeNumber),
                              !item.movie};
        rows.push_back({{item.animeTitle}, number, {item.animeUrl}, {item.pageUrl}, {file}});
    }
    return rows;
}

// A sheet with a semicolon between the columns, as the French Excel reads it.
std::string RenderCsv(const std::vector<DownloadItem>& items) {
    std::string text = "\xEF\xBB\xBF";
    for (const std::vector<sheet::Cell>& row : SheetRows(items)) {
        for (size_t column = 0; column < row.size(); ++column) {
            text += (column > 0 ? ";" : "") + Field(row[column].text);
        }
        text += "\r\n";
    }
    return text;
}

}  // namespace

namespace exporting {

const wchar_t* Extension(Format format) {
    switch (format) {
    case Format::Adm:
        return L"adm";
    case Format::Text:
        return L"txt";
    case Format::Json:
        return L"json";
    case Format::Xlsx:
        return L"xlsx";
    case Format::Ods:
        return L"ods";
    default:
        return L"csv";
    }
}

std::string Render(Format format, const std::vector<DownloadItem>& items,
                   const std::vector<AnimeGroup>& groups) {
    switch (format) {
    case Format::Adm:
        return RenderAdm(items, groups);
    case Format::Text:
        return RenderText(items);
    case Format::Json:
        return RenderJson(items);
    case Format::Xlsx:
        return sheet::WriteXlsx(SheetRows(items));
    case Format::Ods:
        return sheet::WriteOds(SheetRows(items));
    default:
        return RenderCsv(items);
    }
}

}  // namespace exporting
