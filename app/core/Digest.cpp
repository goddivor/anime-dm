#include "core/Digest.h"

#include <windows.h>
#include <bcrypt.h>

namespace {

constexpr char kHex[] = "0123456789abcdef";

}  // namespace

namespace digest {

// Lowercase hexadecimal SHA-256 of a buffer.
std::string Sha256Hex(const std::vector<uint8_t>& bytes) {
    BCRYPT_ALG_HANDLE algorithm = nullptr;
    if (BCryptOpenAlgorithmProvider(&algorithm, BCRYPT_SHA256_ALGORITHM, nullptr, 0) != 0) {
        return std::string();
    }

    DWORD length = 0;
    DWORD written = 0;
    std::vector<uint8_t> hash;
    std::string hex;

    if (BCryptGetProperty(algorithm, BCRYPT_HASH_LENGTH, reinterpret_cast<PUCHAR>(&length),
                          sizeof(length), &written, 0) == 0) {
        hash.resize(length);
        if (BCryptHash(algorithm, nullptr, 0, const_cast<PUCHAR>(bytes.data()),
                       static_cast<ULONG>(bytes.size()), hash.data(),
                       static_cast<ULONG>(hash.size())) == 0) {
            hex.reserve(hash.size() * 2);
            for (uint8_t byte : hash) {
                hex.push_back(kHex[byte >> 4]);
                hex.push_back(kHex[byte & 0x0F]);
            }
        }
    }

    BCryptCloseAlgorithmProvider(algorithm, 0);
    return hex;
}

}  // namespace digest
