#include "SessionCommands.hpp"
#include "Config.hpp"
#include "SessionManager.hpp"

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

        // Display the session ID and active project on one line
        for(const SessionInfo& session : sessions) {
            std::cout << session.id << "    ";

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
    if(command == "list") { return SessionCommand::List; }
    if(command == "global") { return SessionCommand::Global; }
    if(command == "per-terminal") { return SessionCommand::PerTerminal; }

    return SessionCommand::Unknown;
}


/**
 * handleSessionCommand()
 * Handles session listing and session-mode commands
 * Handles "gptb session <list|global|per-terminal>"
 */
int handleSessionCommand(int argc, char* argv[]) {
    // "gptb session" requires exactly one supported session subcommand
    if(argc != 3) {
        std::cout << "Usage: gptb session <list|global|per-terminal>\n";
        return 1;
    }

    const SessionCommand sessionCommand = parseSessionCommand(argv[2]);

    switch(sessionCommand) {
        case SessionCommand::List:
            return listSessionCommand();
        case SessionCommand::Global:
            setSessionMode(SessionMode::Global);
            break;
        case SessionCommand::PerTerminal:
            setSessionMode(SessionMode::PerTerminal);
            break;
        case SessionCommand::Unknown:
            std::cout << "Unknown session command: " << argv[2] << '\n';
            return 1;
    }

    std::cout << "Session mode: " << sessionModeToString(getSessionMode()) << '\n';
    return 0;
}
