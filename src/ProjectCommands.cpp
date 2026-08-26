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
