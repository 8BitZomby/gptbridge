#include "SessionCommands.hpp"
#include "SessionAttachment.hpp"
#include "SessionManager.hpp"

#include <algorithm>
#include <cstdlib>
#include <iomanip>
#include <ios>
#include <iostream>
#include <string>
#include <vector>



namespace {
    /**
     * closeSessionCommand()
     * Closes every live managed-shell attachment belonging to the requested
     * logical session
     */
    int closeSessionCommand(const std::string& sessionId) {
        // Do not close the managed shell that is currently executing this command.
        // Closing a session from another terminal allows the target PTY parent to
        // unwind normally and restore its terminal state
        const char* currentSessionId = std::getenv("GPTB_SESSION_ID");

        if(currentSessionId != nullptr && sessionId == currentSessionId) {
            std::cout << "Cannot close the current session from inside itself\n";
            return 1;
        }

        const std::size_t closedAttachmentCount = closeSessionAttachments(sessionId);

        if(closedAttachmentCount == 0) {
            std::cout << "Session is not active: " << sessionId << '\n';
            return 1;
        }

        std::cout << "Closed session: " << sessionId << '\n';
        return 0;
    }

    /**
     * deleteSessionCommand()
     * Handles the interactive CLI flow for permanently deleting one logical
     * session. The actual deletion rules and filesystem removal remain owned
     * by SessionManager::deleteSession()
     */
    int deleteSessionCommand(const std::string& sessionId) {
        // Load the saved session metadata so the confirmation prompt can show the
        // project name and reject obviously active session before asking the user
        const std::vector<SessionInfo> sessions = listSessions();

        const auto sessionItr = std::find_if(
            sessions.begin(),
            sessions.end(),
            [&sessionId](const SessionInfo& session) {

                return session.id == sessionId;
            }
        );

        // Do not prompt for a session that is not part of the saved session registry
        if(sessionItr == sessions.end()) {
            std::cout << "Session does not exist: " << sessionId << '\n';
            return 1;
        }

        // Deletion is intentionally separate from closing. Active sessions must be
        // closed first so their managed shalls can shut down and clean up normally
        if(sessionItr->active) {
            std::cout << "Session is active. Close it before deleting:" << sessionId << '\n';
            return 1;
        }

        // Include the associated project name when one exists so the user can
        // clearly identify which persistent session data is about to be removed
        std::cout << "Delete session: " << sessionId;
        if(!sessionItr->activeProject.empty()) {
            std::cout << " (" << sessionItr->activeProject << ")";
        }
        std::cout << "?\n"
                  << "This will permanently delete its saved session data and cannot be undone. [y/N]: ";
        std::string response;
        std::getline(std::cin, response);

        // Only an explicit y/Y confirms deletion. Empty input and every other
        // response safely cancels the operation
        if(response != "y" && response != "Y") {
            std::cout << "Session deletion cancelled\n";
            return 0;
        }

        // Delegate the actual validation and filesystem deletion to SessionManager
        deleteSession(sessionId);

        std::cout << "Deleted session: " << sessionId << '\n';
        return 0;
    }

    /**
     * listSessionCommand()
     * Prints all saved gptbridge sessions
     */
    int listSessionCommand() {
        // Load each saved session and its current active project
        const std::vector<SessionInfo> sessions = listSessions();

        if(sessions.empty()) {
            std::cout << "No saved sessions\n";
            return 0;
        }

        // Display the session ID, runtime state, and active project on one line
        for(const SessionInfo& session : sessions) {
            // Use fixed-width columns so IDs, runtime state, and project names line up
            // consistently even when individual values have different lengths
            std::cout << std::left
                      << std::setw(12) << session.id
                      << std::setw(12) << (session.active ? "active" : "inactive");

            if(session.activeProject.empty()) {
                std::cout << "none";
            }
            else {
                std::cout << session.activeProject;
            }
            std::cout << '\n';
        }
        return 0;
    }
}


/**
 * parseSessionCommand()
 * Converts a session subcommand string into the corresponding SessionCommand value
 */
SessionCommand parseSessionCommand(const std::string& command) {
    if(command == "close") { return SessionCommand::Close; }
    if(command == "delete") { return SessionCommand::Delete; }
    if(command == "list") { return SessionCommand::List; }

    return SessionCommand::Unknown;
}


/**
 * handleSessionCommand()
 * Validates and dispatches logical-session management commands
 */
int handleSessionCommand(int argc, char* argv[]) {
    // "gptb session" requires at least one subcommand
    if(argc < 3) {
        std::cout << "Usage: gptb session <close|delete|list>\n";
        return 1;
    }

    // Conver the requested subcommand once before dispatching it
    const SessionCommand sessionCommand = parseSessionCommand(argv[2]);

    switch(sessionCommand) {
        case SessionCommand::Close:
            // Closing a session requires the logical session ID to target
            if(argc != 4) {
                std::cout << "Usage: gptb session close <session-id>\n";
                return 1;
            }
            return closeSessionCommand(argv[3]);
        case SessionCommand::Delete:
            // Deleting a session requires the logical session ID to target
            if(argc != 4) {
                std::cout << "Usage: gptb session delete <session-id>\n";
                return 1;
            }
            return deleteSessionCommand(argv[3]);
        case SessionCommand::List:
            // Listing sessions does not accept any additional arguments
            if(argc != 3) {
                std::cout << "Usage: gptb session list\n";
                return 1;
            }
            return listSessionCommand();
        case SessionCommand::Unknown:
            std::cout << "Unknown session command: " << argv[2] << '\n';
            std::cout << "Usage: gptb session <command>\n";
            return 1;
    }

    // All enum values are handled above. Fallback for compiler and future enums
    return 1;
}
