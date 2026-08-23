#include "ListCommands.hpp"
#include "ProjectManager.hpp"

#include <iostream>
#include <string>
#include <vector>


namespace {

    /**
     * Prints all registered projects.
     */
    int listProjectsCommand() {
        // Load all saved project names and paths.
        const std::vector<ProjectInfo> projects = listProjects();

        if(projects.empty()) {
            std::cout << "No registered projects\n";
            return 0;
        }

        // Display each project name beside its registered root path.
        for(const ProjectInfo& project : projects) {
            std::cout << project.name
                      << "    "
                      << project.path.string()
                      << '\n';
        }

        return 0;
    }
}

/**
 * handleListCommand()
 * Handles listing registered projects
 */
int handleListCommand(int argc, char* argv[]) {
    // "gptb list" requires one supported list type.
    if(argc != 3) {
        std::cout << "Usage: gptb list projects\n";
        return 1;
    }

    const std::string listType = argv[2];

    if(listType == "projects") {
        return listProjectsCommand();
    }

    // Reject unsupported list types rather than silently doing nothing.
    std::cout << "Usage: gptb list projects\n";
    return 1;
}
