#include "core/Http.h"

#include <windows.h>
#include <winhttp.h>

#include "core/Text.h"

namespace {

constexpr wchar_t kUserAgent[] =
    L"Mozilla/5.0 (Windows NT 10.0; Win64; x64) AppleWebKit/537.36 (KHTML, like Gecko) "
    L"Chrome/124.0.0.0 Safari/537.36";
constexpr int kTimeoutMs = 20000;

// Splits a URL into the pieces WinHTTP needs.
struct Target {
    std::wstring host;
    std::wstring path;
    unsigned short port = 0;
    bool secure = false;
};

std::optional<Target> Split(const std::string& url) {
    std::wstring wide = Widen(url);

    URL_COMPONENTS parts = {};
    parts.dwStructSize = sizeof(parts);
    parts.dwHostNameLength = static_cast<DWORD>(-1);
    parts.dwUrlPathLength = static_cast<DWORD>(-1);
    parts.dwExtraInfoLength = static_cast<DWORD>(-1);

    if (!WinHttpCrackUrl(wide.c_str(), static_cast<DWORD>(wide.size()), 0, &parts)) {
        return std::nullopt;
    }

    Target target;
    target.host.assign(parts.lpszHostName, parts.dwHostNameLength);
    target.path.assign(parts.lpszUrlPath, parts.dwUrlPathLength);
    if (parts.dwExtraInfoLength > 0) {
        target.path.append(parts.lpszExtraInfo, parts.dwExtraInfoLength);
    }
    if (target.path.empty()) {
        target.path = L"/";
    }
    target.port = parts.nPort;
    target.secure = parts.nScheme == INTERNET_SCHEME_HTTPS;
    return target;
}

// Joins the headers into the single block WinHTTP takes.
std::wstring JoinHeaders(const std::map<std::string, std::string>& headers) {
    std::wstring block;
    for (const auto& [name, value] : headers) {
        block += Widen(name);
        block += L": ";
        block += Widen(value);
        block += L"\r\n";
    }
    return block;
}

}  // namespace

// Opens the session shared by every request of the process.
Http::Http() {
    session_ = WinHttpOpen(kUserAgent, WINHTTP_ACCESS_TYPE_AUTOMATIC_PROXY,
                           WINHTTP_NO_PROXY_NAME, WINHTTP_NO_PROXY_BYPASS, 0);
    if (session_ != nullptr) {
        WinHttpSetTimeouts(session_, kTimeoutMs, kTimeoutMs, kTimeoutMs, kTimeoutMs);
    }
}

// Closes the session.
Http::~Http() {
    if (session_ != nullptr) {
        WinHttpCloseHandle(session_);
        session_ = nullptr;
    }
}

// Runs one request and reads the whole body.
std::optional<std::vector<uint8_t>> Http::Fetch(const std::string& url,
                                                const std::map<std::string, std::string>& headers) {
    if (session_ == nullptr) {
        return std::nullopt;
    }
    std::optional<Target> target = Split(url);
    if (!target) {
        return std::nullopt;
    }

    HINTERNET connection = WinHttpConnect(session_, target->host.c_str(), target->port, 0);
    if (connection == nullptr) {
        return std::nullopt;
    }

    DWORD flags = target->secure ? WINHTTP_FLAG_SECURE : 0;
    HINTERNET request = WinHttpOpenRequest(connection, L"GET", target->path.c_str(), nullptr,
                                           WINHTTP_NO_REFERER, WINHTTP_DEFAULT_ACCEPT_TYPES, flags);
    if (request == nullptr) {
        WinHttpCloseHandle(connection);
        return std::nullopt;
    }

    std::wstring block = JoinHeaders(headers);
    if (!block.empty()) {
        WinHttpAddRequestHeaders(request, block.c_str(), static_cast<DWORD>(block.size()),
                                 WINHTTP_ADDREQ_FLAG_ADD | WINHTTP_ADDREQ_FLAG_REPLACE);
    }

    std::optional<std::vector<uint8_t>> body;
    if (WinHttpSendRequest(request, WINHTTP_NO_ADDITIONAL_HEADERS, 0, WINHTTP_NO_REQUEST_DATA, 0, 0,
                           0) &&
        WinHttpReceiveResponse(request, nullptr)) {
        DWORD status = 0;
        DWORD size = sizeof(status);
        WinHttpQueryHeaders(request, WINHTTP_QUERY_STATUS_CODE | WINHTTP_QUERY_FLAG_NUMBER,
                            WINHTTP_HEADER_NAME_BY_INDEX, &status, &size, WINHTTP_NO_HEADER_INDEX);

        if (status >= 200 && status < 300) {
            std::vector<uint8_t> bytes;
            DWORD available = 0;
            while (WinHttpQueryDataAvailable(request, &available) && available > 0) {
                size_t offset = bytes.size();
                bytes.resize(offset + available);
                DWORD read = 0;
                if (!WinHttpReadData(request, bytes.data() + offset, available, &read)) {
                    break;
                }
                bytes.resize(offset + read);
            }
            body = std::move(bytes);
        }
    }

    WinHttpCloseHandle(request);
    WinHttpCloseHandle(connection);
    return body;
}

// Fetches a URL as text.
std::optional<std::string> Http::GetText(const std::string& url,
                                         const std::map<std::string, std::string>& headers) {
    std::optional<std::vector<uint8_t>> bytes = Fetch(url, headers);
    if (!bytes) {
        return std::nullopt;
    }
    return std::string(bytes->begin(), bytes->end());
}

// Fetches a URL as bytes.
std::optional<std::vector<uint8_t>> Http::GetBytes(
    const std::string& url, const std::map<std::string, std::string>& headers) {
    return Fetch(url, headers);
}
