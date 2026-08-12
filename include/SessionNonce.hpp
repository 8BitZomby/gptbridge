#ifndef GPTB_SESSION_NONCE_HPP
#define GPTB_SESSION_NONCE_HPP

#include <string>


/**
 * generateSessionNonce()
 * Generates a fresh per-capture nonce/session token used to distinguish
 * legitimate gptbridge control frames from ordinary terminal output.
 *
 * The token is generated from cryptographically secure random bytes. The
 * randome bytes are encoded as lowercase hexadecimal text so the token
 * can be embedded safely in the text-oriented control protocol.
 */
std::string generateSessionNonce();


#endif
