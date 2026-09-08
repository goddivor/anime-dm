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

// Reads a numeric header of an answered request.
bool QueryNumber(HINTERNET request, DWORD query, DWORD* value) {
    DWORD size = sizeof(*value);
    return WinHttpQueryHeaders(request, query | WINHTTP_QUERY_FLAG_NUMBER,
                               WINHTTP_HEADER_NAME_BY_INDEX, value, &size,
                               WINHTTP_NO_HEADER_INDEX) != FALSE;
}

// Reads a text header of an answered request; empty when absent.
std::wstring QueryText(HINTERNET request, DWORD query, const wchar_t* name = nullptr) {
    DWORD size = 0;
    WinHttpQueryHeaders(request, query, name, WINHTTP_NO_OUTPUT_BUFFER, &size,
                        WINHTTP_NO_HEADER_INDEX);
    if (GetLastError() != ERROR_INSUFFICIENT_BUFFER || size == 0) {
        return std::wstring();
    }
    std::wstring value(size / sizeof(wchar_t), L'\0');
    if (!WinHttpQueryHeaders(request, query, name, value.data(), &size, WINHTTP_NO_HEADER_INDEX)) {
        return std::wstring();
    }
    value.resize(size / sizeof(wchar_t));
    return value;
}

// Opens a request on a fresh connection, with the caller's headers attached.
// Both handles are returned so the caller closes them once it has read.
bool OpenRequest(HINTERNET session, const std::string& url,
                 const std::map<std::string, std::string>& headers,
                 const std::wstring& extraHeaders, HINTERNET* connection, HINTERNET* request) {
    std::optional<Target> target = Split(url);
    if (!target) {
        return false;
    }

    *connection = WinHttpConnect(session, target->host.c_str(), target->port, 0);
    if (*connection == nullptr) {
        return false;
    }

    DWORD flags = target->secure ? WINHTTP_FLAG_SECURE : 0;
    *request = WinHttpOpenRequest(*connection, L"GET", target->path.c_str(), nullptr,
                                  WINHTTP_NO_REFERER, WINHTTP_DEFAULT_ACCEPT_TYPES, flags);
    if (*request == nullptr) {
        WinHttpCloseHandle(*connection);
        *connection = nullptr;
        return false;
    }

    std::wstring block = JoinHeaders(headers) + extraHeaders;
    if (!block.empty()) {
        WinHttpAddRequestHeaders(*request, block.c_str(), static_cast<DWORD>(block.size()),
                                 WINHTTP_ADDREQ_FLAG_ADD | WINHTTP_ADDREQ_FLAG_REPLACE);
    }
    return true;
}

// Sends an opened request and reads its status; zero when it failed.
int SendAndReceive(HINTERNET request) {
    if (!WinHttpSendRequest(request, WINHTTP_NO_ADDITIONAL_HEADERS, 0, WINHTTP_NO_REQUEST_DATA, 0,
                            0, 0) ||
        !WinHttpReceiveResponse(request, nullptr)) {
        return 0;
    }
    DWORD status = 0;
    QueryNumber(request, WINHTTP_QUERY_STATUS_CODE, &status);
    return static_cast<int>(status);
}

// Reads the total size out of a `Content-Range: bytes a-b/total` header.
uint64_t TotalOfContentRange(const std::wstring& header) {
    size_t slash = header.find(L'/');
    if (slash == std::wstring::npos) {
        return 0;
    }
    return _wcstoui64(header.c_str() + slash + 1, nullptr, 10);
}

}  // namespace

// Asks for the first byte of a URL to learn its size and its range support.
std::optional<HttpProbe> Http::Probe(const std::string& url,
                                     const std::map<std::string, std::string>& headers) {
    if (session_ == nullptr) {
        return std::nullopt;
    }
    HINTERNET connection = nullptr;
    HINTERNET request = nullptr;
    if (!OpenRequest(session_, url, headers, L"Range: bytes=0-0\r\n", &connection, &request)) {
        return std::nullopt;
    }

    std::optional<HttpProbe> probe;
    int status = SendAndReceive(request);
    if (status > 0) {
        HttpProbe result;
        result.status = status;
        if (status == 206) {
            result.ranges = true;
            result.length =
                TotalOfContentRange(QueryText(request, WINHTTP_QUERY_CONTENT_RANGE));
        } else if (status == 200) {
            std::wstring length = QueryText(request, WINHTTP_QUERY_CONTENT_LENGTH);
            result.length = _wcstoui64(length.c_str(), nullptr, 10);
        }
        probe = result;
    }

    WinHttpCloseHandle(request);
    WinHttpCloseHandle(connection);
    return probe;
}

// Streams a byte range of a URL into a sink.
int Http::Stream(const std::string& url, const std::map<std::string, std::string>& headers,
                 uint64_t from, uint64_t to, const HttpAccept& accept, const HttpSink& sink) {
    if (session_ == nullptr) {
        return 0;
    }
    std::wstring range;
    if (from > 0 || to > 0) {
        range = L"Range: bytes=" + std::to_wstring(from) + L"-";
        if (to > 0) {
            range += std::to_wstring(to);
        }
        range += L"\r\n";
    }

    HINTERNET connection = nullptr;
    HINTERNET request = nullptr;
    if (!OpenRequest(session_, url, headers, range, &connection, &request)) {
        return 0;
    }

    int status = SendAndReceive(request);
    if (status >= 200 && status < 300 && accept(status)) {
        std::vector<uint8_t> chunk(64 * 1024);
        DWORD read = 0;
        while (WinHttpReadData(request, chunk.data(), static_cast<DWORD>(chunk.size()), &read) &&
               read > 0) {
            if (!sink(chunk.data(), read)) {
                break;
            }
        }
    }

    WinHttpCloseHandle(request);
    WinHttpCloseHandle(connection);
    return status;
}

namespace {

// Runs one request and reads the whole body.
std::optional<std::vector<uint8_t>> FetchWhole(HINTERNET session, const std::string& url,
                                               const std::map<std::string, std::string>& headers) {
    HINTERNET connection = nullptr;
    HINTERNET request = nullptr;
    if (!OpenRequest(session, url, headers, std::wstring(), &connection, &request)) {
        return std::nullopt;
    }

    std::optional<std::vector<uint8_t>> body;
    int status = SendAndReceive(request);
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

    WinHttpCloseHandle(request);
    WinHttpCloseHandle(connection);
    return body;
}

}  // namespace

// Runs one request and reads the whole body.
std::optional<std::vector<uint8_t>> Http::Fetch(const std::string& url,
                                                const std::map<std::string, std::string>& headers) {
    if (session_ == nullptr) {
        return std::nullopt;
    }
    return FetchWhole(session_, url, headers);
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
