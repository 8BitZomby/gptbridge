#include "McpCommands.hpp"
#include "McpServer.hpp"
#include "McpState.hpp"
#include "SessionManager.hpp"

#include <iostream>
#include <optional>


namespace {
    /**
     * syncMcpCommand()
     * Makes the logical session attached to the current managed shell the
     * authoritative session exposed through MCP
     */
    int syncMcpCommand(int argc) {
        // "gptb mcp sync" takes no additional arguments
        if(argc != 3) {
            std::cout << "Usage: gptb mcp sync\n";
            return 1;
        }

        // MCP synchronization is meaningful only from a managed shell because
        // the current logical session determines which project/context to espose
        const std::optional<std::string> sessionId = getCurrentSessionId();

        if(!sessionId.has_value()) {
            std::cout << "No logical gptbridge session is attached to the current shell\n";
            return 1;
        }

        // Explicit sync is a recovery operation, so unlike automatic updates,
        // failures are allowed to propagate to main() and produce a real error
        setMcpActiveSessionId(sessionId.value());

        const std::string projectName = getActiveProjectForCurrentSession();

        std::cout << "MCP session: " << sessionId.value() << '\n';

        if(projectName.empty()) {
            std::cout << "MCP project: none\n";
        }
        else {
            std::cout << "MCP project: " << projectName << '\n';
        }

        return 0;
    }
}


/**
 * parseMcpCommand()
 * Converts an MCP subcommand string into the corresponding McpCommand value
 */
McpCommand parseMcpCommand(const std::string& command) {
    if(command == "sync") { return McpCommand::Sync; }

    return McpCommand::Unknown;
}


/**
 * handleMcpCommand()
 * Handles user-facing commands under the `gptb mcp` namespace
 */
int handleMcpCommand(int argc, char* argv[]) {
    // "gptb mcp" requires one supported MCP-management subcommand
    if(argc < 3) {
        std::cout << "Usage: gptb mcp <sync>\n";
        return 1;
    }
    const McpCommand mcpCommand = parseMcpCommand(argv[2]);

    switch(mcpCommand) {
        case McpCommand::Sync:
            return syncMcpCommand(argc);
        case McpCommand::Unknown:
            std::cout << "Unknown MCP command: " << argv[2] << '\n';
            std::cout << "Usage: gptb mcp <sync>\n";
            return 1;
    }

    // All enum values are handled above. Fallback for future additions
    return 1;
}


/**
 * handleMcpServerCommand()
 * Starts the MCP server used by external ChatGPT integration
 */
int handleMcpServerCommand(int argc, char* argv[]) {
    // This is an internal entry point and does not accept user arguments
    if(argc != 2) {
        std::cout << "Usage: gptb mcp-server\n";
        return 1;
    }

    // Start the MCP server and keep this process alive for the client connection
    McpServer server;
    return server.run();

}
