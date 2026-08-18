#include "ProjectCommands.hpp"
#include "ProjectManager.hpp"
#include "SessionManager.hpp"
#include "ShellCommands.hpp"
#include "Storage.hpp"

#include <cstdlib>
#include <filesystem>
#include <iostream>
#include <string>


namespace {

/**
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
 * handleAddProjectCommand()
 * Registers a project without changing the active project.
 */
int handleAddProjectCommand(int argc, char* argv[]) {
    // "gptb add project <name> <path>" requires the project subcommand.
    if(argc != 5 || std::string(argv[2]) != "project") {
        std::cout << "Usage: gptb add project <name> <path>\n";
        return 1;
    }

    // Normalize the supplied path before validating or storing it.
    const std::filesystem::path projectPath =
        normalizeProjectPath(argv[4]);

    if(!validateProjectPath(projectPath)) {
        return 1;
    }

    // Save the project name and normalized path in the global registry.
    saveProject(argv[3], projectPath);

    std::cout << "Project name: " << argv[3] << '\n';
    std::cout << "Project path: " << projectPath.string() << '\n';

    return 0;
}


/**
 * handleInitProjectCommand()
 * Registers a project and makes it active for the current session.
 */
int handleInitProjectCommand(int argc, char* argv[]) {
    // "gptb init <path> <project-name>" requires a path and project name.
    if(argc != 4) {
        std::cout << "Usage: gptb init <path> <project-name>\n";
        return 1;
    }

    // Normalize the supplied project directory into an absolute path.
    const std::filesystem::path projectPath =
        normalizeProjectPath(argv[2]);

    if(!validateProjectPath(projectPath)) {
        return 1;
    }

    const std::string projectName = argv[3];

    // Register the project in the global project registry.
    saveProject(projectName, projectPath);

    // Make the newly initialized project active for this session.
    setActiveProject(projectName);

    std::cout << "Initialized project: " << projectName << '\n';
    std::cout << "Project path: " << projectPath.string() << '\n';

    // A session nonce means this command is already running inside a
    // managed shell
    const char* sessionNonce = std::getenv("GPTB_SESSION_NONCE");

    if(sessionNonce != nullptr && *sessionNonce != '\0') {
        return 0;
    }

    // Outside a managed shell, initialization immediately enters one
    return runManagedShell();
}


/**
 * handleUseProjectCommand()
 * Selects an existing project for the current session.
 */
int handleUseProjectCommand(int argc, char* argv[]) {
    // "gptb use <project|.>" accepts a saved name or the current directory.
    if(argc != 3) {
        std::cout << "Usage: gptb use <project|.>\n";
        return 1;
    }

    std::string projectName;

    // "." means: find the project registered at the current directory.
    if(std::string(argv[2]) == ".") {
        projectName =
            findProjectByPath(std::filesystem::current_path());

        if(projectName.empty()) {
            std::cout << "No project registered at current directory\n";
            return 1;
        }
    }
    else {
        projectName = argv[2];

        // Named projects must already exist in the registry.
        if(!projectExists(projectName)) {
            std::cout << "Project not found: "
                      << projectName << '\n';
            return 1;
        }
    }

    // Store the selected project in the current session state.
    setActiveProject(projectName);

    std::cout << "Active project: " << projectName << '\n';

    return 0;
}


/**
 * handleRestoreCommand()
 * Re-enters a managed shell using the current or explicitly selected project
 */
int handleRestoreCommand(int argc, char* argv[]) {
    // "gptb restore" uses the current session's saved active project.
    // "gptb restore <project>" selects a registered project before restoring.
    if(argc != 2 && argc != 3) {
        std::cout << "Usage: gptb restore [project]\n";
        return 1;
    }

    // Restore must be started from the user's normal shell. Starting another
    // managed shell inside an existing one would create a nested PTY session.
    const char* sessionNonce = std::getenv("GPTB_SESSION_NONCE");

    if(sessionNonce != nullptr && *sessionNonce != '\0') {
        std::cout << "Already inside a gptbridge managed shell\n";
        return 1;
    }

    std::string projectName;

    if(argc == 3) {
        // An explicitly named project must already exist in the registry
        projectName = argv[2];

        if(!projectExists(projectName)) {
            std::cout << "Project not found: " << projectName << '\n';
            return 1;
        }

        // Make the requested project active for this terminal session
        setActiveProject(projectName);
    }
    else {
        // Without a project arguement, restore the project already saved for
        // this terminal session
        projectName = getActiveProject();

        if(projectName.empty()) {
            std::cout << "No project to restore for this session\n";
            return 1;
        }

        // Session state can outlive a project registry entry, so verify that
        // the saved project is still registered before entering the shell
        if(!projectExists(projectName)) {
            std::cout << "Saved project is no longer registered: " << projectName << '\n';
            return 1;
        }
    }

    std::cout << "Restoring project: " << projectName << '\n';

    return runManagedShell();
}
