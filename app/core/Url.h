#pragma once

#include <string>

// Small readings of an address, shared by the add window, the browser
// bridge and the imports.
namespace url {

// The origin of a page (`https://host`), which image hosts ask for as a referer.
std::string OriginOf(const std::string& url);

// The host of an address, lowercased and without its `www.`.
std::string HostOf(const std::string& url);

// The path and query of an address, `/` when it has none.
std::string PathOf(const std::string& url);

// Whether two hosts belong to the same site, a subdomain counting as one.
bool SameSite(const std::string& one, const std::string& other);

// Whether two addresses name the same page, a trailing slash, a fragment
// and the scheme apart.
bool SamePage(const std::string& one, const std::string& other);

}  // namespace url
