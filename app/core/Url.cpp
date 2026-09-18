#include "core/Url.h"

#include <algorithm>
#include <cctype>

namespace url {

std::string OriginOf(const std::string& url) {
    size_t scheme = url.find("://");
    if (scheme == std::string::npos) {
        return std::string();
    }
    size_t slash = url.find('/', scheme + 3);
    return slash == std::string::npos ? url : url.substr(0, slash);
}

std::string HostOf(const std::string& url) {
    size_t start = url.find("://");
    start = start == std::string::npos ? 0 : start + 3;
    size_t end = url.find_first_of("/?#", start);
    std::string host = url.substr(start, end == std::string::npos ? std::string::npos : end - start);
    std::transform(host.begin(), host.end(), host.begin(),
                   [](unsigned char letter) { return static_cast<char>(tolower(letter)); });
    if (host.rfind("www.", 0) == 0) {
        host = host.substr(4);
    }
    return host;
}

std::string PathOf(const std::string& url) {
    size_t start = url.find("://");
    start = start == std::string::npos ? 0 : start + 3;
    size_t slash = url.find('/', start);
    if (slash == std::string::npos) {
        return "/";
    }
    size_t hash = url.find('#', slash);
    return url.substr(slash, hash == std::string::npos ? std::string::npos : hash - slash);
}

bool SameSite(const std::string& one, const std::string& other) {
    if (one.empty() || other.empty()) {
        return false;
    }
    if (one == other) {
        return true;
    }
    const std::string& longer = one.size() > other.size() ? one : other;
    const std::string& shorter = one.size() > other.size() ? other : one;
    return longer.compare(longer.size() - shorter.size() - 1, shorter.size() + 1,
                          "." + shorter) == 0;
}

// The page without scheme, fragment or trailing slash, lowercased on its host.
static std::string Canonical(const std::string& url) {
    std::string page = HostOf(url) + PathOf(url);
    while (!page.empty() && page.back() == '/') {
        page.pop_back();
    }
    return page;
}

bool SamePage(const std::string& one, const std::string& other) {
    return Canonical(one) == Canonical(other);
}

}  // namespace url
