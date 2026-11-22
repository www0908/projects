#include "crypto_util.h"
#include <openssl/evp.h>
#include <openssl/rand.h>
#include <openssl/crypto.h>
#include <sstream>
#include <iomanip>
#include <vector>
#include <stdexcept>
 namespace Security{
    constexpr int KDF_ITERATIONS = 210000;
    constexpr int SALT_LEN = 16;
    constexpr int HASH_LEN = 32;
 }
 static std::string ToHex(const unsigned char* data, size_t len) {
    std::stringstream ss;
    ss << std::hex << std::setfill('0');
    for (size_t i = 0; i < len; ++i) {
        ss << std::setw(2) << static_cast<int>(data[i]);
    }
    return ss.str();
}
static std::vector<unsigned char> FromHex(const std::string& hex) {
    std::vector<unsigned char> bytes;
    if (hex.length() % 2 != 0) return bytes;
    for (size_t i = 0; i < hex.length(); i += 2) {
        int byte;
        std::sscanf(hex.substr(i, 2).c_str(), "%02x", &byte);
        bytes.push_back(static_cast<unsigned char>(byte));
    }
    return bytes;
}