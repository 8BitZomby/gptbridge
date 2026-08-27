#include "AskCommand.hpp"

#include "ExecutablePath.hpp"
#include "SessionManager.hpp"

#include <cerrno>
#include <cstdlib>
#include <filesystem>
#include <iostream>
#include <nlohmann/json.hpp>
#include <optional>
#include <signal.h>
#include <stdexcept>
#include <string>
#include <sys/wait.h>
#include <unistd.h>


namespace {

    /**
     * buildQuestion()
     * Reconstructs the question passed after `gptb ask`.
     */
    std::string buildQuestion(int argc, char* argv[]) {
        std::string question;

        // Join every argument after `ask` into one prompt.
        for(int index = 2; index < argc; ++index) {
            // Restore the spaces removed by shell argument parsing.
            if(!question.empty()) {
                question += ' ';
            }

            question += argv[index];
        }

        return question;
    }


    /**
     * findClaudeOnPath()
     * Searches PATH for an executable Claude Code binary.
     */
    std::optional<std::filesystem::path> findClaudeOnPath() {
        // Read the current PATH used to resolve normal CLI installations.
        const char* pathEnvironment = std::getenv("PATH");

        // Without PATH, allow Claude Desktop discovery to handle resolution.
        if(pathEnvironment == nullptr || *pathEnvironment == '\0') {
            return std::nullopt;
        }

        const std::string pathValue = pathEnvironment;
        std::size_t start = 0;

        // Check each colon-separated PATH entry in order.
        while(start <= pathValue.size()) {
            const std::size_t separator = pathValue.find(':', start);

            // Extract the directory represented by this PATH entry.
            const std::string directoryText =
                pathValue.substr(
                    start,
                    separator == std::string::npos
                        ? std::string::npos
                        : separator - start
                );

            // An empty PATH entry represents the current working directory.
            const std::filesystem::path directory =
                directoryText.empty()
                    ? std::filesystem::current_path()
                    : std::filesystem::path(directoryText);

            // Claude Code normally exposes an executable named `claude`.
            const std::filesystem::path candidate =
                directory / "claude";

            std::error_code fileError;

            // Verify that the candidate exists as a normal file.
            const bool regularFile =
                std::filesystem::is_regular_file(
                    candidate,
                    fileError
                );

            // Require both a valid file and execute permission.
            if(!fileError &&
               regularFile &&
               ::access(candidate.c_str(), X_OK) == 0) {

                // Return an absolute canonical path for child-process execution.
                return std::filesystem::canonical(candidate);
            }

            // Stop once the final PATH entry has been checked.
            if(separator == std::string::npos) {
                break;
            }

            // Advance to the next PATH entry.
            start = separator + 1;
        }

        return std::nullopt;
    }


    /**
     * findBundledClaudeCode()
     * Finds Claude Code when it is bundled with Claude Desktop.
     */
    std::optional<std::filesystem::path> findBundledClaudeCode() {
        // Claude Desktop stores its runtime beneath the user's home directory.
        const char* home = std::getenv("HOME");

        if(home == nullptr || *home == '\0') {
            return std::nullopt;
        }

        // Claude Desktop keeps versioned Claude Code runtimes here on macOS.
        const std::filesystem::path claudeCodeDirectory =
            std::filesystem::path(home) /
            "Library" /
            "Application Support" /
            "Claude" /
            "claude-code";

        std::error_code directoryError;

        // A missing runtime directory simply means this fallback is unavailable.
        if(!std::filesystem::is_directory(
                claudeCodeDirectory,
                directoryError
            ) ||
            directoryError) {

            return std::nullopt;
        }

        std::optional<std::filesystem::path> selectedExecutable;
        std::filesystem::file_time_type selectedWriteTime{};

        // Iterate over every installed version directory.
        std::filesystem::directory_iterator iterator(
            claudeCodeDirectory,
            directoryError
        );

        if(directoryError) {
            return std::nullopt;
        }

        for(const auto& entry : iterator) {
            // Each version uses the same application-bundle executable path.
            const std::filesystem::path candidate =
                entry.path() /
                "claude.app" /
                "Contents" /
                "MacOS" /
                "claude";

            std::error_code fileError;

            // Ignore entries that do not contain an executable Claude binary.
            if(!std::filesystem::is_regular_file(candidate, fileError) ||
               fileError ||
               ::access(candidate.c_str(), X_OK) != 0) {

                continue;
            }

            // Use modification time to choose between installed runtimes.
            const std::filesystem::file_time_type writeTime =
                std::filesystem::last_write_time(
                    candidate,
                    fileError
                );

            if(fileError) {
                continue;
            }

            // Keep the newest executable discovered so updates do not require
            // hard-coding a particular Claude Code version.
            if(!selectedExecutable.has_value() ||
               writeTime > selectedWriteTime) {

                selectedExecutable = candidate;
                selectedWriteTime = writeTime;
            }
        }

        // No usable bundled Claude runtime was found.
        if(!selectedExecutable.has_value()) {
            return std::nullopt;
        }

        // Resolve the selected executable to a stable absolute path.
        return std::filesystem::canonical(
            selectedExecutable.value()
        );
    }


