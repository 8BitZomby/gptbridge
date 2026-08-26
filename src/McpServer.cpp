#include "McpServer.hpp"

#include "GptIgnore.hpp"
#include "McpProjectFilesystem.hpp"
#include "McpState.hpp"
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
     * Resolves the persisten logical session currently selected for MCP access.
     *
     * GPTB_MCP_SESSION_ID remains available as an explicit override. Normal MCP
     * clients instead follow the authoritative active-session pointer maintained
     * in ~/.gptbridge/mcp-state.json
     */
    std::optional<PersistentSessionStorage> tryResolvePersistentSessionStorageForMcp() {
        // MCP runs without a terminal of its own, so its logical session identity
        // must be supplied explicitly by the process environment
        const char* configuredSessionId = std::getenv("GPTB_MCP_SESSION_ID");
        std::optional<std::string> sessionId;

        if(configuredSessionId != nullptr && *configuredSessionId != '\0') {
            // An explicit environment override pins this MCP process to one logical
            // session instead of following gptbridge's globally selected MCP target
            sessionId = std::string(configuredSessionId);
        }
        else {
            try {
                // Normal MCP clients follow the session explicitly selected by
                // gptbridge commands such as init, use, restore, and mcp sync
                sessionId = getMcpActiveSessionId();
            }
            catch(const std::exception& error) {
                // Persistent MCP-state failures should fail the current tool call,
                // not terminate the long-lived stdio MCP server
                std::cerr << "gptb MCP: failed to resolve MCP state: " << error.what() << '\n';
                return std::nullopt;
            }

            if(!sessionId.has_value()) {
                return std::nullopt;
            }
        }

        try {
            // Resolve and validate the selected logical session before any MCP
            // tool reads project state or terminal context from it
            const PersistentSessionStorage sessionStorage = PersistentSessionStorage::forExplicitSessionId(sessionId.value());

            // Reject stale MCP state that points at a logical session which has
            // since been deleted
            if(!std::filesystem::exists(sessionStorage.getSessionStatePath())) {
                std::cerr << "gptb MCP: session does not exist: " << sessionId.value() << '\n';
                return std::nullopt;
            }
            return sessionStorage;
        }
        catch(std::exception& error) {
            // Invalid explicit or persisted session IDs fail the tool call without
            // terminating the MCP server process
            std::cerr << "gptb MCP: invalid session id: " << error.what() << '\n';

            return std::nullopt;
        }
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

        nlohmann::json message;

        try {
            // Parse separately so malformed JSON can receive the required null-ID error
            message = nlohmann::json::parse(line);
        }
        catch(const nlohmann::json::parse_error&) {
            // Invalid JSON cannot provide a usable request ID, so JSON-RPC
            // requires a parse-error response with a null ID
            sendJsonRpcError(nullptr, -32700, "Parse error");
            continue;
        }

        try {
            // Handle one valid JSON message without allowing failures to stop the server
            handleMessage(message);
        }
        catch(const std::exception& error) {
            // Keep the detailed internal failure local to the server process
            std::cerr << "gptb MCP: internal error: " << error.what() << '\n';

            if(!message.contains("id")) {
                continue;
            }

            // Tool execution failures use MCP's tool-level error result
            if(message.contains("method") && message["method"].is_string() && message["method"] == "tools/call") {

                sendToolError(message["id"], "Internal server error");
                continue;
            }

            // Unexpected failures in other JSON-RPC requests use Internal error
            sendJsonRpcError(message["id"], -32603, "Internal error");
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

        // Resolve the registered project root before scanning its contents so all
        // containment checks compare against one canonical filesystem location.
        std::error_code projectPathError;
        const std::filesystem::path projectPath =
            std::filesystem::canonical(
                getProjectPath(activeProject),
                projectPathError
            );

        if(projectPathError) {
            sendToolError(message["id"], "Failed to resolve active project path");
            return;
        }

        // Parse project-local ignore rules once for this complete listing operation
        const GptIgnore ignoreRules(projectPath);

        std::vector<std::string> files;

        std::error_code iteratorError;

        std::filesystem::recursive_directory_iterator itr(
            projectPath,
            std::filesystem::directory_options::skip_permission_denied,
            iteratorError
        );

        const std::filesystem::recursive_directory_iterator end;

        if(iteratorError) {
            sendToolError(message["id"], "Failed to enumerate active project files");
            return;
        }

        while(itr != end) {
            const std::filesystem::path entryPath = itr->path();

            // Preserve the directory entry's own project-relative path rather than
            // resolving symlinks. The resolved target is checked separately below.
            const std::filesystem::path relativePath = entryPath.lexically_relative(projectPath);

            if(!relativePath.empty()) {
                // Query entry type through error-code overloads so a file that disappears
                // during traversal does not abort the complete MCP request.
                std::error_code typeError;
                const bool isDirectory = itr->is_directory(typeError);

                if(!typeError && isDirectory) {
                    const std::string directoryName = entryPath.filename().string();

                    // Do not descend into ignored, sensitive, generated, or cache directories.
                    // File-level visibility checks remain in place as a second layer.
                    if(ignoreRules.isIgnored(relativePath, true) ||
                        isSensitiveProjectPath(relativePath) ||
                        directoryName == "build" ||
                        directoryName == ".cache") {

                        itr.disable_recursion_pending();
                    }
                }

                else if(!typeError) {
                    typeError.clear();
                    const bool isRegularFile =
                        itr->is_regular_file(typeError);

                    if(!typeError && isRegularFile) {
                        // Resolve the file target before exposing it. A file symlink may point
                        // outside the project even though the symlink itself is inside it.
                        std::error_code resolvedPathError;
                        const std::filesystem::path resolvedEntryPath =
                            std::filesystem::weakly_canonical(
                                entryPath,
                                resolvedPathError
                            );

                        if(!resolvedPathError) {
                            std::error_code resolvedRelativeError;
                            const std::filesystem::path resolvedRelativePath =
                                std::filesystem::relative(
                                    resolvedEntryPath,
                                    projectPath,
                                    resolvedRelativeError
                                );

                            // Both the visible alias path and the resolved target must remain
                            // inside project policy. This prevents a symlink from exposing an
                            // external or otherwise hidden file through a harmless-looking name.
                            if(!resolvedRelativeError &&
                               !resolvedRelativePath.empty() &&
                               *resolvedRelativePath.begin() != ".." &&
                               entryPath.filename() != ".DS_Store" &&
                               !ignoreRules.isIgnored(relativePath) &&
                               !ignoreRules.isIgnored(resolvedRelativePath) &&
                               isProjectPathVisible(relativePath) &&
                               isProjectPathVisible(resolvedRelativePath)) {

                                files.push_back(relativePath.generic_string());
                            }
                        }
                    }
                }
            }

            // Advance using the non-throwing overload. If traversal itself can no
            // longer advance, return the entries collected successfully so far rather
            // than risking a repeated entry or terminating the MCP server.
            iteratorError.clear();
            itr.increment(iteratorError);

            if(iteratorError) {
                break;
            }
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

        // Resolve the registered project root through the non-throwing filesystem
        // overload so search containment checks use one canonical location.
        std::error_code projectPathError;
        const std::filesystem::path projectPath =
            std::filesystem::canonical(
                getProjectPath(activeProject),
                projectPathError
            );

        if(projectPathError) {
            sendToolError(message["id"], "Failed to resolve active project path");
            return;
        }

        // Load .gptignore once rather than reopening it for every searched entry
        const GptIgnore ignoreRules(projectPath);

        constexpr std::size_t maxResults = 50;
        constexpr std::uintmax_t maxFileSize = 1024 * 1024;

        std::size_t resultCount = 0;
        std::size_t oversizedFileCount = 0;
        std::ostringstream text;

        // Walk the project recursively while skipping directories that cannot be
        // entered because of filesystem permissions.
        std::error_code iteratorError;

        std::filesystem::recursive_directory_iterator itr(
            projectPath,
            std::filesystem::directory_options::skip_permission_denied,
            iteratorError
        );

        const std::filesystem::recursive_directory_iterator end;

        if(iteratorError) {
            sendToolError(message["id"], "Failed to enumerate active project files");
            return;
        }

        while(itr != end && resultCount < maxResults) {
            const std::filesystem::path entryPath = itr->path();

            // Preserve the entry's alias path for filtering and result presentation.
            // Symlink targets are resolved separately for containment checks below.
            const std::filesystem::path relativePath =
                entryPath.lexically_relative(projectPath);

            if(!relativePath.empty()) {
                std::error_code typeError;
                const bool isDirectory = itr->is_directory(typeError);

                if(!typeError && isDirectory) {
                    const std::string directoryName =
                        entryPath.filename().string();

                    // Never descend into generated/cache/sensitive directories or
                    // directories explicitly excluded by this project's .gptignore.
                    if(directoryName == "build" ||
                       directoryName == ".cache" ||
                       isSensitiveProjectPath(relativePath) ||
                       ignoreRules.isIgnored(relativePath, true)) {

                        itr.disable_recursion_pending();
                    }
                }

                else if(!typeError) {
                    typeError.clear();
                    const bool isRegularFile =
                        itr->is_regular_file(typeError);

                    if(!typeError && isRegularFile) {
                        // Resolve the file target before searching it. A file symlink inside
                        // the project may still resolve to content outside the project root.
                        std::error_code resolvedPathError;
                        const std::filesystem::path resolvedEntryPath =
                            std::filesystem::weakly_canonical(
                                entryPath,
                                resolvedPathError
                            );

                        if(!resolvedPathError) {
                            std::error_code resolvedRelativeError;
                            const std::filesystem::path resolvedRelativePath =
                                std::filesystem::relative(
                                    resolvedEntryPath,
                                    projectPath,
                                    resolvedRelativeError
                                );

                            // Search only when both the alias path and resolved target remain
                            // inside the project and satisfy the shared visibility policies.
                            if(!resolvedRelativeError &&
                               !resolvedRelativePath.empty() &&
                               *resolvedRelativePath.begin() != ".." &&
                               !ignoreRules.isIgnored(relativePath) &&
                               !ignoreRules.isIgnored(resolvedRelativePath) &&
                               isProjectPathVisible(relativePath) &&
                               isProjectPathVisible(resolvedRelativePath)) {

                                std::error_code fileSizeError;
                                const std::uintmax_t fileSize =
                                    std::filesystem::file_size(
                                        resolvedEntryPath,
                                        fileSizeError
                                    );

                                // Skip files whose size cannot be determined. Oversized files are
                                // counted so the caller knows the search did not inspect every file.
                                if(!fileSizeError) {
                                    if(fileSize > maxFileSize) {
                                        ++oversizedFileCount;
                                    }
                                    else {
                                        std::ifstream input(resolvedEntryPath);

                                        // Unreadable files are skipped rather than failing the
                                        // entire search operation.
                                        if(input) {
                                            std::string line;
                                            std::size_t lineNumber = 0;

                                            while(resultCount < maxResults &&
                                                  std::getline(input, line)) {

                                                ++lineNumber;

                                                // Search uses exact case-sensitive matching.
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
                                    }
                                }
                            }
                        }
                    }
                }
            }

            iteratorError.clear();
            itr.increment(iteratorError);

            // If recursive traversal itself can no longer advance, return whatever
            // search results were collected successfully before the filesystem error.
            if(iteratorError) {
                break;
            }
        }

        // Explain when the search found nothing or stopped at the result limit.
        if(resultCount == 0) {
            text << "No matches found.";
        }
        else if(resultCount == maxResults) {
            text << "\nSearch stopped after "
                 << maxResults
                 << " matches.";
        }

        // Report oversized files separately so callers know when the search did not
        // inspect every otherwise-visible project file.
        if(oversizedFileCount > 0) {
            text << "\n"
                 << oversizedFileCount
                 << (oversizedFileCount == 1
                     ? " file was skipped because it exceeds the 1 MiB search limit."
                     : " files were skipped because they exceed the 1 MiB search limit.");
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

        // Execute the filesystem operation independently from MCP transport so the
        // same containment and visibility behavior can be regression-tested directly.
        const McpProjectFileResult fileResult =
            readMcpProjectFile(
                getProjectPath(activeProject),
                requestedPath
            );

        if(!fileResult.success) {
            sendToolError(
                message["id"],
                fileResult.text
            );
            return;
        }

        const std::string& contents = fileResult.text;


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
