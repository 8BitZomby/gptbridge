#ifndef GPTB_MCP_SERVER_HPP
#define GPTB_MCP_SERVER_HPP

#include <nlohmann/json.hpp>
#include <string>


/**
 * McpServer
 * Runs gptbridge's read-only MCP server over standard input/output.
 */
class McpServer {
    public:
        /**
        * run()
        * Reads MCP messages from stdin until the client closes the connection
        */
        int run();

    private:
        /**
         * handleMessage()
         * Routes one parsed JSON-RPC message to the appropriate MCP handler
         */
        void handleMessage(const nlohmann::json& message);

        /**
         * handleInitialize()
         * Responds to the MCP initialization request with server metadata and
         * the protocol version supported by gptbridge
         */
        void handleInitialize(const nlohmann::json& message);

        /**
         * handleToolList()
         * Responds to "tools/list" with the read-only tools currently
         * exposed by gptbridge
         */
        void handleToolsList(const nlohmann::json& message);

        /**
         * handleToolsCall()
         * Executes one read-only MCP tool requested by the client
         */
        void handleToolsCall(const nlohmann::json& message);

        /**
         * sendJsonRpcError()
         * Sends a JSON-RPC error response for an invalid request
         */
        void sendJsonRpcError(const nlohmann::json& id, int code, const std::string& message);

        /**
         * sendToolError()
         * Sends an MCP tool result indicating that valid tool execution failed
         */
        void sendToolError(const nlohmann::json& id, const std::string& message);
};


#endif
