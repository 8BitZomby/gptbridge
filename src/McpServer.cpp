#include "McpServer.hpp"

#include "PersistentSessionStorage.hpp"
#include "ProjectManager.hpp"
#include "ProjectVisibility.hpp"
#include "SensitivePath.hpp"
#include "SessionManager.hpp"
#include "TerminalContext.hpp"
#include "TerminalInteraction.hpp"

#include <algorithm>
#include <cstdlib>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <optional>
#include <sstream>
#include <string>
#include <vector>


namespace {
    /**
     * tryResolvePersistentSessionStorageForMcp()
     * Resolves the persistent gptbridge session assigned to this MCP server.
     *
     * std::nullopt represents the expected failure case where the MCP process
     * was started without a GPTB_MCP_SESSION_ID environment variable.
     */
    std::optional<PersistentSessionStorage> tryResolvePersistentSessionStorageForMcp() {
        // MCP runs without a terminal of its own, so its logical session identity
        // must be supplied explicitly by the process environment
        const char* sessionId = std::getenv("GPTB_MCP_SESSION_ID");

        if(sessionId == nullptr || *sessionId == '\0') {
            std::cerr << "gptb MCP: GPTB_MCP_SESSION_ID is not set\n";
            return std::nullopt;
        }

        // PersistentSessionStorage performs session ID validation and applies the
        // configured Global or PerTerminal storage routing policy in one place
        return PersistentSessionStorage::forExplicitSessionId(sessionId);
    }
}


/**
 * run()
 * Reads newline-delimited JSON-RPC messages from the MCP client
 */
int McpServer::run() {
    std::string line;

    // STDIO MCP send one complete JSON-RPC message per line
    while(std::getline(std::cin, line)) {
        // Ignore empty input rather than trying to parse it as JSON
        if(line.empty()) {
            continue;
        }
        try {
            // Convert the raw JSON text into a structured request
            const nlohmann::json message = nlohmann::json::parse(line);
            handleMessage(message);
        }
        catch(const nlohmann::json::parse_error&) {
            // Invalid JSON cannot provide a usable request ID, so JSON-RPC
            // requires a parse-error response with a null ID
            sendJsonRpcError(nullptr, -32700, "Parse error");
        }
    }

    // EOF means the MCP client close the STDIO connection
    return 0;
}


/**
 * handleMessage()
 * Routes one MCP message based on its JSON-RPC method name
 */
void McpServer::handleMessage(const nlohmann::json& message) {
    // Requests without a method cannot be routed as MCP operations
    if(!message.contains("method") || !message["method"].is_string()) {
        if(message.contains("id")) {
            sendJsonRpcError(message["id"], -32600, "Invalid JSON-RPC request");
        }
        return;
    }

    // Read the method once so the individual routing checks stay simple
    const std::string method = message["method"].get<std::string>();

    // "initialize" begins the MCP connection and expects a JSON-RPC response
    if(method == "initialize") {
        handleInitialize(message);
        return;
    }

    // The client send this notification after initialization completes.
    // Notifications do not carry an ID and must not receive a response
    if(method == "notifications/initialized") {
        return;
    }

    // "tools/list" as the server while tools the model is allowed to call
    if(method == "tools/list") {
        handleToolsList(message);
        return;
    }

    // "tools/call" asks the server to execute one advertised tool
    if(method == "tools/call") {
        handleToolsCall(message);
        return;
    }

    // Unknown notifications require no response, but unknown requests do
    if(message.contains("id")) {
        sendJsonRpcError(message["id"], -32601, "Method not found: " + method);
    }
}


/**
 * handleInitialize()
 * Returns the protocol version and capabilities supported by this server
 */
void McpServer::handleInitialize(const nlohmann::json& message) {
    // JSON-RPC requests must carry an ID so the response can be matched
    // with the request that produced it
    if(!message.contains("id")) {
        return;
    }

    // Describe the MCP features currently supported by gptbridge
    const nlohmann::json result = {
        {"protocolVersion", "2025-11-25"},
        {"capabilities", {
            {"tools", nlohmann::json::object()},
        }},
        {"serverInfo", {
            {"name", "gptbridge"},
            {"version", "0.1.0"}
        }}
    };

    // Wrap the MCP result in a JSON-RPC response using the request's ID
    const nlohmann::json response = {
        {"jsonrpc", "2.0"},
        {"id", message["id"]},
        {"result", result}
    };

    // stdout is reserved for MCP protocol messages
    std::cout << response.dump() << '\n';
    std::cout.flush();
}


/**
 * handleToolsList()
 * Advertises the read-only MCP tools available from gptbridge
 */
