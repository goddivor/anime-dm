#pragma once

// What the application and the native messaging host agree on. The host is a
// separate executable the browsers start; it finds the application by its
// window class and hands it messages through WM_COPYDATA.
namespace bridge {

// The name of the native messaging host, as the browsers know it.
constexpr char kHostName[] = "com.animedm.host";

// The Chromium extension, whose id derives from the key in its manifest, and
// the Firefox extension, whose id is declared.
constexpr char kChromiumExtensionId[] = "kajalpjiomebkclalgjggcgjeiibkfcg";
constexpr char kFirefoxExtensionId[] = "adm@animedm.app";

// Where the browsers fetch the extension once it is published: the Chrome
// Web Store serves the whole Chromium family, and its update service is what
// an install key names; Firefox takes a signed package, shipped with the
// application under `resources\extension`.
constexpr wchar_t kChromiumStoreUrl[] =
    L"https://chromewebstore.google.com/detail/kajalpjiomebkclalgjggcgjeiibkfcg";
constexpr wchar_t kChromiumUpdateUrl[] = L"https://clients2.google.com/service/update2/crx";
constexpr wchar_t kFirefoxPackage[] = L"resources\\extension\\adm@animedm.app.xpi";

// The class of the main window of the application.
constexpr wchar_t kWindowClass[] = L"AnimeDmMainWindow";

// The mark of a WM_COPYDATA meant for the application; the payload is a JSON
// document in UTF-8: `{"kind":"add","url":"...","episode":"..."}`, the
// episode being optional.
constexpr unsigned long kCopyDataMark = 0x31444D41;  // "ADM1"

// The switches that hand an address, and the episode wanted on it, to a
// fresh instance.
constexpr wchar_t kAddSwitch[] = L"--add";
constexpr wchar_t kEpisodeSwitch[] = L"--episode";

}  // namespace bridge
