#include "core/Digest.h"

#include <windows.h>
#include <bcrypt.h>

namespace {

constexpr char kHex[] = "0123456789abcdef";

// Reads a length property of an algorithm or hash handle.
bool Length(BCRYPT_HANDLE handle, LPCWSTR property, DWORD* value) {
    DWORD written = 0;
    return BCryptGetProperty(handle, property, reinterpret_cast<PUCHAR>(value), sizeof(*value),
                             &written, 0) == 0;
}

}  // namespace

namespace digest {

// Lowercase hexadecimal SHA-256 of a buffer.
std::string Sha256Hex(const std::vector<uint8_t>& bytes) {
    BCRYPT_ALG_HANDLE algorithm = nullptr;
    if (BCryptOpenAlgorithmProvider(&algorithm, BCRYPT_SHA256_ALGORITHM, nullptr, 0) != 0) {
        return std::string();
    }

    std::string hex;
    DWORD objectSize = 0;
    DWORD hashSize = 0;

    if (Length(algorithm, BCRYPT_OBJECT_LENGTH, &objectSize) &&
        Length(algorithm, BCRYPT_HASH_LENGTH, &hashSize)) {
        std::vector<uint8_t> object(objectSize);
        std::vector<uint8_t> hash(hashSize);
        BCRYPT_HASH_HANDLE handle = nullptr;

        if (BCryptCreateHash(algorithm, &handle, object.data(), objectSize, nullptr, 0, 0) == 0) {
            bool ok = BCryptHashData(handle, const_cast<PUCHAR>(bytes.data()),
                                     static_cast<ULONG>(bytes.size()), 0) == 0 &&
                      BCryptFinishHash(handle, hash.data(), hashSize, 0) == 0;
            BCryptDestroyHash(handle);

            if (ok) {
                hex.reserve(hash.size() * 2);
                for (uint8_t byte : hash) {
                    hex.push_back(kHex[byte >> 4]);
                    hex.push_back(kHex[byte & 0x0F]);
                }
            }
        }
    }

    BCryptCloseAlgorithmProvider(algorithm, 0);
    return hex;
}

}  // namespace digest
