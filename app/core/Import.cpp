#include "core/Import.h"

#include <algorithm>
#include <map>
#include <memory>
#include <optional>
#include <regex>

#include "core/Addon.h"
#include "core/AddonStore.h"
#include "core/Sheet.h"
#include "core/Url.h"
#include "core/Zip.h"
#include "third_party/json.hpp"

namespace {

std::string Trim(const std::string& text) {
    size_t start = text.find_first_not_of(" \t\r\n\xEF\xBB\xBF");
    if (start == std::string::npos) {
        return std::string();
    }
    size_t end = text.find_last_not_of(" \t\r\n");
    return text.substr(start, end - start + 1);
}

bool LooksLikeUrl(const std::string& text) {
    return text.rfind("http://", 0) == 0 || text.rfind("https://", 0) == 0;
}

// The file of the application.
std::vector<ImportEntry> ParseAdm(const nlohmann::json& root) {
    std::vector<ImportEntry> entries;
    auto animes = root.find("animes");
    if (animes == root.end() || !animes->is_array()) {
        return entries;
    }
    for (const nlohmann::json& anime : *animes) {
        if (!anime.is_object()) {
            continue;
        }
        auto episodes = anime.find("episodes");
        if (episodes == anime.end() || !episodes->is_array()) {
            continue;
        }
        for (const nlohmann::json& episode : *episodes) {
            ImportEntry entry;
            entry.url = episode.value("pageUrl", std::string());
            entry.addonId = anime.value("addonId", std::string());
            entry.animeUrl = anime.value("url", std::string());
            entry.animeTitle = anime.value("title", std::string());
            entry.posterUrl = anime.value("posterUrl", std::string());
            entry.number = episode.value("number", -1.0);
            entry.movie = episode.value("movie", false);
            entry.player = episode.value("player", std::string());
            entry.queue = episode.value("queue", std::string()) == "scheduler"
                              ? QueueKind::Scheduler
                              : QueueKind::Main;
            if (!entry.url.empty()) {
                entries.push_back(std::move(entry));
            }
        }
    }
    return entries;
}

// The flat JSON list, or any list of objects with an address in them.
std::vector<ImportEntry> ParseJsonList(const nlohmann::json& list) {
    std::vector<ImportEntry> entries;
    for (const nlohmann::json& item : list) {
        ImportEntry entry;
        if (item.is_string()) {
            entry.url = item.get<std::string>();
        } else if (item.is_object()) {
            for (const char* key : {"url", "pageUrl", "episodeUrl", "animeUrl", "page"}) {
                if (item.contains(key) && item[key].is_string()) {
                    entry.url = item[key].get<std::string>();
                    break;
                }
            }
        }
        if (LooksLikeUrl(entry.url)) {
            entries.push_back(std::move(entry));
        }
    }
    return entries;
}

// A sheet, one entry per row: the episode page comes last among the
// addresses of a row, the anime page, when there is one, sitting before it.
std::vector<ImportEntry> ParseRows(const std::vector<std::vector<std::string>>& rows) {
    std::vector<ImportEntry> entries;
    for (const std::vector<std::string>& fields : rows) {
        std::string page;
        for (const std::string& value : fields) {
            std::string trimmed = Trim(value);
            if (LooksLikeUrl(trimmed)) {
                page = trimmed;
            }
        }
        if (!page.empty()) {
            ImportEntry entry;
            entry.url = page;
            entries.push_back(std::move(entry));
        }
    }
    return entries;
}

// A CSV sheet, split into its rows and fields before being read as a sheet.
std::vector<ImportEntry> ParseCsv(const std::string& text) {
    std::vector<std::vector<std::string>> rows;
    size_t from = 0;
    while (from < text.size()) {
        size_t end = text.find('\n', from);
        std::string line = text.substr(from, end == std::string::npos ? std::string::npos : end - from);
        from = end == std::string::npos ? text.size() : end + 1;
        std::vector<std::string> fields;
        std::string field;
        bool quoted = false;
        for (size_t i = 0; i < line.size(); ++i) {
            char letter = line[i];
            if (quoted) {
                if (letter == '"' && i + 1 < line.size() && line[i + 1] == '"') {
                    field += '"';
                    ++i;
                } else if (letter == '"') {
                    quoted = false;
                } else {
                    field += letter;
                }
            } else if (letter == '"') {
                quoted = true;
            } else if (letter == ';' || letter == ',' || letter == '\t') {
                fields.push_back(field);
                field.clear();
            } else if (letter != '\r') {
                field += letter;
            }
        }
        fields.push_back(field);
        rows.push_back(std::move(fields));
    }
    return ParseRows(rows);
}

// One address per line.
std::vector<ImportEntry> ParseText(const std::string& text) {
    std::vector<ImportEntry> entries;
    size_t from = 0;
    while (from < text.size()) {
        size_t end = text.find('\n', from);
        std::string line = Trim(text.substr(from, end == std::string::npos ? std::string::npos : end - from));
        from = end == std::string::npos ? text.size() : end + 1;
        if (LooksLikeUrl(line)) {
            ImportEntry entry;
            entry.url = line;
            entries.push_back(std::move(entry));
        }
    }
    return entries;
}

// A loaded source with what tells its pages apart.
struct Source {
    std::string id;
    std::string host;
    std::unique_ptr<Addon> addon;
    std::optional<std::regex> episodePattern;
    std::string animeFromEpisode;
};

// The `$1` style of the extension turned into the `$1` of std::regex, which
// happens to agree; the function stands where a difference would go.
std::string Replacement(const std::string& text) {
    return text;
}

// The anime page an episode page leads to, by the pattern of its source, or
// the page itself when it is not an episode.
std::string AnimeOf(const Source& source, const std::string& pageUrl, bool* isEpisode) {
    *isEpisode = false;
    if (!source.episodePattern || source.animeFromEpisode.empty()) {
        return pageUrl;
    }
    std::string path = url::PathOf(pageUrl);
    if (!std::regex_search(path, *source.episodePattern)) {
        return pageUrl;
    }
    *isEpisode = true;
    std::string animePath = std::regex_replace(path, *source.episodePattern,
                                               Replacement(source.animeFromEpisode),
                                               std::regex_constants::format_first_only);
    return url::OriginOf(pageUrl) + animePath;
}

}  // namespace

