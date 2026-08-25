#include "SessionCommands.hpp"
#include "Config.hpp"
#include "SessionAttachment.hpp"
#include "SessionManager.hpp"

#include <cstdlib>
#include <iomanip>
#include <ios>
#include <iostream>
#include <string>
#include <vector>



namespace {
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
}


/**
 * parseSessionCommand()
 * Converts a session subcommand string into the corresponding SessionCommand value
 */
SessionCommand parseSessionCommand(const std::string& command) {
    if(command == "close") { return SessionCommand::Close; }
    if(command == "list") { return SessionCommand::List; }
    if(command == "global") { return SessionCommand::Global; }
    if(command == "per-terminal") { return SessionCommand::PerTerminal; }

    return SessionCommand::Unknown;
}


/**
 * handleSessionCommand()
 * Handles session listing, closing, and legacy session-mode commands
 * Handles "gptb session <close|list|global|per-terminal>"
 */
int handleSessionCommand(int argc, char* argv[]) {
    // Most session commands take only a subcommand. Commands such as `close`
    // validate their additional arguments inside their own switch case
    if(argc < 3) {
        std::cout << "Usage: gptb session <command>\n";
        return 1;
    }

    const SessionCommand sessionCommand = parseSessionCommand(argv[2]);

    switch(sessionCommand) {
        case SessionCommand::Close:
            if(argc != 4) {
                std::cout << "Usage: gptb session close <session-id>\n";
                return 1;
            }
            return closeSessionCommand(argv[3]);
        case SessionCommand::List:
            if(argc != 3) {
                std::cout << "Usage: gptb session list\n";
                return 1;
            }
            return listSessionCommand();
        case SessionCommand::Global:
            if(argc != 3) {
                std::cout << "Usage: gptb session global\n";
                return 1;
            }
            setSessionMode(SessionMode::Global);
            break;
        case SessionCommand::PerTerminal:
            if(argc != 3) {
                std::cout << "Usage: gptb session per-terminal\n";
                return 1;
            }
            setSessionMode(SessionMode::PerTerminal);
            break;
        case SessionCommand::Unknown:
            std::cout << "Unknown session command: " << argv[2] << '\n';
            std::cout << "Usage: gptb session <command>\n";
            return 1;
    }

    std::cout << "Session mode: " << sessionModeToString(getSessionMode()) << '\n';
    return 0;
}
