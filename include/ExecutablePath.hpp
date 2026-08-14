#ifndef GPTB_EXECUTABLE_PATH_HPP
#define GPTB_EXECUTABLE_PATH_HPP

#include <filesystem>


/**
 * getExecutablePath()
 * Returns the filesystem path of the currently running gptb executable.
 * Kepping executable-path discovery in one utility prevents platform-specific
 * process APIs from leaking into PtyCaptureBackend
 */
std::filesystem::path getExecutablePath();


#endif
