#pragma once

// Whether the application starts when the user signs in: a value under the
// Run key of HKCU, pointing at the running executable.
namespace autostart {

bool Enabled();
void Set(bool enabled);

}  // namespace autostart