    /**
     * findClaudeCode()
     * Resolves the Claude Code executable used by `gptb ask`.
     */
    std::optional<std::filesystem::path> findClaudeCode() {
        // Prefer a normal Claude Code installation configured through PATH.
        const std::optional<std::filesystem::path> pathExecutable =
            findClaudeOnPath();

        if(pathExecutable.has_value()) {
            return pathExecutable;
        }

        // Fall back to the runtime bundled with Claude Desktop.
        return findBundledClaudeCode();
    }


    /**
     * buildMcpConfiguration()
     * Builds the temporary MCP configuration passed to Claude Code.
     */
    std::string buildMcpConfiguration(
            const std::filesystem::path& gptbExecutable,
            const std::string& sessionId) {

        // Point Claude at this exact gptb binary and current logical session.
        const nlohmann::json configuration = {
            {"mcpServers", {
                {"gptbridge", {
                    {"type", "stdio"},
                    {"command", gptbExecutable.string()},
                    {"args", nlohmann::json::array({"mcp-server"})},
                    {"env", {
                        {"GPTB_MCP_SESSION_ID", sessionId}
                    }}
                }}
            }}
        };

        // Claude accepts the MCP configuration directly as a JSON string.
        return configuration.dump();
    }


    /**
     * writeAll()
     * Writes the complete prompt to Claude's stdin pipe.
     */
    void writeAll(int descriptor, const std::string& contents) {
        std::size_t totalBytesWritten = 0;

        // write() may transfer only part of the supplied buffer.
        while(totalBytesWritten < contents.size()) {
            const ssize_t bytesWritten =
                ::write(
                    descriptor,
                    contents.data() + totalBytesWritten,
                    contents.size() - totalBytesWritten
                );

            // Retry writes interrupted before completion by a signal.
            if(bytesWritten == -1 && errno == EINTR) {
                continue;
            }

            // Any other error prevents the complete prompt from being sent.
            if(bytesWritten <= 0) {
                throw std::runtime_error(
                    "Failed to send question to Claude Code"
                );
            }

            // Continue from the first byte that has not yet been transferred.
            totalBytesWritten +=
                static_cast<std::size_t>(bytesWritten);
        }
    }


    /**
     * waitForClaude()
     * Waits for Claude Code and returns an equivalent CLI exit code.
     */
    int waitForClaude(pid_t childPid) {
        int childStatus = 0;

        // waitpid() can also be interrupted and should then be retried.
        while(::waitpid(childPid, &childStatus, 0) == -1) {
            if(errno == EINTR) {
                continue;
            }

            throw std::runtime_error(
                "Failed to wait for Claude Code"
            );
        }

        // Preserve Claude's normal exit code.
        if(WIFEXITED(childStatus)) {
            return WEXITSTATUS(childStatus);
        }

        // Match normal shell behavior when Claude terminates from a signal.
        if(WIFSIGNALED(childStatus)) {
            return 128 + WTERMSIG(childStatus);
        }

        // Treat any unexpected process state as failure.
        return 1;
    }


