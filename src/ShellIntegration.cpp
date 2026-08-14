#include "ShellIntegration.hpp"

#include <stdexcept>
#include <string_view>

std::string generateShellInit(std::string_view shellName) {
    if(shellName == "zsh") {
        // The zsh implementation will be added next
        return {};
    }
    throw std::runtime_error("Unsupported shell for gptbridge integration: " + std::string(shellName));
}
