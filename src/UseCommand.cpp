#include "UseCommand.hpp"
#include "ProjectManager.hpp"
#include "SessionManager.hpp"

#include <filesystem>
#include <iostream>
#include <string>


/**
 * handleUseCommand()
 * Selects an existing project for the current logical session.
 */
int handleUseCommand(int argc, char* argv[]) {
    // "gptb use <project-name|.>" accepts a saved name or the current directory.
    if(argc != 3) {
        std::cout << "Usage: gptb use <project-name|.>\n";
        return 1;
    }

    std::string projectName;

    // "." means: find the project registered at the current directory.
    if(std::string(argv[2]) == ".") {
        projectName = findProjectByPath(std::filesystem::current_path());

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

    // Store the selected project in the current logical session.
    setActiveProjectForCurrentSession(projectName);

    std::cout << "Active project: " << projectName << '\n';

    return 0;
}