    /**
     * runClaudeAsk()
     * Launches Claude Code and sends one question through stdin.
     */
    int runClaudeAsk(
            const std::filesystem::path& claudeExecutable,
            const std::string& question,
            const std::string& mcpConfiguration) {

        // Create a pipe from the gptb parent to Claude's stdin.
        int inputPipe[2];

        if(::pipe(inputPipe) == -1) {
            throw std::runtime_error(
                "Failed to create Claude Code input pipe"
            );
        }

        // Fork so the child can be replaced with Claude Code.
        const pid_t childPid = ::fork();

        if(childPid == -1) {
            // Neither pipe end is useful after a failed fork.
            ::close(inputPipe[0]);
            ::close(inputPipe[1]);

            throw std::runtime_error(
                "Failed to launch Claude Code"
            );
        }

        if(childPid == 0) {
            // Claude only needs the read side of the input pipe.
            ::close(inputPipe[1]);

            // Replace the child's stdin with the pipe.
            if(::dup2(inputPipe[0], STDIN_FILENO) == -1) {
                _exit(127);
            }

            // The original descriptor is unnecessary after dup2().
            ::close(inputPipe[0]);

            // Start outside the active project so Claude cannot discover
            // project files independently of gptbridge's MCP restrictions.
            if(::chdir("/") == -1) {
                _exit(127);
            }

            const std::string claudePath =
                claudeExecutable.string();

            // Run one non-interactive request using only gptbridge MCP access.
            ::execl(
                claudePath.c_str(),
                claudePath.c_str(),
                "-p",
                "--mcp-config",
                mcpConfiguration.c_str(),
                "--strict-mcp-config",
                "--no-session-persistence",
                "--tools",
                "",
                "--allowedTools",
                "mcp__gptbridge__get_active_project",
                "mcp__gptbridge__get_terminal_context",
                "mcp__gptbridge__list_project_files",
                "mcp__gptbridge__search_project_files",
                "mcp__gptbridge__read_project_file",
                static_cast<char*>(nullptr)
            );

            // execl() returns only when Claude could not be started.
            _exit(127);
        }

        // The parent only writes, so close its copy of the read end.
        ::close(inputPipe[0]);

        struct sigaction ignoredPipeSignal{};
        struct sigaction previousPipeSignal{};

        // Ignore SIGPIPE while writing so a closed Claude pipe becomes EPIPE.
        ignoredPipeSignal.sa_handler = SIG_IGN;
        sigemptyset(&ignoredPipeSignal.sa_mask);

        if(::sigaction(
                SIGPIPE,
                &ignoredPipeSignal,
                &previousPipeSignal
            ) == -1) {

            // Close the remaining pipe descriptor before cleanup.
            ::close(inputPipe[1]);

            // Stop and reap the child before reporting the setup failure.
            ::kill(childPid, SIGTERM);
            waitForClaude(childPid);

            throw std::runtime_error(
                "Failed to configure Claude Code input handling"
            );
        }

        try {
            // Send the prompt exactly as stdin text, followed by a newline.
            writeAll(
                inputPipe[1],
                question + '\n'
            );

            // EOF tells Claude that the complete prompt has been delivered.
            ::close(inputPipe[1]);

            // Restore the process's original SIGPIPE behavior.
            ::sigaction(
                SIGPIPE,
                &previousPipeSignal,
                nullptr
            );
        }
        catch(...) {
            // Ensure the pipe is closed if writing fails partway through.
            ::close(inputPipe[1]);

            // Restore SIGPIPE before leaving this command.
            ::sigaction(
                SIGPIPE,
                &previousPipeSignal,
                nullptr
            );

            // Reap the Claude child so a failed request cannot leave a zombie.
            waitForClaude(childPid);

            throw;
        }

        // Return Claude's final process result to the caller.
        return waitForClaude(childPid);
    }

}


/**
 * handleAskCommand()
 * Handles `gptb ask <question...>`.
 */
int handleAskCommand(int argc, char* argv[]) {
    // A question must follow the ask command.
    if(argc < 3) {
        std::cout << "Usage: gptb ask <question...>\n";
        return 1;
    }

    // Reconstruct the complete question from the remaining CLI arguments.
    const std::string question =
        buildQuestion(argc, argv);

    if(question.empty()) {
        std::cout << "Usage: gptb ask <question...>\n";
        return 1;
    }

    // Resolve the logical session that owns the current pushed context.
    const std::optional<std::string> sessionId =
        getCurrentSessionId();

    if(!sessionId.has_value()) {
        std::cout
            << "gptb ask requires an active gptbridge terminal session\n";
        return 1;
    }

    // Resolve either a normal Claude installation or Claude Desktop runtime.
    const std::optional<std::filesystem::path> claudeExecutable =
        findClaudeCode();

    if(!claudeExecutable.has_value()) {
        std::cout
            << "Claude Code could not be found\n"
            << "Install Claude Code or make the `claude` executable available on PATH\n";
        return 1;
    }

    // Use this exact gptb executable for Claude's MCP server.
    const std::filesystem::path gptbExecutable =
        getExecutablePath();

    // Pin the MCP server to the same session that issued this command.
    const std::string mcpConfiguration =
        buildMcpConfiguration(
            gptbExecutable,
            sessionId.value()
        );

    // Run the one-shot Claude request and propagate its result.
    return runClaudeAsk(
        claudeExecutable.value(),
        question,
        mcpConfiguration
    );
}
