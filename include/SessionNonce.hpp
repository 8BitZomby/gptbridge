#ifndef GPTB_SESSION_NONCE_HPP
#define GPTB_SESSION_NONCE_HPP

#include <string>


/**
 * generateSessionNonce()
 * Generates a fresh per-capture nonce used to distinguish legitimate
 * gptbridge private OSC metadata from unrelated terminal output.
 *
 * The nonce is generated from cryptographically secure random bytes. The
 * random bytes are encoded as lowercase hexadecimal text so the value can
 * be embedded safely in gptbridge's private OSC sequences.
 */
std::string generateSessionNonce();


#endif
