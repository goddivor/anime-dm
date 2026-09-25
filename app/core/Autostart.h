#pragma once

// Whether the application starts when the user signs in: a value under the
// Run key of HKCU, pointing at the running executable.
namespace autostart {

// The switch the Run value adds: a session start opens the application in
// the notification area alone, its window hidden.
constexpr wchar_t kTraySwitch[] = L"--tray";

bool Enabled();
void Set(bool enabled);

}  // namespace autostart