void McpServer::handleToolsList(const nlohmann::json& message) {
    // Requests need an ID so the client can match the response
    if(!message.contains("id")) {
        return;
    }

    // Describe each tool using the MCP tool schema
    const nlohmann::json tools = nlohmann::json::array({

        // GET ACTIVE PROJECT
        {
            {"name", "get_active_project"},
            {"description", "Returns the project currently active in gptbridge."},

            // This tool takes no arguments, so its input is an empty object
            {"inputSchema", {
                {"type", "object"},
                {"properties", nlohmann::json::object()},
                {"additionalProperties", false}
            }}
        },

        // GET TERMINAL CONTEXT
        {
            {"name", "get_terminal_context"},
            {"description", "Returns the terminal commands and output explicitly pushed into the gptbridge session context."},

            // This tool reads the MCP server's bound session and takes no arguments
            {"inputSchema", {
                {"type", "object"},
                {"properties", nlohmann::json::object()},
                {"additionalProperties", false}
            }}
        },

        // LIST PROJECT FILES
        {
            {"name", "list_project_files"},
            {"description", "List files in the active gptbridge project."},

            // This tool uses the MCP server's bound session, so it nooed no argument
            {"inputSchema", {
                {"type", "object"},
                {"properties", nlohmann::json::object()},
                {"additionalProperties", false}
            }}
        },

        // SEARCH PROJECT FILES
        {
            {"name", "search_project_files"},
            {"description", "Searches readable files in the active gptbridge project for matching text."},

            // The caller supplies the text to search for inside project files
            {"inputSchema", {
                {"type", "object"},
                {"properties", {
                    {"query", {
                        {"type", "string"},
                        {"description", "Case-sensitive text to search for in project files."}
                    }}
                }},
                {"required", nlohmann::json::array({"query"})},
                {"additionalProperties", false}
            }}
        },

        // READ PROJECT FILE
        {
            {"name", "read_project_file"},
            {"description", "Reads one text file from the active gptbridge project."},

            // The caller supplies a path relative to the active project root
            {"inputSchema", {
                {"type", "object"},
                {"properties", {
                    {"path", {
                        {"type", "string"},
                        {"description", "Project-relative path of the file to read."}
                    }}
                }},
                {"required", nlohmann::json::array({"path"})},
                {"additionalProperties", false}
            }}
        }
    });

    // Put the advertised tools inside the MCP result object
    const nlohmann::json result = {
        {"tools", tools}
    };

    // Wrap the result in the JSON-RPC response expected by the client
    const nlohmann::json response = {
        {"jsonrpc", "2.0"},
        {"id", message["id"]},
        {"result", result}
    };

    // MCP protocol responses are written only to stdout
    std::cout << response.dump() << '\n';
    std::cout.flush();
}


/**
 * handleToolsCall()
 * Executes one read-only MCP tool requested by the client
 */
