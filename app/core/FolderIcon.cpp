#include "core/FolderIcon.h"

#include <windows.h>
#include <objbase.h>
#include <shellapi.h>
#include <shlobj.h>

#include <filesystem>
#include <fstream>

#include "core/Digest.h"
#include "core/Paths.h"
#include "core/Text.h"

namespace {

// A recipe is the ImageMagick argument list of RightClickFolderIconTools,
// carried over from the Tauri application unchanged. `{INPUT}` names the
// poster, `{ASSETS}` the layer images, `{TMP}` the scratch image a pass hands
// to the next.
struct Recipe {
    const char* id;
    const char* const* passes[2];
    int passCount;
};


const char* const kNone[] = {
    "{INPUT}", "-resize", "512x512", "-background", "none", "-gravity", "center", "-extent",
    "512x512",
    nullptr,
};

const char* const kShadow[] = {
    "{INPUT}", "-resize", "490x490", "(", "+clone", "-background", "BLACK", "-shadow",
    "60x5+5+6.5", ")", "+swap", "-background", "none", "-layers", "merge", "-gravity",
    "center", "-extent", "512x512",
    nullptr,
};

const char* const kDvdcaseBluray[] = {
    "(", "-size", "512x512", "xc:none", ")", "-compose", "Over", "(", "{INPUT}", "-resize",
    "340x438^", "-gravity", "center", "-extent", "340x438", "+repage", "-background", "none",
    "-gravity", "Northwest", "-geometry", "+78+48", ")", "-compose", "Over", "-composite", "(",
    "{ASSETS}/dvdcase-bluray.png", "-resize", "512x512!", ")", "-compose", "Over",
    "-composite", "(", "+clone", "-background", "BLACK", "-shadow", "0x2+2+2.5", ")", "+swap",
    "-background", "none", "-layers", "merge", "-extent", "512x512",
    nullptr,
};

const char* const kFolderHorizontal[] = {
    "(", "-size", "512x512", "xc:none", ")", "-compose", "Over", "(", "{INPUT}", "-scale",
    "512x512!", "-blur", "0x8", "{ASSETS}/folderhorizontal-top.png", ")", "-compose", "over",
    "-composite", "(", "{ASSETS}/folderhorizontal-topfx.png", "-scale", "512x512!", ")",
    "-compose", "over", "-composite", "(", "{ASSETS}/folderhorizontal-topshadow.png", "-scale",
    "512x512!", ")", "-compose", "over", "-composite", "(", "{INPUT}", "-resize", "495x307^",
    "-gravity", "center", "-extent", "495x307", "+repage", "-gravity", "Northwest",
    "-geometry", "+8+141", "{ASSETS}/folderhorizontal-main.png", ")", "-compose", "over",
    "-composite", "(", "{ASSETS}/folderhorizontal-mainfx.png", "-scale", "512x512!", ")",
    "-compose", "over", "-composite",
    nullptr,
};

const char* const kFolderVertical[] = {
    "(", "-size", "512x512", "xc:none", ")", "-compose", "Over", "(", "{INPUT}", "-scale",
    "512x512!", "-blur", "0x8", "{ASSETS}/foldervertical-side.png", ")", "-compose", "over",
    "-composite", "(", "{ASSETS}/foldervertical-sidefx.png", "-scale", "512x512!", ")",
    "-compose", "over", "-composite", "(", "{ASSETS}/foldervertical-sideshadow.png", "-scale",
    "512x512!", ")", "-compose", "over", "-composite", "(", "{INPUT}", "-resize", "346x490^",
    "-gravity", "center", "-extent", "346x490", "+repage", "-gravity", "Northwest",
    "-geometry", "+70+14", "{ASSETS}/foldervertical-main.png", ")", "-compose", "over",
    "-composite", "(", "{ASSETS}/foldervertical-mainfx.png", "-scale", "512x512!", ")",
    "-compose", "over", "-composite",
    nullptr,
};

const char* const kDvdcaseTransparent[] = {
    "(", "-size", "512x512", "xc:none", ")", "-compose", "Over", "(", "{INPUT}", "-resize",
    "336x474^", "-gravity", "center", "-extent", "336x474", "+repage", "-gravity", "Northwest",
    "-geometry", "+108+14", "{ASSETS}/dvdcase-plastic-mask.png", ")", "-compose", "over",
    "-composite", "(", "{ASSETS}/dvdcase-plastic.png", "-resize", "512x512!", ")", "-compose",
    "Over", "-composite",
    nullptr,
};

const char* const kDvdboxDark[] = {
    "(", "-size", "512x512", "xc:none", ")", "-compose", "Over", "(",
    "{ASSETS}/disc-vinyl.png", "-scale", "340x340!", "-background", "none", "-extent",
    "512x512-164-84", "(", "+clone", "-background", "BLACK", "-shadow", "100x1.3+2+2", ")",
    "+swap", "-background", "none", "-layers", "merge", "-extent", "512x512", ")", "-compose",
    "Over", "-composite", "(", "{INPUT}", "-resize", "340x483^", "-gravity", "center",
    "-extent", "340x483", "+repage", "-background", "none", "-gravity", "Northwest",
    "-geometry", "+7+11", ")", "-compose", "Over", "-composite", "(",
    "{ASSETS}/dvdbox-dark.png", "-resize", "512x512!", ")", "-compose", "Over", "-composite",
    nullptr,
};

const char* const kDvdboxLight[] = {
    "(", "-size", "512x512", "xc:none", ")", "-compose", "Over", "(",
    "{ASSETS}/disc-vinyl.png", "-scale", "340x340!", "-background", "none", "-extent",
    "512x512-164-84", "(", "+clone", "-background", "BLACK", "-shadow", "100x1.3+2+2", ")",
    "+swap", "-background", "none", "-layers", "merge", "-extent", "512x512", ")", "-compose",
    "Over", "-composite", "(", "{INPUT}", "-resize", "340x483^", "-gravity", "center",
    "-extent", "340x483", "+repage", "-background", "none", "-gravity", "Northwest",
    "-geometry", "+7+11", ")", "-compose", "Over", "-composite", "(",
    "{ASSETS}/dvdbox-light.png", "-resize", "512x512!", ")", "-compose", "Over", "-composite",
    nullptr,
};

const char* const kWindows11A[] = {
    "(", "-size", "512x512", "xc:none", ")", "-compose", "Over", "(", "{INPUT}", "-resize",
    "2x2!", "-resize", "200x200!", "-scale", "390x390!", "-gravity", "Center", "-modulate",
    "105,150", "-brightness-contrast", "-10x0", "-blur", "0x8", "-brightness-contrast", "5x20",
    "-modulate", "95,100", "{ASSETS}/Win11A-Back.png", "-scale", "512x512!", ")", "-compose",
    "over", "-composite", "(", "{INPUT}", "-resize", "2x2!", "-resize", "200x200!", "-scale",
    "390x390!", "-gravity", "Center", "-modulate", "100,150", "-blur", "0x8",
    "-brightness-contrast", "5x20", "-brightness-contrast", "-50x10",
    "{ASSETS}/Win11A-Back-Gradient.png", "-scale", "512x512!", ")", "-compose", "over",
    "-composite", "(", "{INPUT}", "-resize", "498x320^", "-gravity", "center", "-extent",
    "498x320", "+repage", "-gravity", "Northwest", "-geometry", "+5+117",
    "{ASSETS}/Win11A-Front.png", ")", "-compose", "over", "-composite", "(", "{INPUT}",
    "-resize", "498x320^", "-gravity", "center", "-extent", "498x320", "+repage", "-gravity",
    "Northwest", "-geometry", "+5+117", "-brightness-contrast", "-9x10",
    "{ASSETS}/Win11A-Front-BevelShadow.png", ")", "-compose", "over", "-composite", "(",
    "{INPUT}", "-resize", "498x320^", "-gravity", "center", "-extent", "498x320", "+repage",
    "-gravity", "Northwest", "-geometry", "+5+117", "-modulate", "110,110",
    "-brightness-contrast", "25x10", "{ASSETS}/Win11A-Front-Bevel.png", ")", "-compose",
    "over", "-composite", "(", "{INPUT}", "-resize", "498x320^", "-gravity", "center",
    "-extent", "498x320", "+repage", "-gravity", "Northwest", "-geometry", "+5+117",
    "-brightness-contrast", "20x10", "-modulate", "110,110",
    "{ASSETS}/Win11A-Front-Gradient.png", ")", "-compose", "over", "-composite", "(",
    "{INPUT}", "-resize", "498x320^", "-gravity", "center", "-extent", "498x320", "+repage",
    "-gravity", "Northwest", "-geometry", "+5+117", "-brightness-contrast", "0x10",
    "-modulate", "94,100", "{ASSETS}/Win11A-Front-GradientShadow.png", ")", "-compose", "over",
    "-composite",
    nullptr,
};

const char* const kBeorigin[] = {
    "(", "-size", "512x512", "xc:none", ")", "-compose", "Over", "(", "{INPUT}", "-modulate",
    "100,150", "-modulate", "80,100", "-brightness-contrast", "0x5", "-modulate", "100,130",
    "-resize", "2x2!", "-resize", "200x200!", "-scale", "512x512!", "-gravity", "Center",
    "-blur", "0x8", "-brightness-contrast", "-5x0", "-brightness-contrast", "0x27", "-blur",
    "0x8", "{ASSETS}/BeOriginal-back.png", ")", "-compose", "over", "-composite", "(",
    "{ASSETS}/BeOriginal-BackFx.png", "-scale", "512x512!", ")", "-compose", "over",
    "-composite", "(", "{INPUT}", "-resize", "480x318^", "-gravity", "center", "-extent",
    "480x318", "+repage", "-gravity", "Northwest", "-geometry", "+18+124",
    "{ASSETS}/BeOriginal-front.png", ")", "-compose", "over", "-composite", "(",
    "{ASSETS}/BeOriginal-FrontFx.png", "-scale", "512x512!", ")", "-compose", "over",
    "-composite",
    nullptr,
};

const char* const kDiscart[] = {
    "(", "-size", "512x512", "xc:none", ")", "-compose", "Over", "(", "{INPUT}", "-resize",
    "485x485^", "-gravity", "center", "-extent", "485x485", "+repage", "-gravity", "center",
    "{ASSETS}/DiscArt-Main.png", ")", "-compose", "over", "-composite", "(",
    "{ASSETS}/DiscArt-Transparent.png", "-scale", "512x512!", ")", "-compose", "over",
    "-composite", "(", "{ASSETS}/DiscArt-Label.png", "-scale", "512x512!", ")", "-compose",
    "over", "-composite", "(", "{ASSETS}/DiscArt-Logo.png", "-scale", "512x512!", ")",
    "-compose", "over", "-composite", "(", "{INPUT}", "-scale", "900x900!", "-blur", "0x10",
    "-brightness-contrast", "0x30", "{ASSETS}/DiscArt-Border.png", ")", "-compose", "over",
    "-composite",
    nullptr,
};

const char* const kDualtabVerticalPass1[] = {
    "(", "-size", "512x512", "xc:none", ")", "-compose", "Over", "(", "{INPUT}", "-resize",
    "3x3!", "-resize", "200x200!", "-scale", "512x512!", "-modulate", "100,130",
    "-brightness-contrast", "8x13", "-blur", "0x10", "{ASSETS}/DualTabV-Tab2.png", ")",
    "-compose", "over", "-composite", "(", "{ASSETS}/DualTabV-Tab2FX.png", "-scale",
    "512x512!", ")", "-compose", "over", "-composite", "(", "{INPUT}", "-resize", "3x3!",
    "-resize", "200x200!", "-scale", "512x512!", "-modulate", "100,130",
    "-brightness-contrast", "8x13", "-blur", "0x10", "{ASSETS}/DualTabV-Tab1.png", ")",
    "-compose", "over", "-composite", "(", "{ASSETS}/DualTabV-Tab1FX.png", "-scale",
    "512x512!", ")", "-compose", "over", "-composite", "(", "{INPUT}", "-resize", "372x482^",
    "-gravity", "center", "-extent", "372x482", "+repage", "-brightness-contrast", "5x15",
    "-modulate", "100,110", "-gravity", "Northwest", "-geometry", "+51+4",
    "{ASSETS}/DualTabV-Front.png", ")", "-compose", "over", "-composite", "(",
    "{ASSETS}/DualTabV-FrontFX.png", "-scale", "512x512!", ")", "-compose", "over",
    "-composite",
    nullptr,
};

const char* const kDualtabVerticalPass2[] = {
    "(", "-size", "512x512", "xc:none", ")", "-compose", "Over", "(",
    "{ASSETS}/DualTabV-DropShadow.png", "-scale", "512x512!", ")", "-compose", "over",
    "-composite", "(", "{TMP}", "-scale", "512x512!", ")", "-compose", "over", "-composite",
    nullptr,
};

const Recipe kRecipes[] = {
    {"none", {kNone, nullptr}, 1},
    {"shadow", {kShadow, nullptr}, 1},
    {"dvdcase-bluray", {kDvdcaseBluray, nullptr}, 1},
    {"folder-horizontal", {kFolderHorizontal, nullptr}, 1},
    {"folder-vertical", {kFolderVertical, nullptr}, 1},
    {"dvdcase-transparent", {kDvdcaseTransparent, nullptr}, 1},
    {"dvdbox-dark", {kDvdboxDark, nullptr}, 1},
    {"dvdbox-light", {kDvdboxLight, nullptr}, 1},
    {"windows-11-a", {kWindows11A, nullptr}, 1},
    {"beorigin", {kBeorigin, nullptr}, 1},
    {"discart", {kDiscart, nullptr}, 1},
    {"dualtab-vertical", {kDualtabVerticalPass1, kDualtabVerticalPass2}, 2},
};

constexpr wchar_t kIcoDefine[] = L"icon:auto-resize=16,32,48,64,128,256";
constexpr DWORD kRenderTimeoutMs = 120000;

// The folder the executable runs from.
std::wstring ExeDir() {
    wchar_t path[MAX_PATH] = {};
    GetModuleFileNameW(nullptr, path, MAX_PATH);
    std::wstring file(path);
    size_t cut = file.find_last_of(L"\\/");
    return cut == std::wstring::npos ? std::wstring() : file.substr(0, cut);
}

bool IsDir(const std::wstring& path) {
    DWORD attributes = GetFileAttributesW(path.c_str());
    return attributes != INVALID_FILE_ATTRIBUTES && (attributes & FILE_ATTRIBUTE_DIRECTORY) != 0;
}

bool IsFile(const std::wstring& path) {
    DWORD attributes = GetFileAttributesW(path.c_str());
    return attributes != INVALID_FILE_ATTRIBUTES && (attributes & FILE_ATTRIBUTE_DIRECTORY) == 0;
}

// The layer images: next to the executable, or in the source tree above a
// build folder.
std::wstring AssetsDir() {
    std::wstring exe = ExeDir();
    for (const std::wstring& base : {exe, exe + L"\\.."}) {
        std::wstring dir = base + L"\\resources\\folder-templates\\images";
        if (IsDir(dir)) {
            return dir;
        }
    }
    return std::wstring();
}

// Wraps one argument for a Windows command line.
std::wstring Quote(const std::wstring& argument) {
    if (argument.find_first_of(L" \t\"") == std::wstring::npos && !argument.empty()) {
        return argument;
    }
    std::wstring quoted = L"\"";
    for (wchar_t c : argument) {
        if (c == L'"') {
            quoted += L"\\\"";
        } else {
            quoted += c;
        }
    }
    quoted += L"\"";
    return quoted;
}

// Runs a command without a window, waits for it, and keeps what it wrote on
// its error stream. False when it could not run or failed.
bool Run(const std::wstring& program, const std::vector<std::wstring>& arguments,
         std::string* stderrText) {
    std::wstring line = Quote(program);
    for (const std::wstring& argument : arguments) {
        line += L' ';
        line += Quote(argument);
    }

    SECURITY_ATTRIBUTES inheritable = {sizeof(SECURITY_ATTRIBUTES), nullptr, TRUE};
    HANDLE readEnd = nullptr;
    HANDLE writeEnd = nullptr;
    if (!CreatePipe(&readEnd, &writeEnd, &inheritable, 0)) {
        return false;
    }
    SetHandleInformation(readEnd, HANDLE_FLAG_INHERIT, 0);

    STARTUPINFOW startup = {};
    startup.cb = sizeof(startup);
    startup.dwFlags = STARTF_USESTDHANDLES;
    startup.hStdError = writeEnd;
    startup.hStdOutput = writeEnd;
    startup.hStdInput = GetStdHandle(STD_INPUT_HANDLE);
    PROCESS_INFORMATION process = {};

    std::vector<wchar_t> mutableLine(line.begin(), line.end());
    mutableLine.push_back(L'\0');
    BOOL started = CreateProcessW(program.c_str(), mutableLine.data(), nullptr, nullptr, TRUE,
                                  CREATE_NO_WINDOW, nullptr, nullptr, &startup, &process);
    CloseHandle(writeEnd);
    if (!started) {
        CloseHandle(readEnd);
        return false;
    }

    std::string captured;
    char buffer[1024];
    DWORD read = 0;
    while (ReadFile(readEnd, buffer, sizeof(buffer), &read, nullptr) && read > 0) {
        captured.append(buffer, read);
    }
    CloseHandle(readEnd);

    DWORD exitCode = 1;
    if (WaitForSingleObject(process.hProcess, kRenderTimeoutMs) == WAIT_OBJECT_0) {
        GetExitCodeProcess(process.hProcess, &exitCode);
    } else {
        TerminateProcess(process.hProcess, 1);
    }
    CloseHandle(process.hThread);
    CloseHandle(process.hProcess);

    if (stderrText != nullptr) {
        *stderrText = captured.substr(0, 400);
    }
    return exitCode == 0;
}

// Swaps the placeholders of a recipe for the real paths.
std::wstring Fill(const char* argument, const std::wstring& input, const std::wstring& assets,
                  const std::wstring& scratch) {
    std::wstring text = Widen(argument);
    for (const auto& [key, value] : {std::pair<std::wstring, std::wstring>{L"{INPUT}", input},
                                     {L"{ASSETS}", assets},
                                     {L"{TMP}", scratch}}) {
        size_t at = 0;
        while ((at = text.find(key, at)) != std::wstring::npos) {
            text.replace(at, key.size(), value);
            at += value.size();
        }
    }
    return text;
}

// Renders an icon file from a poster file with a recipe.
foldericon::Error Compose(const std::wstring& magick, const std::wstring& assets,
                          const std::wstring& poster, const Recipe& recipe,
                          const std::wstring& out, std::string* detail) {
    std::wstring scratch = out + L".pass.png";
    for (int pass = 0; pass < recipe.passCount; ++pass) {
        bool last = pass == recipe.passCount - 1;
        std::vector<std::wstring> arguments;
        for (const char* const* argument = recipe.passes[pass]; *argument != nullptr; ++argument) {
            arguments.push_back(Fill(*argument, poster, assets, scratch));
        }
        if (last) {
            arguments.push_back(L"-define");
            arguments.push_back(kIcoDefine);
        }
        arguments.push_back(last ? out : scratch);
        if (!Run(magick, arguments, detail)) {
            DeleteFileW(scratch.c_str());
            return foldericon::Error::Render;
        }
    }
    DeleteFileW(scratch.c_str());
    return foldericon::Error::None;
}

// Writes a file whole.
bool WriteWhole(const std::wstring& path, const void* data, size_t size) {
    std::ofstream file(std::filesystem::path(path), std::ios::binary | std::ios::trunc);
    if (!file) {
        return false;
    }
    file.write(static_cast<const char*>(data), static_cast<std::streamsize>(size));
    return file.good();
}

// Removes the icons an earlier apply left in a folder, whatever their name.
void RemoveOldIcons(const std::wstring& folder) {
    WIN32_FIND_DATAW found = {};
    HANDLE search = FindFirstFileW((folder + L"\\folder*.ico").c_str(), &found);
    if (search == INVALID_HANDLE_VALUE) {
        return;
    }
    do {
        std::wstring file = folder + L"\\" + found.cFileName;
        SetFileAttributesW(file.c_str(), FILE_ATTRIBUTE_NORMAL);
        DeleteFileW(file.c_str());
    } while (FindNextFileW(search, &found));
    FindClose(search);
}

// Bumps the modification time of a folder, which Explorer compares before it
// trusts what it remembers of it.
void TouchFolder(const std::wstring& folder) {
    HANDLE handle = CreateFileW(folder.c_str(), FILE_WRITE_ATTRIBUTES,
                                FILE_SHARE_READ | FILE_SHARE_WRITE | FILE_SHARE_DELETE, nullptr,
                                OPEN_EXISTING, FILE_FLAG_BACKUP_SEMANTICS, nullptr);
    if (handle == INVALID_HANDLE_VALUE) {
        return;
    }
    FILETIME now = {};
    GetSystemTimeAsFileTime(&now);
    SetFileTime(handle, nullptr, nullptr, &now);
    CloseHandle(handle);
}

// Tells Explorer the folder changed, in every way it listens to: the image
// it keeps for the folder in the system image list, the item and its
// attributes by identifier, the parent folder, then the icon cache itself,
// which only SHCNE_ASSOCCHANGED invalidates. Path notifications alone leave
// an open window showing the old picture for minutes.
void NotifyShell(const std::wstring& folder) {
    HRESULT com = CoInitializeEx(nullptr, COINIT_APARTMENTTHREADED);

    SHFILEINFOW info = {};
    if (SHGetFileInfoW(folder.c_str(), 0, &info, sizeof(info), SHGFI_SYSICONINDEX) != 0) {
        SHChangeNotify(SHCNE_UPDATEIMAGE, SHCNF_DWORD | SHCNF_FLUSH, nullptr,
                       reinterpret_cast<LPCVOID>(static_cast<INT_PTR>(info.iIcon)));
    }

    PIDLIST_ABSOLUTE pidl = nullptr;
    if (SUCCEEDED(SHParseDisplayName(folder.c_str(), nullptr, &pidl, 0, nullptr))) {
        SHChangeNotify(SHCNE_ATTRIBUTES, SHCNF_IDLIST | SHCNF_FLUSH, pidl, nullptr);
        SHChangeNotify(SHCNE_UPDATEITEM, SHCNF_IDLIST | SHCNF_FLUSH, pidl, nullptr);
        PIDLIST_ABSOLUTE parent = ILClone(pidl);
        if (parent != nullptr) {
            if (ILRemoveLastID(parent)) {
                SHChangeNotify(SHCNE_UPDATEDIR, SHCNF_IDLIST | SHCNF_FLUSH, parent, nullptr);
            }
            ILFree(parent);
        }
        ILFree(pidl);
    } else {
        SHChangeNotify(SHCNE_UPDATEITEM, SHCNF_PATHW | SHCNF_FLUSH, folder.c_str(), nullptr);
    }
    SHChangeNotify(SHCNE_ASSOCCHANGED, SHCNF_IDLIST | SHCNF_FLUSH, nullptr, nullptr);
    if (SUCCEEDED(com)) {
        CoUninitialize();
    }
}

// Points Explorer at the icon: desktop.ini, hidden files, read-only folder.
// The icon carries its key in its name: Explorer caches a folder icon by
// path, so a new file at the old name would keep showing the old picture.
bool Place(const std::wstring& cached, const std::wstring& folder, const std::wstring& key) {
    paths::EnsureDir(folder);
    std::wstring name = L"folder-" + key + L".ico";
    std::wstring ico = folder + L"\\" + name;
    std::wstring ini = folder + L"\\desktop.ini";

    // What an earlier apply left is hidden and system, which Windows refuses
    // to overwrite: strip the attributes before touching the files.
    DWORD folderAttributes = GetFileAttributesW(folder.c_str());
    if (folderAttributes != INVALID_FILE_ATTRIBUTES) {
        SetFileAttributesW(folder.c_str(), folderAttributes & ~FILE_ATTRIBUTE_READONLY);
    }
    RemoveOldIcons(folder);
    SetFileAttributesW(ini.c_str(), FILE_ATTRIBUTE_NORMAL);
    DeleteFileW(ini.c_str());

    if (!CopyFileW(cached.c_str(), ico.c_str(), FALSE)) {
        return false;
    }

    // The shell writes desktop.ini itself, and updates what it remembers of
    // the folder on the way: this is what the Customize tab and the folder
    // icon tools do, and what a hand-written file never triggers.
    SHFOLDERCUSTOMSETTINGS custom = {};
    custom.dwSize = sizeof(custom);
    custom.dwMask = FCSM_ICONFILE;
    custom.pszIconFile = const_cast<LPWSTR>(ico.c_str());
    custom.cchIconFile = 0;
    custom.iIconIndex = 0;
    if (FAILED(SHGetSetFolderCustomSettings(&custom, folder.c_str(), FCS_FORCEWRITE))) {
        std::string content = "[.ShellClassInfo]\r\nIconResource=" + Narrow(name) +
                              ",0\r\n[ViewState]\r\nMode=\r\nVid=\r\nFolderType=Generic\r\n";
        if (!WriteWhole(ini, content.data(), content.size())) {
            return false;
        }
    }

    SetFileAttributesW(ini.c_str(), FILE_ATTRIBUTE_HIDDEN | FILE_ATTRIBUTE_SYSTEM);
    SetFileAttributesW(ico.c_str(), FILE_ATTRIBUTE_HIDDEN);
    folderAttributes = GetFileAttributesW(folder.c_str());
    if (folderAttributes != INVALID_FILE_ATTRIBUTES) {
        SetFileAttributesW(folder.c_str(), folderAttributes | FILE_ATTRIBUTE_READONLY);
    }
    TouchFolder(folder);
    NotifyShell(folder);
    return true;
}

}  // namespace

