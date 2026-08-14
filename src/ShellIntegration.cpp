#include "EmbeddedZshIntegration.hpp"
#include "ShellIntegration.hpp"

#include <stdexcept>
#include <string_view>

std::string generateShellInit(std::string_view shellName) {
    if(shellName == "zsh") {
        // Return the exact zsh integration script embedded into the executable
        // at build time from shell/gptbridge.zsh
        return std::string(EmbeddedShellIntegration::zshScript);
    }
    throw std::runtime_error("Unsupported shell for gptbridge integration: " + std::string(shellName));
}
