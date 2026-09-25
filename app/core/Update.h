#pragma once

#include <cstdint>
#include <optional>
#include <string>

class Http;

// A version of the application published on GitHub, with its installer.
struct UpdateInfo {
    std::string version;       // N.NN
    std::string installerUrl;  // the setup executable of the release
    std::string notes;         // the text of the release, plain
};

// The updates of the application: its releases are the GitHub Releases
// tagged win-v<N.NN>, each carrying the installer, which runs over the
// installed application and keeps the data of the user.
namespace update {

// The rank of a version, to compare two: "0.01" is 1, "1.00" is 100; -1 when
// the text is no version.
int Rank(const std::string& version);

// The newest Windows release, or an empty version when none is published;
// nothing when GitHub could not be reached. Network: off the interface thread.
std::optional<UpdateInfo> Latest(Http& http);

// Downloads the installer of a release into the temporary folder and returns
// its path, or an empty path when the download failed.
std::wstring Download(Http& http, const UpdateInfo& info);

}  // namespace update
