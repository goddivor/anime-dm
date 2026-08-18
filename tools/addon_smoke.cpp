// Loads a source library and prints what it declares, so the C ABI can be
// checked without launching the whole application:
//
//   addon-smoke <path to addon.dll> [anime URL]
//
// With a URL it also walks the reading chain: details, episodes, hosters and
// the videos of the first hoster.

#include <cstdio>
#include <map>
#include <string>

#include "core/Addon.h"
#include "core/Http.h"
#include "core/Text.h"

namespace {

void PrintJson(const char* label, const nlohmann::json& value) {
    std::printf("%s: %s\n", label, value.dump(2).c_str());
}

int Walk(const Addon& addon, const std::string& url) {
    std::string error;

    std::optional<nlohmann::json> details =
        addon.Call("adm_anime_details", {{"url", url}}, &error);
    if (!details) {
        std::printf("anime_details failed: %s\n", error.c_str());
        return 1;
    }
    std::printf("title: %s\n", details->value("title", "?").c_str());

    std::optional<nlohmann::json> episodes =
        addon.Call("adm_episode_list", {{"url", url}}, &error);
    if (!episodes || !episodes->is_array() || episodes->empty()) {
        std::printf("episode_list failed: %s\n", error.c_str());
        return 1;
    }
    std::printf("episodes: %zu\n", episodes->size());

    const nlohmann::json& first = episodes->front();
    std::optional<nlohmann::json> hosters =
        addon.Call("adm_hoster_list", {{"url", first.value("url", "")}}, &error);
    if (!hosters || !hosters->is_array()) {
        std::printf("hoster_list failed: %s\n", error.c_str());
        return 1;
    }
    std::printf("hosters: %zu\n", hosters->size());

    int resolved = 0;
    for (const nlohmann::json& hoster : *hosters) {
        std::optional<nlohmann::json> videos = addon.Call("adm_video_list", hoster, &error);
        if (videos && videos->is_array() && !videos->empty()) {
            std::printf("  %s -> %s\n", hoster.value("name", "?").c_str(),
                        videos->front().value("url", "?").c_str());
            ++resolved;
        }
    }
    std::printf("resolved: %d\n", resolved);
    return resolved > 0 ? 0 : 1;
}

}  // namespace

int main(int argc, char** argv) {
    if (argc < 2) {
        std::printf("usage: addon-smoke <addon.dll> [anime URL]\n");
        return 2;
    }

    Http http;
    if (!http.Ready()) {
        std::printf("the HTTP client could not start\n");
        return 1;
    }

    std::map<std::string, std::string> config;
    std::unique_ptr<Addon> addon = Addon::Load(Widen(argv[1]), http, config);
    if (!addon) {
        std::printf("load failed: %s\n", Addon::LastError().c_str());
        return 1;
    }

    const AddonMetadata& meta = addon->Meta();
    std::printf("id: %s\nname: %s\nlang: %s\nversion: %s\nbaseUrl: %s\nnsfw: %s\n", meta.id.c_str(),
                meta.name.c_str(), meta.lang.c_str(), meta.version.c_str(), meta.baseUrl.c_str(),
                meta.nsfw ? "yes" : "no");

    nlohmann::json preferences = nlohmann::json::array();
    for (const AddonPreference& preference : addon->Preferences()) {
        preferences.push_back({{"key", preference.key},
                               {"title", preference.title},
                               {"type", preference.kind},
                               {"default", preference.defaultValue},
                               {"options", preference.options}});
    }
    PrintJson("preferences", preferences);

    return argc > 2 ? Walk(*addon, argv[2]) : 0;
}
