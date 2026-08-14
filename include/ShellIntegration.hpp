#ifndef GPTB_SHELL_INTEGRATION_HPP
#define GPTB_SHELL_INTEGRATION_HPP

#include <string>
#include <string_view>


/**
 * generateShellInit()
 * Returns the shell initialization code for a supported shell.
 * The returned code is intended to be evaluated by the user's normal shell
 * during startup, allowing gptbridge to unstall shell-specific lifecycle hooks
 * without replacing or modifying the shell's normal startup process
 */
std::string generateShellInit(std::string_view shellName);


#endif
