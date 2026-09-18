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

// The class of the main window of the application.
constexpr wchar_t kWindowClass[] = L"AnimeDmMainWindow";

// The mark of a WM_COPYDATA meant for the application; the payload is a JSON
// document in UTF-8, `{"kind":"add","url":"..."}` for now.
constexpr unsigned long kCopyDataMark = 0x31444D41;  // "ADM1"

// The switch that hands an address to a fresh instance.
constexpr wchar_t kAddSwitch[] = L"--add";

}  // namespace bridge
