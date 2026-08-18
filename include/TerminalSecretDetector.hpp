#ifndef GPTB_TERMINAL_SECRET_DETECTOR_HPP
#define GPTB_TERMINAL_SECRET_DETECTOR_HPP

#include "TerminalInteraction.hpp"

#include <string>
#include <vector>


/**
 * SecretFinding
 * Describes one suspicious value detected in terminal command or output.
 */
struct SecretFinding {
    std::string reason;
    std::string location;
};


/**
 * detectTerminalSecrets()
 * Returns suspicious secret-like findings from selected terminal interactions.
 */
std::vector<SecretFinding> detectTerminalSecrets(
    const std::vector<TerminalInteraction>& interactions
);


#endif
