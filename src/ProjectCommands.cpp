#include "ProjectCommands.hpp"
#include "PersistentSessionStorage.hpp"
#include "ProjectManager.hpp"
#include "SessionManager.hpp"
#include "ShellCommands.hpp"
#include "Storage.hpp"

#include <cstdlib>
#include <filesystem>
#include <iostream>
#include <string>
#include <vector>


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

    /**
     * addProjectCommand()
     * Registers a project in the global project registry without changing
     * the project selected by the current logical session
     */
    int addProjectCommand(int argc, char* argv[]) {
        // "gptb project add <name> <path>" requires a project name and path
        if(argc != 5) {
            std::cout << "Usage: gptb project add <name> <path>\n";
            return 1;
        }

        // Normalize the supplied path before validating or storing it.
        const std::filesystem::path projectPath = normalizeProjectPath(argv[4]);

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
     * listProjectCommand()
     * Prints every project currently stored in the global project registry.
     */
    int listProjectCommand(int argc) {
        // "gptb project list" does not accept additional arguments.
        if(argc != 3) {
            std::cout << "Usage: gptb project list\n";
            return 1;
        }

        const std::vector<ProjectInfo> projects = listProjects();

        if(projects.empty()) {
            std::cout << "No registered projects\n";
            return 0;
        }

        // Display each registered project beside its normalized root path.
        for(const ProjectInfo& project : projects) {
            std::cout << project.name
                      << "    "
                      << project.path.string()
                      << '\n';
        }

        return 0;
    }

    /**
     * removeProjectCommand()
     * Removes a project from the global registry after confirming that no saved
     * logical session still references it.
     */
    int removeProjectCommand(int argc, char* argv[]) {
        // "gptb project remove <name>" requires exactly one project name.
        if(argc != 4) {
            std::cout << "Usage: gptb project remove <name>\n";
            return 1;
        }

        const std::string projectName = argv[3];

        // Refuse unknown names before scanning session state.
        if(!projectExists(projectName)) {
            std::cout << "Project not found: " << projectName << '\n';
            return 1;
        }

        // A registered project cannot be removed while any saved logical session
        // still points to it, because that would leave persistent session state
        // referring to a project that no longer exists in the global registry.
        const std::vector<SessionInfo> sessions = listSessions();

        for(const SessionInfo& session : sessions) {
            if(session.activeProject == projectName) {
                std::cout << "Project is still referenced by session: "
                          << session.id << '\n';
                return 1;
            }
        }

        // Ask before modifying the global project registry.
        std::cout << "Remove project: " << projectName << "?\n";
        std::cout << "This unregisters the project from gptbridge but does not "
                     "delete its files. [y/N]: ";

        std::string confirmation;
        std::getline(std::cin, confirmation);

        if(confirmation != "y" && confirmation != "Y") {
            std::cout << "Project removal cancelled\n";
            return 0;
        }

        // removeProject() returns false only if the project disappeared between
        // validation above and the registry update.
        if(!removeProject(projectName)) {
            std::cout << "Project not found: " << projectName << '\n';
            return 1;
        }

        std::cout << "Removed project: " << projectName << '\n';
        return 0;
    }
}


/**
 * parseProjectCommand()
 * Converts a project subcommand string into the corresponding ProjectCommand
 * value
 */
ProjectCommand parseProjectCommand(const std::string& command) {
    if(command == "add") { return ProjectCommand::Add; }
    if(command == "list") { return ProjectCommand::List; }
    if(command == "remove") { return ProjectCommand::Remove; }

    return ProjectCommand::Unknown;
}


/**
 * handleProjectCommand()
 * Validates and dispatches commands under the `gptb project` namespace.
 */
int handleProjectCommand(int argc, char* argv[]) {
    // "gptb project" requires a project-management subcommand.
    if(argc < 3) {
        std::cout << "Usage: gptb project <add|list|remove>\n";
        return 1;
    }

    const ProjectCommand projectCommand =
            parseProjectCommand(argv[2]);

    switch(projectCommand) {
        case ProjectCommand::Add:
            return addProjectCommand(argc, argv);

        case ProjectCommand::List:
            return listProjectCommand(argc);

        case ProjectCommand::Remove:
            return removeProjectCommand(argc, argv);

        case ProjectCommand::Unknown:
            std::cout << "Unknown project command: " << argv[2] << '\n';
            return 1;
    }

    // All enum values are handled above. Keep a defensive fallback for future
    // additions to ProjectCommand.
    return 1;
}


/**
 * handleInitProjectCommand()
 * Registers a project and makes it active for the appropriate logical session
 */
int handleInitProjectCommand(int argc, char* argv[]) {
    // "gptb init <path> <project-name>" requires a path and project name.
    if(argc != 4) {
        std::cout << "Usage: gptb init <path> <project-name>\n";
        return 1;
    }

    // Normalize the supplied project directory into an absolute path.
    const std::filesystem::path projectPath = normalizeProjectPath(argv[2]);

    if(!validateProjectPath(projectPath)) {
        return 1;
    }

    const std::string projectName = argv[3];

    // Register the project independently of any particular logical session
    saveProject(projectName, projectPath);

    std::cout << "Initialized project: " << projectName << '\n';
    std::cout << "Project path: " << projectPath.string() << '\n';

    // A session nonce means this command is already running inside a managed
    // shell. In that case, keep using the existing logical session rather than
    // creating another session inside it
    const char* sessionNonce = std::getenv("GPTB_SESSION_NONCE");

    if(sessionNonce != nullptr && *sessionNonce != '\0') {
        // Make the project active in the logical session inherited by this shell
        setActiveProjectForCurrentSession(projectName);
        return 0;
    }

    // Starting from a normal shell creates a new persistent logical session.
    // The session ID is allocated before the PTY starts so the child shell can
    // inherit a stable identity that is independent of the terminal device
    const std::string sessionId = createSession();

    // Store the initialized project in the newly created session explicitly.
    // This avoids falling back to the outer terminal's legacy TTY-based identity
    const PersistentSessionStorage sessionStorage = PersistentSessionStorage::forExplicitSessionId(sessionId);

    // Make the initialized project active in the newly created logical session
    setActiveProject(sessionStorage, projectName);

    // This session is about to become the active managed session, so record
    // it as the most recently used logical session
    markSessionUsed(sessionStorage);

    // Launch the managed PTY attached to the new persistent logical session
    return runManagedShell(sessionId);
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
    setActiveProjectForCurrentSession(projectName);

    std::cout << "Active project: " << projectName << '\n';

    return 0;
}
