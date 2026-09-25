#pragma once

#include <string>
#include <vector>

#include "core/Download.h"

class AddonStore;
class Http;

// What a file to import yields once read: pages to fetch, some already
// known down to the source and the anime, some bare addresses.
struct ImportEntry {
    std::string url;         // the page of an episode, or of an anime
    std::string addonId;     // when the file named the source
    std::string animeUrl;    // when the file named the anime
    std::string animeTitle;
    std::string posterUrl;
    double number = -1.0;    // the episode number, when the file gave it
    bool movie = false;
    std::string player;
    QueueKind queue = QueueKind::Main;
};

struct ImportedEpisode {
    std::string url;
    std::string name;
    double number = 0.0;
    std::string player;
    QueueKind queue = QueueKind::Main;
};

// One anime ready to be queued, with the episodes the file asked for.
struct ImportedAnime {
    std::string addonId;
    std::string animeUrl;
    std::string title;
    std::string posterUrl;
    std::vector<ImportedEpisode> episodes;
};

struct ImportResult {
    std::vector<ImportedAnime> animes;
    int unknown = 0;  // addresses no installed source serves
    int failed = 0;   // pages a source could not read
};

// One anime a list of addresses asks for: its page, its source, and the
// episode pages named, none meaning every episode.
struct AddressGroup {
    std::string addonId;
    std::string animeUrl;
    std::vector<std::string> episodes;
};

struct Grouping {
    std::vector<AddressGroup> groups;
    int unknown = 0;  // addresses no installed source serves
};

namespace importing {

// Reads a file, whichever of its shapes it has (the file of the application,
// text, JSON, CSV, Excel or OpenDocument workbook); empty when unreadable.
std::vector<ImportEntry> Parse(const std::string& text);

// Turns the entries into animes and episodes, asking the sources for what
// the file did not say. Loads libraries and reads pages: off the interface
// thread.
ImportResult Resolve(const std::vector<ImportEntry>& entries, const AddonStore& store,
                     Http& http);

// Gathers addresses by anime and source, loading the sources but reading no
// page: off the interface thread.
Grouping Group(const std::vector<std::string>& addresses, const AddonStore& store, Http& http);

}  // namespace importing
