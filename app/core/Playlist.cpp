#include "core/Playlist.h"

#include <algorithm>
#include <sstream>

namespace {

// Strips the whitespace and the line ending around a line.
std::string Trim(const std::string& line) {
    size_t start = line.find_first_not_of(" \t\r\n");
    if (start == std::string::npos) {
        return std::string();
    }
    size_t end = line.find_last_not_of(" \t\r\n");
    return line.substr(start, end - start + 1);
}

// Reads one attribute of a tag such as `KEY=VALUE,OTHER="quoted"`.
std::string Attribute(const std::string& attributes, const std::string& name) {
    size_t at = 0;
    while ((at = attributes.find(name + "=", at)) != std::string::npos) {
        bool startsToken = at == 0 || attributes[at - 1] == ',';
        at += name.size() + 1;
        if (!startsToken) {
            continue;
        }
        if (at < attributes.size() && attributes[at] == '"') {
            size_t close = attributes.find('"', at + 1);
            return attributes.substr(at + 1, close == std::string::npos ? std::string::npos
                                                                         : close - at - 1);
        }
        size_t comma = attributes.find(',', at);
        return attributes.substr(at, comma == std::string::npos ? std::string::npos : comma - at);
    }
    return std::string();
}

// The text after `#TAG:`.
std::string ValueOf(const std::string& line) {
    size_t colon = line.find(':');
    return colon == std::string::npos ? std::string() : line.substr(colon + 1);
}

// Splits the text into trimmed lines, dropping the empty ones.
std::vector<std::string> Lines(const std::string& text) {
    std::vector<std::string> lines;
    std::istringstream stream(text);
    std::string line;
    while (std::getline(stream, line)) {
        std::string trimmed = Trim(line);
        if (!trimmed.empty()) {
            lines.push_back(std::move(trimmed));
        }
    }
    return lines;
}

}  // namespace

namespace playlist {

// Whether the text is a playlist at all.
bool IsPlaylist(const std::string& text) {
    return Trim(text.substr(0, 16)).rfind("#EXTM3U", 0) == 0;
}

// Whether the playlist lists renditions rather than segments.
bool IsMaster(const std::string& text) {
    return text.find("#EXT-X-STREAM-INF") != std::string::npos;
}

// Resolves a reference against the URL of the playlist that carries it.
std::string Resolve(const std::string& baseUrl, const std::string& reference) {
    if (reference.rfind("http://", 0) == 0 || reference.rfind("https://", 0) == 0) {
        return reference;
    }
    size_t scheme = baseUrl.find("://");
    if (scheme == std::string::npos) {
        return reference;
    }
    if (reference.rfind("//", 0) == 0) {
        return baseUrl.substr(0, scheme + 1) + reference;
    }
    size_t hostEnd = baseUrl.find('/', scheme + 3);
    if (reference.rfind("/", 0) == 0) {
        return baseUrl.substr(0, hostEnd == std::string::npos ? baseUrl.size() : hostEnd) +
               reference;
    }
    size_t query = baseUrl.find('?');
    std::string path = baseUrl.substr(0, query == std::string::npos ? baseUrl.size() : query);
    size_t lastSlash = path.find_last_of('/');
    if (lastSlash == std::string::npos || lastSlash < scheme + 3) {
        return baseUrl + "/" + reference;
    }
    return path.substr(0, lastSlash + 1) + reference;
}

// The renditions of a master playlist, best bandwidth first.
std::vector<Variant> ParseMaster(const std::string& text, const std::string& baseUrl) {
    std::vector<Variant> variants;
    std::vector<std::string> lines = Lines(text);
    for (size_t i = 0; i + 1 < lines.size(); ++i) {
        if (lines[i].rfind("#EXT-X-STREAM-INF", 0) != 0) {
            continue;
        }
        Variant variant;
        variant.bandwidth = std::strtoull(Attribute(ValueOf(lines[i]), "BANDWIDTH").c_str(),
                                          nullptr, 10);
        size_t next = i + 1;
        while (next < lines.size() && lines[next][0] == '#') {
            ++next;
        }
        if (next >= lines.size()) {
            break;
        }
        variant.url = Resolve(baseUrl, lines[next]);
        variants.push_back(std::move(variant));
        i = next;
    }
    std::stable_sort(variants.begin(), variants.end(),
                     [](const Variant& a, const Variant& b) { return a.bandwidth > b.bandwidth; });
    return variants;
}

// The segments of a media playlist, or nothing when it lists none.
std::optional<Media> ParseMedia(const std::string& text, const std::string& baseUrl) {
    Media media;
    std::string keyUrl;
    std::string iv;
    uint64_t sequence = 0;
    bool expectSegment = false;

    for (const std::string& line : Lines(text)) {
        if (line.rfind("#EXT-X-MEDIA-SEQUENCE", 0) == 0) {
            sequence = std::strtoull(ValueOf(line).c_str(), nullptr, 10);
        } else if (line.rfind("#EXT-X-KEY", 0) == 0) {
            std::string attributes = ValueOf(line);
            std::string method = Attribute(attributes, "METHOD");
            if (method == "AES-128") {
                keyUrl = Resolve(baseUrl, Attribute(attributes, "URI"));
                iv = Attribute(attributes, "IV");
                if (iv.rfind("0x", 0) == 0 || iv.rfind("0X", 0) == 0) {
                    iv = iv.substr(2);
                }
            } else {
                keyUrl.clear();
                iv.clear();
            }
        } else if (line.rfind("#EXT-X-MAP", 0) == 0) {
            media.initUrl = Resolve(baseUrl, Attribute(ValueOf(line), "URI"));
        } else if (line.rfind("#EXTINF", 0) == 0) {
            expectSegment = true;
        } else if (line[0] != '#' && expectSegment) {
            Segment segment;
            segment.url = Resolve(baseUrl, line);
            segment.keyUrl = keyUrl;
            segment.iv = iv;
            segment.sequence = sequence++;
            media.segments.push_back(std::move(segment));
            expectSegment = false;
        }
    }

    if (media.segments.empty()) {
        return std::nullopt;
    }
    return media;
}

}  // namespace playlist
