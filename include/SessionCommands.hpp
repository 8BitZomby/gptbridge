#ifndef GPTB_SESSION_COMMANDS_HPP
#define GPTB_SESSION_COMMANDS_HPP

#include <string>


enum class SessionCommand {
    Close,      // Lists all saved logical sessions and their runtime state
    Delete,     // Closes every live managed-shell attachment for one logical session
    List,       // Permanently deletes one inactive logical session
    Restore,    // Resores the most recently used or explicitly requested saved session
    Unknown     // Represents an unsupported session subcommand
};


/**
 * Converts a session subcommand string into the corresponding SessionCommand value
 */
SessionCommand parseSessionCommand(const std::string& command);


/**
 * handleSessionCommand()
 * Handles logical-session management commands.
 * Handles "gptb session <close|delete|list|restore>".
 */
int handleSessionCommand(int argc, char* argv[]);


#endif
