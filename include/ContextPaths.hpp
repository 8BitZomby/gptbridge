#ifndef GPTB_CONTEXT_PATHS_HPP
#define GPTB_CONTEXT_PATHS_HPP

#include <filesystem>


/**
 * getTerminalContextPath()
 * Returns the persisten terminal-context file for the current gptbridge session
 */
std::filesystem::path getTerminalContextPath();


#endif
