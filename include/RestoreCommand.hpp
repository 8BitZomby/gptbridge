#ifndef GPTB_RESTORE_COMMAND_HPP
#define GPTB_RESTORE_COMMAND_HPP

#include <optional>


/**
 * restoreSession()
 * Restores the specified logical session. When no session ID is supplied,
 * restores the most recently used logical session
 */
int restoreSession(const std::optional<std::string>& requestedSessionId);


/**
 * handleRestoreCommand()
 * Restores the most recently used logical session or a specifically requested
 * saved logical session.
 */
int handleRestoreCommand(int argc, char* argv[]);


#endif
