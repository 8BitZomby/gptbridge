#include "Random.hpp"

#include <Security/SecRandom.h>

#include <stdexcept>
#include <string>
#include <vector>


/**
 * generateSecureRandomHex()
 * Generates the requested number of cryptographically secure random bytes
 * and returns them as lowercase hex text
 */
std::string generateSecureRandomHex(std::size_t byteCount) {
    // Allocate exactly the number of random bytes requested by the caller
    std::vector<unsigned char> randomBytes(byteCount);

    // Ask macOS for cryptographically secure random bytes from the OS random source
    const int result = SecRandomCopyBytes(
            kSecRandomDefault,
            randomBytes.size(),
            randomBytes.data()
    );

    if(result != errSecSuccess) {
        throw std::runtime_error("Failed to generate secure random bytes");
    }

    // Each byte becomes exactly two lowercase hexadecimal characters
    constexpr char hexDigits[] = "0123456789abcdef";

    std::string encoded;
    encoded.reserve(randomBytes.size() * 2);

    for(const unsigned char byte : randomBytes) {
        // Upper 4 bits become the first hex digit
        encoded.push_back(hexDigits[byte >> 4]);

        // Lower 4 bits become the second hex digit
        encoded.push_back(hexDigits[byte & 0x0F]);
    }

    return encoded;
}
