#ifndef GPTB_RANDOM_HPP
#define GPTB_RANDOM_HPP

#include <cstddef>
#include <string>


/**
 * generateSecureRandomHex()
 * Generates the requested number of cryptographically secure random bytes
 * and returns them as lowercase hexadecimal text
 */
std::string generateSecureRandomHex(std::size_t byteCount);


#endif
