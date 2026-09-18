#pragma once

#include <string>

class AddonStore;
class Http;

// What the application does for the browser extension: it tells the native
// host how to be found, and keeps the list of the sites the sources serve.
namespace bridge {

// Writes `<data>/sources.json`: the id, name, language and site of every
// installed source. Each library is loaded to be asked for its site, so this
// belongs off the interface thread.
void WriteSources(const AddonStore& store, Http& http);

// Declares the native messaging host to every browser that reads the
// registry: the manifests go under `<data>/host`, the keys under HKCU. The
// host executable is looked for next to the running executable.
void RegisterHost();

}  // namespace bridge
