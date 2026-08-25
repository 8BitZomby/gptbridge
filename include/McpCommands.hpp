#ifndef GPTB_MCP_COMMANDS_HPP
#define GPTB_MCP_COMMANDS_HPP

#include <string>


/**
 * McpCommand
 * Identifies a user-facing operation under the `gptb mcp` namespace
 */
enum class McpCommand {
    Sync,       // Synchronizes MCP with the logical session attached to this shell
    Unknown     // Represents an unsupported MCP subcommand
};


/**
 * parseMcpCommand()
 * Converts an MCP subcommand string into the corresponding McpCommand value
 */
McpCommand parseMcpCommand(const std::string& command);


/**
 * handleMcpCommand()
 * Handles user-facing commands under the `gptb mcp` namespace
 */
int handleMcpCommand(int argc, char* argv[]);


/**
 * handleMcpServerCommand()
 * Handles the internal command used to launch gptbridge's MCP server.
 */
int handleMcpServerCommand(int argc, char* argv[]);


#endif
