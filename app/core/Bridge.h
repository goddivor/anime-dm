#pragma once

#include <string>
#include <vector>

class AddonStore;
class Http;

// What the application does for the browser extension: it tells the native
// host how to be found, and keeps the list of the sites the sources serve.
namespace bridge {

// How the extension shows its panel, as the options decide.
struct Panel {
    std::string mode = "full";
    bool onPage = true;
    bool onLinks = true;
};

// Writes `<data>/sources.json`: the id, name, language, site and page
// patterns of every installed source, and the panel settings. Each library
// is loaded to be asked for its site, so this belongs off the interface
// thread.
void WriteSources(const AddonStore& store, Http& http, const Panel& panel);

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
// executable. The same browsers are asked to install the extension: the
// Chromium family from the Chrome Web Store, Firefox from the signed
// package shipped with the application, when it is there.
void RegisterHost(const std::vector<std::string>& enabled);

// Opens the page of the extension on the store in the default browser.
void OpenStorePage();

}  // namespace bridge
