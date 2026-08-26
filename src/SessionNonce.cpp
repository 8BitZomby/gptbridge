#include "SessionNonce.hpp"
#include "Random.hpp"

#include <string>


/**
 * generateSessionNonce()
 * Generates 128 bits of cryptographically secure randomness and returns
 * the resulting 16 bytes as a 32-character lowercase hex token
 */
std::string generateSessionNonce() {
    return generateSecureRandomHex(16);
}
