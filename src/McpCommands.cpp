#include "McpCommands.hpp"
#include "McpServer.hpp"

#include <iostream>


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