void McpServer::handleToolsCall(const nlohmann::json& message) {
    // tools/call is a request, so it must include an ID for the response
    if(!message.contains("id")) {
        return;
    }

    // A tool call must identify the requested tool inside params.name
    if(!message.contains("params") ||
        !message["params"].is_object() ||
        !message["params"].contains("name") ||
        !message["params"]["name"].is_string()) {

            sendJsonRpcError(message["id"], -32602, "Invalid tools/call parameters");
            return;
        }

    const std::string toolName = message["params"]["name"].get<std::string>();


    // GET ACTIVE PROJECT
    // get_active_project takes no arguments and only reads current state
    if(toolName == "get_active_project") {

        // Try to determine which persistent gptbridge session storage this MCP server should use
        const std::optional<PersistentSessionStorage> sessionStorage = tryResolvePersistentSessionStorageForMcp();

        if(!sessionStorage.has_value()) {
            sendToolError(message["id"], "MCP session storage could not be resolved");
            return;
        }

        // The optional contains a PersistentSessionStorage object, so access it directly
        const PersistentSessionStorage& persistentSessionStorage = sessionStorage.value();

        // Read the active project from the selected gptbridge session
        const std::string activeProject = getActiveProject(persistentSessionStorage);

        std::string text;

        if(activeProject.empty()) {
            text = "No active project.";
        }
        else if(!projectExists(activeProject)) {
            text = "Active project: " + activeProject + " (not registered)";
        }
        else {
            // Include both the project name and registered root path
            const std::filesystem::path projectPath = getProjectPath(activeProject);

            text = "Active project: " + activeProject +
                   "\nProject path: " + projectPath.string();
        }

        // MCP tool results return their readable output as content blocks
        const nlohmann::json result = {
            {"content", nlohmann::json::array({
                {
                    {"type", "text"},
                    {"text", text}
                }
            })}
        };

        // Wrap the tool result in the matching JSON-RPC response
        const nlohmann::json response = {
            {"jsonrpc", "2.0"},
            {"id", message["id"]},
            {"result", result}
        };

        std::cout << response.dump() << '\n';
        std::cout.flush();
        return;
    }


    // GET TERMINAL CONTEXT
    // get_terminal_context returns terminal interactions explicitly pushed for this session
    if(toolName == "get_terminal_context") {
        // Try to determine which persistent gptbridge session storage this MCP server should use
        const std::optional<PersistentSessionStorage> sessionStorage = tryResolvePersistentSessionStorageForMcp();

        if(!sessionStorage.has_value()) {
            sendToolError(message["id"], "MCP session storage could not be resolved");
            return;
        }

        // The optional contains a PersistentSessionStorage object, so access it directly
        const PersistentSessionStorage& persistentSessionStorage = sessionStorage.value();

        // Load persistent terminal context from the session bound to this MCP server
        TerminalContext terminalContext(persistentSessionStorage);
        const std::vector<TerminalInteraction> interactions = terminalContext.loadAll();

        std::ostringstream text;

        if(interactions.empty()) {
            text << "No terminal context has been pushed for this session.";
        }
        else {
            // Preserve interaction order so the AI sees commands in execution order
            for(std::size_t idx = 0; idx < interactions.size(); ++idx) {
                const TerminalInteraction& interaction = interactions[idx];

                text << "Interaction: " << (idx + 1) << '\n';
                text << "Command: " << interaction.command << '\n';
                text << "Working Directory: " << interaction.workingDirectory.string() << '\n';
                text << "Exit code: " << interaction.exitCode << '\n';
                text << "Started: " << interaction.startedAt << '\n';
                text << "Finished: " << interaction.finishedAt << '\n';
                text << "Output: " << interaction.output << '\n';

                // Separate adjacent interactions without adding extra content
                // after the final record
                if(idx + 1 < interactions.size()) {
                    text << "\n\n";
                }
            }
        }

        // Return the pushed terminal context as a normal MCP text content block
        const nlohmann::json result = {
            {"content", nlohmann::json::array({
                {
                    {"type", "text"},
                    {"text", text.str()}
                }
            })}
        };

        // Match the response to the tools/call request that requested the context
        const nlohmann::json response = {
            {"jsonrpc", "2.0"},
            {"id", message["id"]},
            {"result", result}
        };

        std::cout << response.dump() << '\n';
        std::cout.flush();
        return;
    }


    // LIST PROJECT FILES
    // list_project_files returns the files available in the active project
    if(toolName == "list_project_files") {
        // Try to determine which persistent gptbridge session storage this MCP server should use
        const std::optional<PersistentSessionStorage> sessionStorage = tryResolvePersistentSessionStorageForMcp();

        if(!sessionStorage.has_value()) {
            sendToolError(message["id"], "MCP session storage could not be resolved");
            return;
        }

        // The optional contains a PersistentSessionStorage object, so access it directly
        const PersistentSessionStorage& persistentSessionStorage = sessionStorage.value();

        // Read the active project from the selected gptbridge session
        const std::string activeProject = getActiveProject(persistentSessionStorage);

        if(activeProject.empty() || !projectExists(activeProject)) {
            sendToolError(message["id"], "No valid active gptbridge project");
            return;
        }

        // Resolve the registered project root before scanning its contents
        const std::filesystem::path projectPath = getProjectPath(activeProject);

        std::vector<std::string> files;

        // Walk the project recursively so files in nested source directories
        // are also available to ChatGPT
        for(std::filesystem::recursive_directory_iterator itr(projectPath), end;
            itr != end;
            ++itr) {

            const std::filesystem::path entryPath = itr->path();

            // Skip directories containing repository metadata or generated build
            if(itr->is_directory()) {
                const std::string directoryName = entryPath.filename().string();

                // Skip repository metadata, generated builds, and editor caches
                if(directoryName == ".git" ||
                    directoryName == "build" ||
                    directoryName == ".cache") {

                    itr.disable_recursion_pending();
                }

                continue;
            }

            // Only regular files are useful as readable project content
            if(!itr->is_regular_file()) {
                continue;
            }

            // Ignore macOs filesystem metadata files
            if(entryPath.filename() == ".DS_Store") {
                continue;
            }

            // Return path relative to the project root rather than absolute paths
            const std::filesystem::path relativePath = std::filesystem::relative(entryPath, projectPath);

            // Only expose files allowed by the shared project-visibility policy
            if(!isProjectPathVisible(relativePath)) {
                continue;
            }

            files.push_back(relativePath.generic_string());
        }

        // Keep the file listing stable between requests
        std::sort(files.begin(), files.end());
        std::string text;

        // Put one project-relative file path on each line
        for(const std::string& file : files) {
            text += file + '\n';
        }

        // Return the file listing as an MCP text content block
        const nlohmann::json result = {
            {"content", nlohmann::json::array({
                {
                    {"type", "text"},
                    {"text", text}
                }
            })}
        };

        // Match the response to the tools/call request that produced it
        const nlohmann::json response = {
            {"jsonrpc", "2.0"},
            {"id", message["id"]},
            {"result", result}
        };

        std::cout << response.dump() << '\n';
        std::cout.flush();
        return;
    }

    // SEARCH PROJECT FILES
    // search_project_files find matching text inside readable active-project files
    if(toolName == "search_project_files") {
        // The tool requires a non-empty text query in arguments.query
        if(!message["params"].contains("arguments") ||
           !message["params"]["arguments"].is_object() ||
           !message["params"]["arguments"].contains("query") ||
           !message["params"]["arguments"]["query"].is_string()) {

            sendJsonRpcError(message["id"], -32602, "search_project_files requires a string query");
            return;
        }

        const std::string query = message["params"]["arguments"]["query"].get<std::string>();

        if(query.empty()) {
            sendJsonRpcError(message["id"], -32602, "search_project_files query cannot be empty");
            return;
        }

        // Try to determine which persistent gptbridge session storage this MCP server should use
        const std::optional<PersistentSessionStorage> sessionStorage = tryResolvePersistentSessionStorageForMcp();

        if(!sessionStorage.has_value()) {
            sendToolError(message["id"], "MCP session storage could not be resolved");
            return;
        }

        // The optional contains a PersistentSessionStorage object, so access it directly
        const PersistentSessionStorage& persistentSessionStorage = sessionStorage.value();

        // Read the active project from the selected gptbridge session
        const std::string activeProject = getActiveProject(persistentSessionStorage);

        if(activeProject.empty() || !projectExists(activeProject)) {
            sendToolError(message["id"], "No valid active gptbridge project");
            return;
        }

        const std::filesystem::path projectPath = std::filesystem::canonical(getProjectPath(activeProject));

        constexpr std::size_t maxResults = 50;
        constexpr std::uintmax_t maxFileSize = 1024 * 1024;

        std::size_t resultCount = 0;
        std::ostringstream text;

        // Walk the project recursively until enough matching lines have been found
        for(std::filesystem::recursive_directory_iterator itr(projectPath), end;
                itr != end && resultCount < maxResults;
                ++itr) {

            const std::filesystem::path entryPath = itr->path();
            const std::filesystem::path relativePath = std::filesystem::relative(entryPath, projectPath);

            // Avoid entering generated, repository, cache, or sensitive directories
            if(itr->is_directory()) {
                const std::string directoryName = entryPath.filename().string();

                if(directoryName == "build" || directoryName == ".cache" || isSensitiveProjectPath(relativePath)) {
                    itr.disable_recursion_pending();
                }

                continue;
            }

            // Search only ordinary files
            if(!itr->is_regular_file()) {
                continue;
            }

            // Search only files allowed by the shared project-visibility policy
            if(!isProjectPathVisible(relativePath)) {
                continue;
            }

            // Avoid loading unusually large files into the search path
            if(itr->file_size() > maxFileSize) {
                continue;
            }

            std::ifstream input(entryPath);

            // Unreadable files are skipped rather than failing the entire search
            if(!input) {
                continue;
            }

            std::string line;
            std::size_t lineNumber = 0;

            while(resultCount < maxResults && std::getline(input, line)) {
                ++lineNumber;

                // The initial search omplementatin uses exact case-sensitive matching
                if(line.find(query) == std::string::npos) {
                    continue;
                }

                text << relativePath.generic_string()
                    << ':'
                    << lineNumber
                    << ": "
                    << line
                    << '\n';

                ++resultCount;
            }
        }

        if(resultCount == 0) {
            text << "No matches found.";
        }
        else if(resultCount == maxResults) {
            text << "\nSearch stopped after "
                << maxResults
                << " matches.";
        }

        // Return project-relative matches as one MCP text content block
        const nlohmann::json result = {
            {"content", nlohmann::json::array({
                {
                    {"type", "text"},
                    {"text", text.str()}
                }
            })}
        };

        const nlohmann::json response = {
            {"jsonrpc", "2.0"},
            {"id", message["id"]},
            {"result", result}
        };

        std::cout << response.dump() << '\n';
        std::cout.flush();
        return;
    }


    // READ PROJECT FILE
    // read_project_file returns the contents of one file inside the active project
    if(toolName == "read_project_file") {
        // The tool requires a project-relative path in arguments.path
        if(!message["params"].contains("arguments") ||
                !message["params"]["arguments"].is_object() ||
                !message["params"]["arguments"].contains("path") ||
                !message["params"]["arguments"]["path"].is_string()) {

            sendJsonRpcError(message["id"], -32602, "read_project_file requires a string path");
            return;
        }

        const std::filesystem::path requestedPath = message["params"]["arguments"]["path"].get<std::string>();

        // Absolute paths are not allowed because reads must stay inside the project
        if(requestedPath.is_absolute()) {
            sendToolError(message["id"], "Requested project path must be relative");
            return;
        }

        // Try to determine which persistent gptbridge session storage this MCP server should use
        const std::optional<PersistentSessionStorage> sessionStorage = tryResolvePersistentSessionStorageForMcp();

        if(!sessionStorage.has_value()) {
            sendToolError(message["id"], "MCP session storage could not be resolved");
            return;
        }

        // The optional contains a PersistentSessionStorage object, so access it directly
        const PersistentSessionStorage& persistentSessionStorage = sessionStorage.value();

        // Read the active project from the selected gptbridge session
        const std::string activeProject = getActiveProject(persistentSessionStorage);

        if(activeProject.empty() || !projectExists(activeProject)) {
            sendToolError(message["id"], "No valid active gptbridge project");
            return;
        }

        // Canonical paths let us safely check that the requested file remains
        // inside the registered project, including when symlinks are involved
        const std::filesystem::path projectPath = std::filesystem::canonical(getProjectPath(activeProject));
        const std::filesystem::path filePath = std::filesystem::weakly_canonical(projectPath / requestedPath);

        // Convert the resolved file back into a path relative to the project root
        const std::filesystem::path relativePath = std::filesystem::relative(filePath, projectPath);

        // A path beginning with ".." escaped outside the active project
        if(relativePath.empty() || *relativePath.begin() == "..") {
            sendToolError(message["id"], "Requested project path escapes the active project");
            return;
        }

        // Read only files allowed by the shared project-visibility policy
        if(!isProjectPathVisible(relativePath)) {
            sendToolError(message["id"], "Requested project file is not visible to MCP");
            return;
        }

        // Only existing regular files can be returned as project content
        if(!std::filesystem::exists(filePath) || !std::filesystem::is_regular_file(filePath)) {
            sendToolError(message["id"], "Requested project file does not exist or is not a regular file");
            return;
        }

        std::ifstream input(filePath);

        if(!input) {
            sendToolError(message["id"], "Failed to open requested project file");
            return;
        }

        // Read the complete text file into memory for the MCP response
        std::ostringstream buffer;
        buffer << input.rdbuf();

        const std::string contents = buffer.str();

        // Return the file contents as a normal MCP text content block
        const nlohmann::json result = {
            {"content", nlohmann::json::array({
                {
                    {"type", "text"},
                    {"text", contents}
                }
            })}
        };

        // Match the response to the tools/call request that requested this file
        const nlohmann::json response = {
            {"jsonrpc", "2.0"},
            {"id", message["id"]},
            {"result", result}
        };

        std::cout << response.dump() << '\n';
        std::cout.flush();
        return;
    }

    // Unknown tool error
    sendJsonRpcError(message["id"], -32602, "Unknown MCP tool: " + toolName);
}


/**
 * sendJsonRpcError()
 * Sends a JSON-RPC error response for an invalid request
 */
void McpServer::sendJsonRpcError(const nlohmann::json& id, int code, const std::string& message) {
    const nlohmann::json response = {
        {"jsonrpc", "2.0"},
        {"id", id},
        {"error", {
            {"code", code},
            {"message", message}
        }}
    };

    std::cout << response.dump() << '\n';
    std::cout.flush();
}


/**
 * sendToolError()
 * Sends an MCP tool result indicating that valid tool execution failed
 */
void McpServer::sendToolError(const nlohmann::json& id, const std::string& message) {
    const nlohmann::json result = {
        {"content", nlohmann::json::array({
            {
                {"type", "text"},
                {"text", message}
            }
        })},
        {"isError", true}
    };

    const nlohmann::json response = {
        {"jsonrpc", "2.0"},
        {"id", id},
        {"result", result}
    };

    std::cout << response.dump() << '\n';
    std::cout.flush();
}
