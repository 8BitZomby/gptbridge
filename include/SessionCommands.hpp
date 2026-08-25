#ifndef GPTB_SESSION_COMMANDS_HPP
#define GPTB_SESSION_COMMANDS_HPP

#include <string>


enum class SessionCommand {
    List,
    Close,
    Unknown
};


/**
 * Converts a session subcommand string into the corresponding SessionCommand value
 */
SessionCommand parseSessionCommand(const std::string& command);


/**
 * handleSessionCommand()
 * Handles logical-session management commands.
 * Handles "gptb session <close|list>".
 */
int handleSessionCommand(int argc, char* argv[]);


#endif
