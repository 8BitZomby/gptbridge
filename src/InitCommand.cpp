#include "InitCommand.hpp"
#include "PersistentSessionStorage.hpp"
#include "ProjectManager.hpp"
#include "SessionManager.hpp"
#include "Settings.hpp"
#include "ShellCommands.hpp"
#include "Storage.hpp"

#include <cstdlib>
#include <filesystem>
#include <iostream>
#include <string>


namespace {

    /**
     * validateProjectPath()
     * Validates that a project path exists and refers to a directory.
     */
    bool validateProjectPath(const std::filesystem::path& projectPath) {
        // Registered projects must point to an existing filesystem entry.
        if(!std::filesystem::exists(projectPath)) {
            std::cout << "Project path does not exist: "
                      << projectPath.string() << '\n';
            return false;
        }

        // The project root must be a directory rather than a regular file.
        if(!std::filesystem::is_directory(projectPath)) {
            std::cout << "Project path is not a directory: "
                      << projectPath.string() << '\n';
            return false;
        }

        return true;
    }
}


/**
 * handleInitCommand()
 * Registers a project and makes it active for the appropriate logical session.
 */
int handleInitCommand(int argc, char* argv[]) {
    // "gptb init <path> <project-name>" requires a path and project name.
    if(argc != 4) {
        std::cout << "Usage: gptb init <path> <project-name>\n";
        return 1;
    }

    // Ensure global gptbridge settings exist before creating project/session state
    ensureSettingsFile();

    // Normalize the supplied project directory into an absolute path.
    const std::filesystem::path projectPath =
            normalizeProjectPath(argv[2]);

    if(!validateProjectPath(projectPath)) {
        return 1;
    }

    const std::string projectName = argv[3];

    // Register the project independently of any particular logical session.
    saveProject(projectName, projectPath);

    std::cout << "Initialized project: " << projectName << '\n';
    std::cout << "Project path: " << projectPath.string() << '\n';

    // A session nonce means this command is already running inside a managed
    // shell. In that case, keep using the existing logical session rather than
    // creating another session inside it.
    const char* sessionNonce = std::getenv("GPTB_SESSION_NONCE");

    if(sessionNonce != nullptr && *sessionNonce != '\0') {
        // Make the project active in the logical session inherited by this shell.
        setActiveProjectForCurrentSession(projectName);
        return 0;
    }

    // Starting from a normal shell creates a new persistent logical session.
    // The session ID is allocated before the PTY starts so the child shell can
    // inherit a stable identity independent of the terminal device.
    const std::string sessionId = createSession();

    // Store the initialized project in the newly created session explicitly.
    const PersistentSessionStorage sessionStorage =
            PersistentSessionStorage::forExplicitSessionId(sessionId);

    // Make the initialized project active in the new logical session.
    setActiveProject(sessionStorage, projectName);

    // Record the session as recently used before launching its managed shell.
    markSessionUsed(sessionStorage);

    // Launch the managed PTY attached to the new persistent logical session.
    return runManagedShell(sessionId);
}
