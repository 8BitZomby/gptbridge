#include "StatusCommands.hpp"

#include "PersistentSessionStorage.hpp"
#include "ProjectManager.hpp"
#include "SessionManager.hpp"

#include <filesystem>
#include <iostream>
#include <optional>
#include <string>


/**
 * handleStatusCommand()
 * Displays the logical session and project currently attached to this shell
 */
int handleStatusCommand(int argc, char* argv[]) {
    // "gptb status" does not accept additional arguments.
    if(argc != 2) {
        std::cout << "Usage: gptb status\n";
        return 1;
    }

    std::cout << "gptbridge status\n";

    // A normal terminal outside a managed gptbridge shell has no current
    // logical session to report
    const std::optional<std::string> sessionId =  getCurrentSessionId();

    if(!sessionId.has_value()) {
        std::cout << "No session attached\n";
        return 0;
    }

    std::cout << "Session: " << *sessionId << '\n';

    // Read persistent state directly from the attached logical session
    const PersistentSessionStorage sessionStorage = PersistentSessionStorage::forExplicitSessionId(*sessionId);
    const std::string activeProject = getActiveProject(sessionStorage);

    if(activeProject.empty()) {
        std::cout << "Project: none\n";
        return 0;
    }

    // Session state may reference a project that has since been removed
    if(!projectExists(activeProject)) {
        std::cout << "Project: " << activeProject << " (not registered)\n";
        return 0;
    }

    const std::filesystem::path activeProjectPath = getProjectPath(activeProject);

    std::cout << "Project: " << activeProject << '\n';
    std::cout << "Project path: " << activeProjectPath.string() << '\n';

    return 0;
}
