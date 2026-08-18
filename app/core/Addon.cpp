#include "core/Addon.h"

#include <windows.h>

#include <cstdlib>
#include <cstring>

#include "core/Http.h"
#include "core/Text.h"
#include "core/adm_addon.h"

namespace {

std::string g_lastError;

// The entry points every source library exports, resolved once at load time.
constexpr const char* kEntries[] = {
    "adm_metadata",      "adm_preferences",   "adm_popular",     "adm_latest",
    "adm_search",        "adm_anime_details", "adm_episode_list", "adm_hoster_list",
    "adm_video_list",
};

// Serves an addon request through the host client. The context is the Http the
// addon was loaded with.
extern "C" char* HostHttpGet(void* ctx, const char* url, const char* headersJson) {
    auto* http = static_cast<Http*>(ctx);
    if (http == nullptr || url == nullptr) {
        return nullptr;
    }

    std::map<std::string, std::string> headers;
    if (headersJson != nullptr) {
        nlohmann::json parsed = nlohmann::json::parse(headersJson, nullptr, false);
        if (parsed.is_object()) {
            for (auto& [name, value] : parsed.items()) {
                if (value.is_string()) {
                    headers[name] = value.get<std::string>();
                }
            }
        }
    }

    std::optional<std::string> body = http->GetText(url, headers);
    if (!body) {
        return nullptr;
    }

    // The addon releases this through free_string, so it comes from our heap.
    char* out = static_cast<char*>(std::malloc(body->size() + 1));
    if (out == nullptr) {
        return nullptr;
    }
    std::memcpy(out, body->c_str(), body->size() + 1);
    return out;
}

// Releases a string the table above returned.
extern "C" void HostFreeString(void*, char* text) {
    std::free(text);
}

// Collects a diagnostic line from an addon.
extern "C" void HostLog(void*, const char* message) {
    if (message != nullptr) {
        OutputDebugStringA(message);
        OutputDebugStringA("\n");
    }
}

// Reads a string field, tolerating its absence.
std::string Field(const nlohmann::json& object, const char* key) {
    auto it = object.find(key);
    return it != object.end() && it->is_string() ? it->get<std::string>() : std::string();
}

}  // namespace

// Reason the last load failed.
const std::string& Addon::LastError() {
    return g_lastError;
}

// Unloads the library.
Addon::~Addon() {
    Release();
}

void Addon::Release() {
    if (module_ != nullptr) {
        FreeLibrary(static_cast<HMODULE>(module_));
        module_ = nullptr;
    }
}

Addon::Addon(Addon&& other) noexcept {
    *this = std::move(other);
}

Addon& Addon::operator=(Addon&& other) noexcept {
    if (this != &other) {
        Release();
        module_ = other.module_;
        free_ = other.free_;
        entries_ = std::move(other.entries_);
        meta_ = std::move(other.meta_);
        http_ = other.http_;
        other.module_ = nullptr;
        other.free_ = nullptr;
        other.http_ = nullptr;
    }
    return *this;
}

// Loads a source library, checks its ABI, hands it the host services and its
// settings, then reads back what it declares about itself.
std::unique_ptr<Addon> Addon::Load(const std::wstring& libraryPath, Http& http,
                                   const std::map<std::string, std::string>& config) {
    g_lastError.clear();

    HMODULE module = LoadLibraryExW(libraryPath.c_str(), nullptr,
                                    LOAD_WITH_ALTERED_SEARCH_PATH);
    if (module == nullptr) {
        g_lastError = "the library could not be loaded";
        return nullptr;
    }

    auto version = reinterpret_cast<uint32_t (*)()>(GetProcAddress(module, "adm_abi_version"));
    auto init = reinterpret_cast<void (*)(const AdmHost*)>(GetProcAddress(module, "adm_init"));
    auto setConfig = reinterpret_cast<void (*)(const char*)>(GetProcAddress(module, "adm_set_config"));
    auto release = GetProcAddress(module, "adm_free");

    if (version == nullptr || init == nullptr || setConfig == nullptr || release == nullptr) {
        FreeLibrary(module);
        g_lastError = "the library does not expose the addon ABI";
        return nullptr;
    }
    if (version() != ADM_ABI_VERSION) {
        FreeLibrary(module);
        g_lastError = "the addon was built against another ABI version";
        return nullptr;
    }

    std::unique_ptr<Addon> addon(new Addon());
    addon->module_ = module;
    addon->free_ = reinterpret_cast<void*>(release);
    addon->http_ = &http;

    for (const char* name : kEntries) {
        FARPROC entry = GetProcAddress(module, name);
        if (entry == nullptr) {
            g_lastError = std::string("the addon lacks the entry point ") + name;
            return nullptr;
        }
        addon->entries_[name] = reinterpret_cast<void*>(entry);
    }

    AdmHost table = {};
    table.size = sizeof(table);
    table.ctx = &http;
    table.http_get = HostHttpGet;
    table.free_string = HostFreeString;
    table.log = HostLog;
    init(&table);

    setConfig(nlohmann::json(config).dump().c_str());

    std::string error;
    std::optional<nlohmann::json> meta =
        addon->CallRaw(addon->entries_["adm_metadata"], nullptr, &error);
    if (!meta || !meta->is_object()) {
        g_lastError = error.empty() ? "the addon declares no metadata" : error;
        return nullptr;
    }

    addon->meta_.id = Field(*meta, "id");
    addon->meta_.name = Field(*meta, "name");
    addon->meta_.lang = Field(*meta, "lang");
    addon->meta_.baseUrl = Field(*meta, "baseUrl");
    addon->meta_.version = Field(*meta, "version");
    auto nsfw = meta->find("nsfw");
    addon->meta_.nsfw = nsfw != meta->end() && nsfw->is_boolean() && nsfw->get<bool>();

    if (addon->meta_.id.empty()) {
        g_lastError = "the addon declares no identifier";
        return nullptr;
    }
    return addon;
}

