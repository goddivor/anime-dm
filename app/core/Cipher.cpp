#include "core/Cipher.h"

#include <windows.h>
#include <bcrypt.h>

namespace {

// Closes the BCrypt handles on every path out of the decryption.
struct Handles {
    BCRYPT_ALG_HANDLE algorithm = nullptr;
    BCRYPT_KEY_HANDLE key = nullptr;

    ~Handles() {
        if (key != nullptr) {
            BCryptDestroyKey(key);
        }
        if (algorithm != nullptr) {
            BCryptCloseAlgorithmProvider(algorithm, 0);
        }
    }
};

}  // namespace

namespace cipher {

// Decrypts a buffer in place with AES-128-CBC and strips the padding.
bool AesCbcDecrypt(const std::vector<uint8_t>& key, const uint8_t iv[16],
                   std::vector<uint8_t>& data) {
    if (key.size() != 16 || data.empty() || data.size() % 16 != 0) {
        return false;
    }

    Handles handles;
    if (BCryptOpenAlgorithmProvider(&handles.algorithm, BCRYPT_AES_ALGORITHM, nullptr, 0) != 0) {
        return false;
    }
    if (BCryptSetProperty(handles.algorithm, BCRYPT_CHAINING_MODE,
                          reinterpret_cast<PUCHAR>(const_cast<wchar_t*>(BCRYPT_CHAIN_MODE_CBC)),
                          sizeof(BCRYPT_CHAIN_MODE_CBC), 0) != 0) {
        return false;
    }
    if (BCryptGenerateSymmetricKey(handles.algorithm, &handles.key, nullptr, 0,
                                   const_cast<PUCHAR>(key.data()), static_cast<ULONG>(key.size()),
                                   0) != 0) {
        return false;
    }

    uint8_t chain[16];
    memcpy(chain, iv, sizeof(chain));
    ULONG produced = 0;
    if (BCryptDecrypt(handles.key, data.data(), static_cast<ULONG>(data.size()), nullptr, chain,
                      sizeof(chain), data.data(), static_cast<ULONG>(data.size()), &produced,
                      BCRYPT_BLOCK_PADDING) != 0) {
        return false;
    }
    data.resize(produced);
    return true;
}

}  // namespace cipher
