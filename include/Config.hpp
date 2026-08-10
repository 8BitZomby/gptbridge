#ifndef GPTB_CONFIG_HPP
#define GPTB_CONFIG_HPP

#include <string>


/**
 * SessionMode
 * Controls whether active-project state is shared globally or kept per terminal
 */
enum class SessionMode {
    Global,
    PerTerminal
};


/**
 * getSessionMode()
 * Returns the currently configured session mode
 */
SessionMode getSessionMode();


/**
 * sessionModeToString()
 * Returns a human-readable name for a session mode
 */
std::string sessionModeToString(SessionMode mode);


/**
 * sessionModeFromString()
 * Returns session mode from a string
 */
SessionMode sessionModeFromString(const std::string& mode);


/**
 * setSessionMode()
 * Saves the selected session mode to the global config file
 */
void setSessionMode(SessionMode mode);


#endif
