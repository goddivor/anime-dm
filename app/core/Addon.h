#pragma once

#include <map>
#include <memory>
#include <optional>
#include <string>
#include <vector>

#include "third_party/json.hpp"

class Http;

// What a source declares about itself.
struct AddonMetadata {
    std::string id;
    std::string name;
    std::string lang;
    std::string baseUrl;
    std::string version;
    bool nsfw = false;
};

// One setting a source declares, rendered as a field by the settings screen.
struct AddonPreference {
    std::string key;
    std::string title;
    std::string summary;
    std::string defaultValue;
    std::string kind;  // "text", "select" or "bool"
    std::vector<std::string> options;
};

// A source addon loaded from its native library.
//
// The Http reference is borrowed: it serves the requests the addon asks for and
// must outlive it.
class Addon {
public:
    ~Addon();

    Addon(const Addon&) = delete;
    Addon& operator=(const Addon&) = delete;
    Addon(Addon&& other) noexcept;
    Addon& operator=(Addon&& other) noexcept;

    static std::unique_ptr<Addon> Load(const std::wstring& libraryPath, Http& http,
                                       const std::map<std::string, std::string>& config);

    const AddonMetadata& Meta() const { return meta_; }
    std::vector<AddonPreference> Preferences() const;

    // Calls an entry point that takes a JSON argument and unwraps the envelope.
    // Returns nothing and fills `error` when the addon reports a failure.
    std::optional<nlohmann::json> Call(const char* entry, const nlohmann::json& input,
                                       std::string* error = nullptr) const;

    // Reason the last load failed, when Load returned nothing.
    static const std::string& LastError();

private:
    Addon() = default;

    std::optional<nlohmann::json> CallRaw(void* entry, const std::string* input,
                                          std::string* error) const;
    void Release();

    void* module_ = nullptr;
    void* free_ = nullptr;
    std::map<std::string, void*> entries_;
    AddonMetadata meta_;
    Http* http_ = nullptr;
};
