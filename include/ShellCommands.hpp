#ifndef GPTB_SHELL_COMMANDS_HPP
#define GPTB_SHELL_COMMANDS_HPP

#include <string>


/**
 * runManagedShell()
 * Launches the PTY-backed managed shell attached to the supplied logical session
 */
int runManagedShell(const std::string& sessionId);


/**
 * Handles "gptb capture".
 */
int handleCaptureCommand(int argc, char* argv[]);


/**
 * Handles "gptb shell-init <shell>".
 */
int handleShellInitCommand(int argc, char* argv[]);


/**
 * Handles internal "gptb shell-event ..." commands used by shell integration.
 */
int handleShellEventCommand(int argc, char* argv[]);


#endif