namespace importing {

// Sniffs the shape of the text and hands it to the right reader.
std::vector<ImportEntry> Parse(const std::string& text) {
    if (zip::IsZip(text)) {
        return ParseRows(sheet::Read(text));
    }
    std::string body = Trim(text);
    if (body.empty()) {
        return {};
    }
    if (body.front() == '{' || body.front() == '[') {
        nlohmann::json root = nlohmann::json::parse(body, nullptr, false);
        if (root.is_object() && root.value("format", std::string()) == "anime-dm") {
            return ParseAdm(root);
        }
        if (root.is_array()) {
            return ParseJsonList(root);
        }
        if (root.is_object() && root.contains("animes")) {
            return ParseAdm(root);
        }
        return {};
    }
    if (body.find(';') != std::string::npos || body.find(',') != std::string::npos) {
        std::vector<ImportEntry> rows = ParseCsv(body);
        if (!rows.empty()) {
            return rows;
        }
    }
    return ParseText(body);
}

// Groups the entries by anime, asking the sources for the pages the file
// did not describe, and drops what no source serves.
ImportResult Resolve(const std::vector<ImportEntry>& entries, const AddonStore& store,
                     Http& http) {
    ImportResult result;
    std::vector<Source> sources;
    for (const InstalledAddon& installed : store.Installed()) {
        Source source;
        source.id = installed.id;
        source.addon = Addon::Load(store.LibraryPath(installed.id), http,
                                   store.ReadConfig(installed.id));
        if (!source.addon) {
            continue;
        }
        const AddonMetadata& meta = source.addon->Meta();
        source.host = url::HostOf(meta.baseUrl);
        if (!meta.episodePattern.empty()) {
            try {
                source.episodePattern = std::regex(meta.episodePattern);
            } catch (const std::regex_error&) {
            }
        }
        source.animeFromEpisode = meta.animeFromEpisode;
        sources.push_back(std::move(source));
    }

    // The animes to build, each with the pages asked of it: every episode
    // when the anime page itself was given, the named ones otherwise.
    struct Pending {
        ImportedAnime anime;
        bool whole = false;
        std::vector<std::string> pages;
        std::vector<ImportEntry> described;
    };
    std::vector<Pending> pending;
    auto find = [&](const std::string& animeUrl) -> Pending& {
        for (Pending& item : pending) {
            if (url::SamePage(item.anime.animeUrl, animeUrl)) {
                return item;
            }
        }
        pending.push_back({});
        pending.back().anime.animeUrl = animeUrl;
        return pending.back();
    };

    for (const ImportEntry& entry : entries) {
        if (!entry.addonId.empty() && !entry.animeUrl.empty() && entry.number >= 0) {
            Pending& item = find(entry.animeUrl);
            item.anime.addonId = entry.addonId;
            item.anime.title = entry.animeTitle;
            item.anime.posterUrl = entry.posterUrl;
            item.described.push_back(entry);
            continue;
        }
        std::string host = url::HostOf(entry.url);
        const Source* source = nullptr;
        for (const Source& candidate : sources) {
            if (url::SameSite(host, candidate.host)) {
                source = &candidate;
                break;
            }
        }
        if (source == nullptr) {
            result.unknown += 1;
            continue;
        }
        bool isEpisode = false;
        std::string animeUrl = AnimeOf(*source, entry.url, &isEpisode);
        Pending& item = find(animeUrl);
        item.anime.addonId = source->id;
        if (isEpisode) {
            item.pages.push_back(entry.url);
        } else {
            item.whole = true;
        }
    }

    for (Pending& item : pending) {
        if (!item.whole && item.pages.empty()) {
            for (const ImportEntry& entry : item.described) {
                ImportedEpisode episode;
                episode.url = entry.url;
                episode.number = entry.number;
                episode.name = entry.movie ? "film" : std::string();
                episode.player = entry.player;
                episode.queue = entry.queue;
                item.anime.episodes.push_back(episode);
            }
            result.animes.push_back(std::move(item.anime));
            continue;
        }
        const Source* source = nullptr;
        for (const Source& candidate : sources) {
            if (candidate.id == item.anime.addonId) {
                source = &candidate;
            }
        }
        if (source == nullptr) {
            result.failed += 1;
            continue;
        }
        nlohmann::json input = {{"url", item.anime.animeUrl}};
        if (std::optional<nlohmann::json> details = source->addon->Call("adm_anime_details", input)) {
            item.anime.title = details->value("title", item.anime.title);
            auto poster = details->find("posterUrl");
            if (poster != details->end() && poster->is_string()) {
                item.anime.posterUrl = poster->get<std::string>();
            }
        }
        std::optional<nlohmann::json> episodes = source->addon->Call("adm_episode_list", input);
        if (!episodes || !episodes->is_array() || episodes->empty()) {
            result.failed += 1;
            continue;
        }
        for (const nlohmann::json& entry : *episodes) {
            ImportedEpisode episode;
            episode.url = entry.value("url", std::string());
            episode.name = entry.value("name", std::string());
            episode.number = entry.value("number", 0.0);
            if (episode.url.empty()) {
                continue;
            }
            bool wanted = item.whole || std::any_of(item.pages.begin(), item.pages.end(),
                                                    [&](const std::string& page) {
                                                        return url::SamePage(page, episode.url);
                                                    });
            if (wanted) {
                item.anime.episodes.push_back(episode);
            }
        }
        if (item.anime.title.empty()) {
            item.anime.title = item.anime.animeUrl;
        }
        result.animes.push_back(std::move(item.anime));
    }
    return result;
}

}  // namespace importing
