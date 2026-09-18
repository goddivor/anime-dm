// The native messaging host the browser extension talks to. A browser starts
// it with the extension and speaks through stdin and stdout: each message is
// a four-byte length followed by a JSON document. The host answers three
// requests, hands the addresses to the application through WM_COPYDATA, and
// starts the application when it is not running.
#include <windows.h>

#include <cstdint>
#include <filesystem>
#include <fstream>
#include <string>
#include <vector>

#include "core/BridgeProtocol.h"
#include "third_party/json.hpp"

namespace {

constexpr wchar_t kAppExe[] = L"anime-dm.exe";

// Reads one message from the browser; false at the end of the stream.
bool ReadMessage(HANDLE in, std::string* text) {
    uint32_t length = 0;
    DWORD got = 0;
    if (!ReadFile(in, &length, sizeof(length), &got, nullptr) || got != sizeof(length) ||
        length == 0 || length > (1u << 20)) {
        return false;
    }
    text->assign(length, '\0');
    size_t read = 0;
    while (read < length) {
        if (!ReadFile(in, text->data() + read, length - static_cast<DWORD>(read), &got, nullptr) ||
            got == 0) {
            return false;
        }
        read += got;
    }
    return true;
}

// Sends one message to the browser.
void WriteMessage(HANDLE out, const nlohmann::json& message) {
    std::string text = message.dump();
    uint32_t length = static_cast<uint32_t>(text.size());
    DWORD put = 0;
    WriteFile(out, &length, sizeof(length), &put, nullptr);
    WriteFile(out, text.data(), length, &put, nullptr);
    FlushFileBuffers(out);
}

// The folder of this executable, where the application lives too.
std::wstring ExeDir() {
    wchar_t path[MAX_PATH] = {};
    GetModuleFileNameW(nullptr, path, MAX_PATH);
    std::wstring file(path);
    size_t cut = file.find_last_of(L"\\/");
    return cut == std::wstring::npos ? std::wstring() : file.substr(0, cut);
}

// `%APPDATA%\anime-dm\sources.json`, as the application writes it.
std::string ReadSources() {
    wchar_t buffer[MAX_PATH] = {};
    DWORD length = GetEnvironmentVariableW(L"APPDATA", buffer, MAX_PATH);
    if (length == 0 || length >= MAX_PATH) {
        return std::string();
    }
    std::ifstream file(std::filesystem::path(std::wstring(buffer) + L"\\anime-dm\\sources.json"),
                       std::ios::binary);
    if (!file) {
        return std::string();
    }
    return std::string(std::istreambuf_iterator<char>(file), std::istreambuf_iterator<char>());
}

std::wstring Widen(const std::string& text) {
    if (text.empty()) {
        return std::wstring();
    }
    int size = MultiByteToWideChar(CP_UTF8, 0, text.data(), static_cast<int>(text.size()),
                                   nullptr, 0);
    std::wstring wide(static_cast<size_t>(size), L'\0');
    MultiByteToWideChar(CP_UTF8, 0, text.data(), static_cast<int>(text.size()), wide.data(), size);
    return wide;
}

// Hands a message to the running application; false when it is not running.
bool HandToApplication(const nlohmann::json& message) {
    HWND window = FindWindowW(bridge::kWindowClass, nullptr);
    if (window == nullptr) {
        return false;
    }
    DWORD process = 0;
    GetWindowThreadProcessId(window, &process);
    AllowSetForegroundWindow(process);

    std::string text = message.dump();
    COPYDATASTRUCT data = {};
    data.dwData = bridge::kCopyDataMark;
    data.cbData = static_cast<DWORD>(text.size());
    data.lpData = text.data();
    SendMessageTimeoutW(window, WM_COPYDATA, 0, reinterpret_cast<LPARAM>(&data),
                        SMTO_ABORTIFHUNG, 5000, nullptr);
    return true;
}

// Starts the application with an address to add.
bool StartApplication(const std::string& url) {
    std::wstring exe = ExeDir() + L"\\" + kAppExe;
    std::wstring line = L"\"" + exe + L"\" " + bridge::kAddSwitch + L" \"" + Widen(url) + L"\"";
    STARTUPINFOW startup = {};
    startup.cb = sizeof(startup);
    PROCESS_INFORMATION process = {};
    if (!CreateProcessW(exe.c_str(), line.data(), nullptr, nullptr, FALSE, 0, nullptr, nullptr,
                        &startup, &process)) {
        return false;
    }
    CloseHandle(process.hThread);
    CloseHandle(process.hProcess);
    return true;
}

// Answers one request of the extension.
nlohmann::json Answer(const nlohmann::json& request) {
    std::string kind = request.value("kind", std::string());
    if (kind == "sources") {
        nlohmann::json sources = nlohmann::json::parse(ReadSources(), nullptr, false);
        if (!sources.is_object()) {
            sources = {{"sources", nlohmann::json::array()}};
        }
        sources["ok"] = true;
        return sources;
    }
    if (kind == "add") {
        std::string url = request.value("url", std::string());
        if (url.empty()) {
            return {{"ok", false}, {"error", "no url"}};
        }
        bool done = HandToApplication({{"kind", "add"}, {"url", url}}) || StartApplication(url);
        return {{"ok", done}};
    }
    if (kind == "ping") {
        return {{"ok", true}, {"running", FindWindowW(bridge::kWindowClass, nullptr) != nullptr}};
    }
    return {{"ok", false}, {"error", "unknown request"}};
}

}  // namespace

// Serves the browser until it closes the pipe.
int WINAPI wWinMain(HINSTANCE, HINSTANCE, PWSTR, int) {
    HANDLE in = GetStdHandle(STD_INPUT_HANDLE);
    HANDLE out = GetStdHandle(STD_OUTPUT_HANDLE);
    std::string text;
    while (ReadMessage(in, &text)) {
        nlohmann::json request = nlohmann::json::parse(text, nullptr, false);
        if (!request.is_object()) {
            WriteMessage(out, {{"ok", false}, {"error", "bad json"}});
            continue;
        }
        WriteMessage(out, Answer(request));
    }
    return 0;
}
