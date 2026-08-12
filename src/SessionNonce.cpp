#include "SessionNonce.hpp"

#include <Security/SecRandom.h>

#include <array>
#include <cstddef>
#include <stdexcept>
#include <string>


/**
 * generateSessionNonce()
 * Generates 128 bits of cryptographically secure randomness and converts the
 * resulting 16 bytes into a 32-character lowercase hex session token.
 */
std::string generateSessionNonce() {
    // 16 random bytes provides 128 bits of randomness, making accidental reproduction
    // of the session token effectively negligible for control-frame identification
    std::array<unsigned char, 16> randomBytes{};

    // Asks macOS for cryptographically secure random bytes using the OS's secure
    // random source. Avoids predictable random numbers.
    const int result = SecRandomCopyBytes(kSecRandomDefault, randomBytes.size(), randomBytes.data());

    if(result != errSecSuccess) {
        throw std::runtime_error("Failed to generate session nonce");
    }

    // Convert each binary byte into two hex characters. Hex encoding keeps the token
    // printable and safe to embed inside the control-frame header without delimeter bytes

    // Create lookup table
    constexpr char hexDigits[] = "0123456789abcdef";

    std::string nonce;
    // Allocate memory for 16 bytes (32 hex characters)
    nonce.reserve(randomBytes.size() * 2);
    // Convert binary to hex
    for(const unsigned char byte : randomBytes) {
        // Extract upper 4 bits - use right shift of 4 bits
        nonce.push_back(hexDigits[(byte >> 4)]);
        // Extract lower 4 bits - & 0x0F eliminates the 4 bits we already converted
        nonce.push_back(hexDigits[byte & 0x0F]);
    }
    return nonce;
}
