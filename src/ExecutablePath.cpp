#include "ExecutablePath.hpp"

#include <filesystem>
#include <mach-o/dyld.h>

#include <stdexcept>
#include <string>
#include <vector>


/**
 * getExecutablePath()
 * Resolves the filesystem path of the currently running executable using
 * macOs's _NSGetExecutablePath() API
 */
std::filesystem::path getExecutablePath() {
    // First ask macOS how much storage is required for the executable path.
    // Passing nullptr intentionally provides no designation buffer; macOS
    // writes the required buffer size into bufferSize instead.
    uint32_t bufferSize = 0;
    _NSGetExecutablePath(nullptr, &bufferSize);

    // Allocate exactly the amount of writable storage requested by mocOS
    std::vector<char> buffer(bufferSize);

    // Call the API again with actual storage for the executable path
    if(_NSGetExecutablePath(buffer.data(), &bufferSize) != 0) {
        throw std::runtime_error("Failed to determine gptn executable path");
    }

    // _NSGetExecutablePath() produces a null terminated C string.
    // Convert it into a filesystem path and normalize it to an absolute,
    // canonical path so the child shell does not depend on the current
    // working directory or the way gptb was originally invoked
    return std::filesystem::canonical(std::filesystem::path(buffer.data()));
}
