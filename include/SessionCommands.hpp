#ifndef GPTB_SESSION_COMMANDS_HPP
#define GPTB_SESSION_COMMANDS_HPP

#include <string>


enum class SessionCommand {
    List,
    Close,
    Global,
    PerTerminal,
    Unknown
};


/**
 * Converts a session subcommand string into the corresponding SessionCommand value
 */
SessionCommand parseSessionCommand(const std::string& command);


/**
 * handleSessionCommand()
 * Handles session listing, closing, and legacy sesion-mode commands.
 * Handles "gptb session <close|list|global|per-terminal>".
 */
int handleSessionCommand(int argc, char* argv[]);


#endif
