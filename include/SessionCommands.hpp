#ifndef GPTB_SESSION_COMMANDS_HPP
#define GPTB_SESSION_COMMANDS_HPP

#include <string>


enum class SessionCommand {
    List,
    Global,
    PerTerminal,
    Unknown
};


/**
 * Converts a session subcommand string into the corresponding SessionCommand value
 */
SessionCommand parseSessionCommand(const std::string& command);


/**
 * Handles "gptb session <list|global|per-terminal>".
 */
int handleSessionCommand(int argc, char* argv[]);


#endif
