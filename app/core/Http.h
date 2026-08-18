#pragma once

#include <cstdint>
#include <map>
#include <optional>
#include <string>
#include <vector>

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

    bool Ready() const { return session_ != nullptr; }

private:
    std::optional<std::vector<uint8_t>> Fetch(const std::string& url,
                                              const std::map<std::string, std::string>& headers);

    void* session_ = nullptr;
};