// Calls an entry point and unwraps the {"ok": ...} / {"error": ...} envelope.
std::optional<nlohmann::json> Addon::CallRaw(void* entry, const std::string* input,
                                             std::string* error) const {
    if (entry == nullptr) {
        if (error != nullptr) {
            *error = "unknown entry point";
        }
        return std::nullopt;
    }

    char* raw = nullptr;
    if (input != nullptr) {
        raw = reinterpret_cast<char* (*)(const char*)>(entry)(input->c_str());
    } else {
        raw = reinterpret_cast<char* (*)()>(entry)();
    }
    if (raw == nullptr) {
        if (error != nullptr) {
            *error = "the addon answered nothing";
        }
        return std::nullopt;
    }

    std::string text(raw);
    reinterpret_cast<void (*)(char*)>(free_)(raw);

    nlohmann::json envelope = nlohmann::json::parse(text, nullptr, false);
    if (envelope.is_discarded() || !envelope.is_object()) {
        if (error != nullptr) {
            *error = "the addon answered malformed JSON";
        }
        return std::nullopt;
    }

    auto failure = envelope.find("error");
    if (failure != envelope.end()) {
        if (error != nullptr) {
            *error = failure->is_string() ? failure->get<std::string>() : "unknown error";
        }
        return std::nullopt;
    }

    auto payload = envelope.find("ok");
    if (payload == envelope.end()) {
        if (error != nullptr) {
            *error = "the addon answered an envelope without payload";
        }
        return std::nullopt;
    }
    return *payload;
}

// Calls an entry point that takes a JSON argument.
std::optional<nlohmann::json> Addon::Call(const char* entry, const nlohmann::json& input,
                                          std::string* error) const {
    auto found = entries_.find(entry);
    if (found == entries_.end()) {
        if (error != nullptr) {
            *error = "unknown entry point";
        }
        return std::nullopt;
    }
    std::string payload = input.dump();
    return CallRaw(found->second, &payload, error);
}

// Reads the settings schema the addon declares.
std::vector<AddonPreference> Addon::Preferences() const {
    std::vector<AddonPreference> out;
    auto found = entries_.find("adm_preferences");
    if (found == entries_.end()) {
        return out;
    }

    std::optional<nlohmann::json> declared = CallRaw(found->second, nullptr, nullptr);
    if (!declared || !declared->is_array()) {
        return out;
    }

    for (const nlohmann::json& item : *declared) {
        if (!item.is_object()) {
            continue;
        }
        AddonPreference preference;
        preference.key = Field(item, "key");
        preference.title = Field(item, "title");
        preference.summary = Field(item, "summary");
        preference.defaultValue = Field(item, "default");
        preference.kind = Field(item, "type");
        auto options = item.find("options");
        if (options != item.end() && options->is_array()) {
            for (const nlohmann::json& option : *options) {
                if (option.is_string()) {
                    preference.options.push_back(option.get<std::string>());
                }
            }
        }
        if (!preference.key.empty()) {
            out.push_back(std::move(preference));
        }
    }
    return out;
}
