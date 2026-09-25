#include "core/Update.h"

#include <windows.h>

#include <cctype>
#include <filesystem>
#include <fstream>
#include <vector>

#include "core/Http.h"
#include "core/Text.h"
#include "third_party/json.hpp"

namespace {

constexpr char kReleases[] = "https://api.github.com/repos/goddivor/anime-dm/releases?per_page=30";
constexpr char kTagPrefix[] = "win-v";

// The text of a release made plain: the marks of Markdown dropped, the
// blank lines and the links GitHub adds left out.
std::string PlainNotes(const std::string& body) {
    std::string plain;
    size_t from = 0;
    while (from < body.size()) {
        size_t end = body.find('\n', from);
        std::string line = body.substr(from, end == std::string::npos ? std::string::npos : end - from);
        from = end == std::string::npos ? body.size() : end + 1;
        while (!line.empty() && (line.back() == '\r' || line.back() == ' ')) {
            line.pop_back();
        }
        size_t start = line.find_first_not_of("#*- ");
        if (start == std::string::npos || line.find("**Full Changelog**") != std::string::npos) {
            continue;
        }
        line = line.substr(start);
        size_t by = line.find(" by @");
        if (by != std::string::npos) {
            line = line.substr(0, by);
        }
        plain += (plain.empty() ? "" : "\n") + std::string("\xE2\x80\xA2 ") + line;
    }
    return plain;
}

}  // namespace

namespace update {

int Rank(const std::string& version) {
    size_t dot = version.find('.');
    if (dot == std::string::npos || dot == 0 || version.size() - dot - 1 != 2) {
        return -1;
    }
    for (size_t index = 0; index < version.size(); ++index) {
        if (index != dot && !std::isdigit(static_cast<unsigned char>(version[index]))) {
            return -1;
        }
    }
    return std::stoi(version.substr(0, dot)) * 100 + std::stoi(version.substr(dot + 1));
}

std::optional<UpdateInfo> Latest(Http& http) {
    std::optional<std::string> text =
        http.GetText(kReleases, {{"Accept", "application/vnd.github+json"}});
    if (!text) {
        return std::nullopt;
    }
    nlohmann::json releases = nlohmann::json::parse(*text, nullptr, false);
    if (!releases.is_array()) {
        return std::nullopt;
    }
    UpdateInfo best;
    for (const nlohmann::json& release : releases) {
        std::string tag = release.value("tag_name", std::string());
        if (release.value("draft", false) || release.value("prerelease", false) ||
            tag.rfind(kTagPrefix, 0) != 0) {
            continue;
        }
        std::string version = tag.substr(sizeof(kTagPrefix) - 1);
        if (Rank(version) <= Rank(best.version)) {
            continue;
        }
        for (const nlohmann::json& asset : release.value("assets", nlohmann::json::array())) {
            std::string name = asset.value("name", std::string());
            if (name.size() > 10 && name.compare(name.size() - 10, 10, "-setup.exe") == 0) {
                best.version = version;
                best.installerUrl = asset.value("browser_download_url", std::string());
                best.notes = PlainNotes(release.value("body", std::string()));
                break;
            }
        }
    }
    return best;
}

std::wstring Download(Http& http, const UpdateInfo& info) {
    std::optional<std::vector<uint8_t>> bytes = http.GetBytes(info.installerUrl);
    if (!bytes || bytes->size() < 1024) {
        return std::wstring();
    }
    wchar_t folder[MAX_PATH] = {};
    GetTempPathW(MAX_PATH, folder);
    std::wstring path =
        std::wstring(folder) + L"AnimeDownloadManager-" + Widen(info.version) + L"-setup.exe";
    std::ofstream file(std::filesystem::path(path), std::ios::binary | std::ios::trunc);
    if (!file.write(reinterpret_cast<const char*>(bytes->data()),
                    static_cast<std::streamsize>(bytes->size()))) {
        return std::wstring();
    }
    return path;
}

}  // namespace update
