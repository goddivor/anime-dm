#pragma once

#include <cstdint>
#include <vector>

namespace cipher {

// Decrypts a buffer in place with AES-128 in CBC mode and strips the PKCS7
// padding, the scheme HLS playlists use. False when the key or the data is
// malformed.
bool AesCbcDecrypt(const std::vector<uint8_t>& key, const uint8_t iv[16],
                   std::vector<uint8_t>& data);

}  // namespace cipher
