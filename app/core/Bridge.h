#pragma once

#include <string>
#include <vector>

class AddonStore;
class Http;

// What the application does for the browser extension: it tells the native
// host how to be found, and keeps the list of the sites the sources serve.
namespace bridge {

// Writes `<data>/sources.json`: the id, name, language and site of every
// installed source. Each library is loaded to be asked for its site, so this
// belongs off the interface thread.
void WriteSources(const AddonStore& store, Http& http);

// A browser the host can be declared to.
struct Browser {
    const char* id;
    const wchar_t* name;
};

// Every browser the application knows, in the order the options list them.
const std::vector<Browser>& Browsers();

// The ids of every browser, the default choice.
std::vector<std::string> AllBrowsers();

// Declares the native messaging host to the browsers named by `enabled` and
// withdraws it from the others: the manifests go under `<data>/host`, the
// keys under HKCU. The host executable is looked for next to the running
// executable.
void RegisterHost(const std::vector<std::string>& enabled);

}  // namespace bridge
