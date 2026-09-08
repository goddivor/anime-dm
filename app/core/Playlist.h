#pragma once

#include <cstdint>
#include <optional>
#include <string>
#include <vector>

// The subset of HLS (RFC 8216) the streaming hosts use: master playlists
// listing variants, media playlists listing segments, AES-128 keys and an
// optional initialisation segment for fragmented MP4.
namespace playlist {

// One rendition of a master playlist.
struct Variant {
    std::string url;
    uint64_t bandwidth = 0;
};

// One piece of a media playlist, with the key that protects it, if any.
struct Segment {
    std::string url;
    std::string keyUrl;  // empty when the segment is in the clear
    std::string iv;      // hex without prefix; empty means the sequence number
    uint64_t sequence = 0;
};

// A media playlist ready to be fetched.
struct Media {
    std::string initUrl;  // EXT-X-MAP, present for fragmented MP4
    std::vector<Segment> segments;
};

// Whether the text is a playlist at all.
bool IsPlaylist(const std::string& text);

// Whether the playlist lists renditions rather than segments.
bool IsMaster(const std::string& text);

// The renditions of a master playlist, best bandwidth first.
std::vector<Variant> ParseMaster(const std::string& text, const std::string& baseUrl);

// The segments of a media playlist, or nothing when it lists none.
std::optional<Media> ParseMedia(const std::string& text, const std::string& baseUrl);

// Resolves a reference against the URL of the playlist that carries it.
std::string Resolve(const std::string& baseUrl, const std::string& reference);

}  // namespace playlist
