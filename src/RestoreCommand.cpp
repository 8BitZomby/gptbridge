#include "RestoreCommand.hpp"
#include "PersistentSessionStorage.hpp"
#include "ProjectManager.hpp"
#include "SessionManager.hpp"
#include "ShellCommands.hpp"

#include <cstdlib>
#include <filesystem>
#include <iostream>
#include <string>


/**
 * restoreSession()
 * Re-enters a managed shell using the explicitly requested logical session or,
 * when no ID is supplied, the most recently used saved session
 */
int restoreSession(const std::optional<std::string>& requestedSessionId) {
    // Restore must be started from the user's normal shall. Starting another
    // managed shell inside an existing one would create a nested PTY session
    const char* sessionNonce = std::getenv("GPTB_SESSION_NONCE");

    if(sessionNonce != nullptr && *sessionNonce != '\0') {
        std::cout << "Already inside a gptbridge managed shell\n";
        return 1;
    }

    std::string sessionId;

    if(requestedSessionId.has_value()) {
        // An explicit request restores exactly the selected logical session
        sessionId = requestedSessionId.value();
    }
    else {
        // Without an explicit ID, restore whichever logical session was used
        // most recently
        sessionId = getMostRecentlyUsedSessionId();

        if(sessionId.empty()) {
            std::cout << "No restorable gptbridge sessions\n";
            return 1;
        }
    }

    // Resolve the requested logical session explicitly so restore always uses
    // its persistent logical identity
    const PersistentSessionStorage sessionStorage = PersistentSessionStorage::forExplicitSessionId(sessionId);

    // A valid restorable session must have completed intitialization and
    // therefore have a persistent state.json file
    if(!std::filesystem::exists(sessionStorage.getSessionStatePath())) {
        std::cout << "Session not found: " << sessionId << '\n';
        return 1;
    }

    // A logical session may exist without an active project
    const std::string projectName = getActiveProject(sessionStorage);

    // If a project is saved, make sure it is still registered before entering
    // the managed shell
    if(!projectName.empty() && !projectExists(projectName)) {
        std::cout << "Saved project is no longer registered: " << projectName << '\n';
        return 1;
    }

    std::cout << "Restoring session: " << sessionId;

    if(!projectName.empty()) {
        std::cout << " (" << projectName << ")";
    }
    std::cout << '\n';

    // This session is about to become active, so record it as recently used
    // before launching the managed PTY
    markSessionUsed(sessionStorage);

    // Re-enter the existing logical session using its persistent identity
    return runManagedShell(sessionId);
}


/**
 * handleRestoreCommand()
 * Re-enters a managed shell using the most recently used or explicitly
 * selected logical session.
 */
int handleRestoreCommand(int argc, char* argv[]) {
    // "gptb restore" restores the most recently used logical session.
    // "gptb restore <session-id>" restores the specified logical session.
    if(argc != 2 && argc != 3) {
        std::cout << "Usage: gptb restore [session-id]\n";
        return 1;
    }

    // "gptb restore [session-id]"
    if(argc == 3) {
        return restoreSession(std::string(argv[2]));
    }

    // "gptb restore"
    return restoreSession(std::nullopt);
}