namespace foldericon {

// The ids of the recipes.
std::vector<std::string> TemplateIds() {
    std::vector<std::string> ids;
    for (const Recipe& recipe : kRecipes) {
        ids.push_back(recipe.id);
    }
    return ids;
}

// ImageMagick: bundled next to the executable first, then on the PATH.
std::wstring MagickPath() {
    std::wstring exe = ExeDir();
    for (const std::wstring& base : {exe, exe + L"\\.."}) {
        std::wstring bundled = base + L"\\resources\\folder-templates\\bin\\magick.exe";
        if (IsFile(bundled)) {
            return bundled;
        }
    }
    wchar_t found[MAX_PATH] = {};
    if (SearchPathW(nullptr, L"magick.exe", nullptr, MAX_PATH, found, nullptr) > 0) {
        return found;
    }
    return std::wstring();
}

// Composes the icon of a poster with a recipe and applies it to a folder.
Error Apply(const std::wstring& folder, const std::vector<uint8_t>& poster,
            const std::string& templateId, std::string* detail) {
    const Recipe* recipe = nullptr;
    for (const Recipe& candidate : kRecipes) {
        if (templateId == candidate.id) {
            recipe = &candidate;
        }
    }
    if (recipe == nullptr) {
        return Error::UnknownTemplate;
    }
    std::wstring magick = MagickPath();
    if (magick.empty()) {
        return Error::NoMagick;
    }
    std::wstring assets = AssetsDir();
    if (assets.empty()) {
        return Error::NoAssets;
    }
    std::wstring cache = paths::DataDir();
    if (cache.empty() || poster.empty()) {
        return Error::Disk;
    }
    cache += L"\\icon-cache";
    paths::EnsureDir(cache);

    // The same poster with the same recipe is only ever rendered once.
    std::wstring key = Widen(digest::Sha256Hex(poster).substr(0, 16) + "_" + templateId);
    std::wstring cached = cache + L"\\" + key + L".ico";
    if (!IsFile(cached)) {
        std::wstring source = cache + L"\\" + key + L".src";
        if (!WriteWhole(source, poster.data(), poster.size())) {
            return Error::Disk;
        }
        Error error = Compose(magick, assets, source, *recipe, cached, detail);
        DeleteFileW(source.c_str());
        if (error != Error::None) {
            DeleteFileW(cached.c_str());
            return error;
        }
    }
    return Place(cached, folder, key) ? Error::None : Error::Disk;
}

// Writes cover.jpg and .nomedia into the folder.
bool AdaptForAniyomi(const std::wstring& folder, const std::vector<uint8_t>& poster) {
    if (poster.empty() || !paths::EnsureDir(folder)) {
        return false;
    }
    return WriteWhole(folder + L"\\cover.jpg", poster.data(), poster.size()) &&
           WriteWhole(folder + L"\\.nomedia", "", 0);
}

// Whether a folder already carries the Aniyomi files.
bool HasAniyomiFiles(const std::wstring& folder) {
    return IsFile(folder + L"\\cover.jpg") && IsFile(folder + L"\\.nomedia");
}

}  // namespace foldericon
