#pragma once

#include <cstdint>
#include <functional>
#include <map>
#include <optional>
#include <string>
#include <vector>

// What a server says about a file before it is downloaded.
struct HttpProbe {
    int status = 0;
    uint64_t length = 0;  // 0 when the server does not say
    bool ranges = false;  // whether byte ranges are honoured
};

// Receives the body of a streamed request piece by piece; returning false
// aborts the transfer.
using HttpSink = std::function<bool(const uint8_t* data, size_t size)>;

// Sees the status code before the body is read; returning false skips it.
using HttpAccept = std::function<bool(int status)>;

// The single HTTP client of the application. Addons never open a socket
// themselves: their requests go through here and inherit its user agent, its
// timeouts and its redirect policy.
class Http {
public:
    Http();
    ~Http();

    Http(const Http&) = delete;
    Http& operator=(const Http&) = delete;

    // Fetches a URL as text, or nothing when the request fails or answers an
    // error status.
    std::optional<std::string> GetText(const std::string& url,
                                       const std::map<std::string, std::string>& headers = {});

    // Fetches a URL as bytes, for the addon libraries and the icons.
    std::optional<std::vector<uint8_t>> GetBytes(
        const std::string& url, const std::map<std::string, std::string>& headers = {});

    // Asks for the first byte of a URL to learn its size and whether the
    // server honours byte ranges, without downloading it.
    std::optional<HttpProbe> Probe(const std::string& url,
                                   const std::map<std::string, std::string>& headers = {});

    // Streams a byte range of a URL into a sink. `to` is inclusive; a `to` of
    // zero with a `from` of zero asks for the whole body. Returns the status
    // code, or zero when the request could not be made.
    int Stream(const std::string& url, const std::map<std::string, std::string>& headers,
               uint64_t from, uint64_t to, const HttpAccept& accept, const HttpSink& sink);

    bool Ready() const { return session_ != nullptr; }

private:
    std::optional<std::vector<uint8_t>> Fetch(const std::string& url,
                                              const std::map<std::string, std::string>& headers);

    void* session_ = nullptr;
};
