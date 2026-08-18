#ifndef GPTB_CONTEXT_PATHS_HPP
#define GPTB_CONTEXT_PATHS_HPP

#include <filesystem>
#include <string>


/**
 * getTerminalContextPath()
 * Returns the persistent terminal-context file for the current gptbridge session
 */
std::filesystem::path getTerminalContextPath();


/**
 * getTerminalContextPathForSession()
 * Returns the persistent terminal-context file for a specified session
 */
std::filesystem::path getTerminalContextPathForSession(const std::string& sessionId);


#endif
