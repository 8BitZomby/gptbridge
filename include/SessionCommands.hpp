#ifndef GPTB_SESSION_COMMANDS_HPP
#define GPTB_SESSION_COMMANDS_HPP


/**
 * Handles "gptb session <global|per-terminal>".
 */
int handleSessionCommand(int argc, char* argv[]);


/**
 * Handles "gptb status".
 */
int handleStatusCommand(int argc, char* argv[]);


#endif
