#include "ProjectCommands.hpp"
#include "ProjectManager.hpp"
#include "SessionManager.hpp"
#include "Storage.hpp"

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

    return 0;
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
