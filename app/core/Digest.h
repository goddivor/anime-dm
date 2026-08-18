#pragma once

#include <cstdint>
#include <string>
#include <vector>

// The store serves native libraries, so their digest is checked before the
// loader ever sees them.
namespace digest {

// Lowercase hexadecimal SHA-256 of a buffer, empty when the platform refuses.
std::string Sha256Hex(const std::vector<uint8_t>& bytes);

}  // namespace digest
