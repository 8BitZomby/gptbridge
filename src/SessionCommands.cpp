#include "SessionCommands.hpp"
#include "Config.hpp"
#include "ProjectManager.hpp"
#include "SessionManager.hpp"
#include "Storage.hpp"

#include <filesystem>
#include <iostream>
#include <stdexcept>
#include <string>


/**
 * handleSessionCommand()
 * Changes whether session state is global or terminal-specific.
 */
int handleSessionCommand(int argc, char* argv[]) {
    // "gptb session" requires exactly one session-mode argument.
    if(argc != 3) {
        std::cout << "Usage: gptb session <global|per-terminal>\n";
        return 1;
    }

    const std::string mode = argv[2];

    try {
        // Convert the CLI text into the strongly typed SessionMode value.
        setSessionMode(sessionModeFromString(mode));
    }
    catch(const std::invalid_argument&) {
        std::cout << "Unknown session mode: " << mode << '\n';
        return 1;
    }

    // Confirm the mode that was persisted.
    std::cout << "Session mode: "
              << sessionModeToString(getSessionMode())
              << '\n';

    return 0;
}


/**
 * handleStatusCommand()
 * Displays the current gptbridge storage, session, and active-project state.
 */
int handleStatusCommand(int argc, char* argv[]) {
    // "gptb status" does not accept additional arguments.
    if(argc != 2) {
        std::cout << "Usage: gptb status\n";
        return 1;
    }

    std::cout << "gptbridge status\n";
    std::cout << "Storage root: " << getStorageRoot() << '\n';

    // Resolve the session mode before inspecting mode-specific state.
    const SessionMode sessionMode = getSessionMode();

    std::cout << "Session mode: "
              << sessionModeToString(sessionMode)
              << '\n';

    // A terminal ID only applies when each terminal has separate state.
    if(sessionMode == SessionMode::PerTerminal) {
        std::cout << "Session ID: "
                  << getCurrentSessionId()
                  << '\n';
    }

    // Determine which project is currently selected for this session.
    const std::string activeProject = getActiveProject();

    if(activeProject.empty()) {
        std::cout << "Active project: none\n";
        return 0;
    }

    // Session state may reference a project that has since been removed.
    if(!projectExists(activeProject)) {
        std::cout << "Active project: "
                  << activeProject
                  << " (not registered)\n";
        return 0;
    }

    // Show the saved root path for the active registered project.
    const std::filesystem::path activeProjectPath =
        getProjectPath(activeProject);

    std::cout << "Active project: " << activeProject << '\n';
    std::cout << "Project path: "
              << activeProjectPath.string()
              << '\n';

    return 0;
}
