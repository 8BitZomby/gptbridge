#include "VersionCommand.hpp"

#include "Version.hpp"

#include <iostream>


/**
 * handleVersionCommand()
 * Prints the semantic version embedded into gptb at build time.
 */
int handleVersionCommand(int argc, char* argv[]) {
    // Version is a top-level option and does not accept additional arguments.
    if(argc != 2) {
        std::cout << "Usage: gptb --version\n";
        return 1;
    }

    std::cout << "gptb " << Version::value << '\n';

    return 0;
}
