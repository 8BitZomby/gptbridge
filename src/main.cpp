#include "CommandLine.hpp"
#include "Config.hpp"
#include "ControlFrameEncoder.hpp"
#include "InteractionHistory.hpp"
#include "ProjectManager.hpp"
#include "PtyCaptureBackend.hpp"
#include "SessionManager.hpp"
#include "ShellIntegration.hpp"
#include "Storage.hpp"
#include "TimeUtils.hpp"

#include <cstdlib>
#include <exception>
#include <filesystem>
#include <iostream>
#include <stdexcept>
#include <string>
#include <vector>


int main(int argc, char* argv[]) {

    // argc is number of CL arguments
    // argv contains argument strings -> argv[0] = gptb
    if(argc < 2) {
        std::cout << "Usage: gptb <command>\n";
        return 0;
    }
    // Parse first argument
    const Command command = parseCommand(argv[1]);

    try {
        // Command handler may throw for malformed JSON, filesystem failures,
        // or other storage errors. Handle those at the CLI boundary below.
        switch(command) {

            case Command::Add: {
                // "gptb add project <name> <path>" requires three args after top-level "add"
                if(argc != 5 || std::string(argv[2]) != "project") {
                    std::cout << "Usage: gptb add project <name> <path>\n";
                    return 1;
                }

                // Get the user's path only after confirming the argument exists
                const std::filesystem::path projectPath = normalizeProjectPath(argv[4]);

                // A registered project must point to an existing directory
                if(!std::filesystem::exists(projectPath)) {
                    std::cout << "Project path does not exist: " << projectPath.string() << '\n';
                    return 1;
                }
                if(!std::filesystem::is_directory(projectPath)) {
                    std::cout << "Project path is not a directory: " << projectPath.string() << '\n';
                    return 1;
                }

                // Persist the normalized project path in the global project registry.
                // saveProject() is responsible for ensuring the storage directory exists.
                saveProject(argv[3], projectPath);

                std::cout << "Project name: " << argv[3] << '\n';
                // Convert filesystem path to text for clean CLI output
                std::cout << "Project path: " << projectPath.string() << '\n';
                return 0;
            }

// ---- TEST FEATURE ONLY ---- //
            case Command::Capture: {
                // "gptb capture" launches an interactive shell through the PTY backend
                if(argc != 2) {
                    std::cout << "Usage: gptb capture\n";
                    return 1;
                }
                PtyCaptureBackend backend;
                backend.run();
                return 0;
            }

            case Command::List: {
                // "gptb list <type>" currently supports projects and saved sessions
                if(argc != 3) {
                    std::cout << "Usage: gptb list <projects|sessions>\n";
                    return 1;
                }
                const std::string listType = argv[2];

                if(listType == "projects") {
                    // Load strongly typed project information from ProjectManager
                    const std::vector<ProjectInfo> projects = listProjects();

                    if(projects.empty()) {
                        std::cout << "No registered projects\n";
                        return 0;
                    }

                    // Display each saved project name with its registered root path
                    for(const ProjectInfo& project : projects) {
                        std::cout << project.name << "    " << project.path.string() << '\n';
                    }
                    return 0;
                }

                if(listType == "sessions") {
                    // Load saved per-terminal sessions with their current active project
                    const std::vector<SessionInfo> sessions = listSessions();
                    if(sessions.empty()) {
                        std::cout << "No saved sessions\n";
                        return 0;
                    }
                    // Display the session ID and active project on one line
                    for(const SessionInfo& session : sessions) {
                        std::cout << session.id << "    ";
                        if(session.activeProject.empty()) {
                            std::cout << "none";
                        }
                        else {
                            std::cout << session.activeProject;
                        }
                        std::cout << '\n';
                    }
                    return 0;
                }
                std::cout << "Usage: gptb list <projects|sessions>\n";
                return 1;
            }

            case Command::Session: {
                // "gptb session <mode>" changes how active-project state is shared
                if(argc != 3) {
                    std::cout << "Usage: gptb session <global|per-terminal>\n";
                    return 1;
                }
                const std::string mode = argv[2];

                try {
                    // Convert the CLI text into the strongly typed SessionMode enum
                    setSessionMode(sessionModeFromString(mode));
                } catch(const std::invalid_argument&) {
                    std::cout << "Unknown session mode: " << mode << '\n';
                    return 1;
                }

                std::cout << "Session mode: " << sessionModeToString(getSessionMode()) << '\n';
                return 0;
            }

            case Command::ShellEvent: {
                // "gptb shell-event" is an internal command used by shell integration
                // hooks to report command lifecycle metadata back to the capture process
                if(argc < 3) {
                    std::cerr << "Usage: gptb shell-event <started|finished> ...\n";
                    return 1;
                }

                const std::string eventType = argv[2];

                if(eventType == "started") {
                    // started requires:
                    //   interaction ID, command text, and working directory
                    if(argc != 6) {
                        std::cerr << "Usage: gptb shell-event started "
                                << "<interaction-id> <command> <working-directory>\n";
                        return 1;
                    }
                    // Build the strongly typed event that represents the command start
                    CommandStartedEvent event{
                        .interactionId = argv[3],
                        .command = argv[4],
                        .workingDirectory = argv[5],
                        .startedAt = currentTimestampUtc()
                    };
                    // The child shell receives this nonce from PtyCaptureBackend
                    // shell-event must include the same nonce so the parent parser accepts the frame
                    const char* sessionNonce = std::getenv("GPTB_SESSION_NONCE");
                    if(sessionNonce == nullptr || *sessionNonce == '\0') {
                        std::cerr << "gptb: GPTB_SESSION_NONCE is not set\n";
                        return 1;
                    }

                    // Convert the structured event into the complete framed byte sequence expected
                    // by ControlProtocolParser
                    const std::string frame = ControlFrameEncoder::encode(event, sessionNonce);

                    // shell-event writes only the private control frame. Because this command runs
                    // inside the captured shell, stdout travels through the PTY back to the parent,
                    // where ControlProtocolParser removes the frame from visible terminal output.
                    std::cout << frame;
                    std::cout.flush();

                    return 0;
                }
                if(eventType == "finished") {
                    // finished requires:
                    //   interaction ID and exit code
                    if(argc != 5) {
                        std::cerr << "Usage: gptb shell-event finished "
                                << "<interaction-id> <exit-code>\n";
                        return 1;
                    }

                    // Convert the exit-code argument from text into an integer
                    int exitCode = 0;
                    try {
                        // std::stoi() reports the index immediately after the parsed integer.
                        // Requiring that index to reach the end rejects values such as "12junk".
                        std::size_t parsedLength = 0;
                        const std::string exitCodeText = argv[4];
                        exitCode = std::stoi(exitCodeText, &parsedLength);
                        if(parsedLength != exitCodeText.size()) {
                            std::cerr << "gptb: invalid shell-event exit code\n";
                            return 1;
                        }
                    }
                    catch(const std::exception&) {
                        std::cerr << "gptb: invalid shell-event exit code\n";
                        return 1;
                    }

                    // Build the strongly typed event representing command completion
                    CommandFinishedEvent event{
                        .interactionId = argv[3],
                        .exitCode = exitCode,
                        .finishedAt = currentTimestampUtc()
                    };

                    // The frame must carry the same nonce that PtyCaptureBackend supplied
                    // to the captured child shell
                    const char* sessionNonce = std::getenv("GPTB_SESSION_NONCE");
                    if(sessionNonce == nullptr || *sessionNonce == '\0') {
                        std::cerr << "gptb: GPTB_SESSION_NONCE is not set\n";
                        return 1;
                    }

                    // Serialize the finished event and wrap it in the control-protocol framing
                    const std::string frame = ControlFrameEncoder::encode(event, sessionNonce);

                    // stdout travels through the PTY to the parent ControlProtocolParser
                    std::cout << frame;
                    std::cout.flush();

                    return 0;
                }
                std::cerr << "Unknown shell event type: " << eventType << '\n';
                return 1;
            }

            case Command::ShellInit: {
                // "gptb shell-init <shell>" prints initialization code for the requested shell
                if(argc != 3) {
                    std::cout << "Usage: gptb shell-init <shell>";
                    return 1;
                }
                // Generate the shell-specific initialization script and write it to stdout
                // This allows usage such as: eval "$(gptb shell-init zsh)"
                std::cout << generateShellInit(argv[2]);
                return 0;
            }

            case Command::Status: {
                std::cout << "gptbridge status\n";
                std::cout << "Storage root: " << getStorageRoot() << '\n';

                // Resolve the session mode before inspecting mode-specific state
                const SessionMode sessionMode = getSessionMode();

                // Show whether active-project state is shared or terminal-specific
                std::cout << "Session mode: " << sessionModeToString(sessionMode) << '\n';

                // A terminal ID is relevant only when session state is per-terminal
                if(sessionMode == SessionMode::PerTerminal) {
                    std::cout << "Session ID: " << getCurrentSessionId() << '\n';

                }

                // Read the project currently selected for this terminal session
                const std::string activeProject = getActiveProject();

                if(activeProject.empty()) {
                    std::cout << "Active project: none\n";
                }
                else {
                    // A session can outlive a project entry, so verify the saved
                    // active-project name still exists in the global project registry
                    if(!projectExists(activeProject)) {
                        std::cout << "Active project: " << activeProject << " (not registered)\n";
                        return 0;
                    }

                    // Resolve the saved root path only after confirming the project exists
                    const std::filesystem::path activeProjectPath = getProjectPath(activeProject);
                    std::cout << "Active project: " << activeProject << '\n';
                    std::cout << "Project path: " << activeProjectPath.string() << '\n';
                }
                return 0;
            }

            case Command::Use: {
                // "gptb use <project|.>" selects the active registered project for
                // the currently configured session mode
                if(argc != 3) {
                    std::cout << "Usage: gptb use <project|.>\n";
                    return 1;
                }
                std::string projectName = argv[2];

                // "." means: find the project registered at this exact directory
                if(std::string(argv[2]) == ".") {
                    projectName = findProjectByPath(std::filesystem::current_path());
                    if(projectName.empty()) {
                        std::cout << "No project registered at current directory\n";
                        return 1;
                    }
                }
                else {
                    projectName = argv[2];

                    // Named projects must already exist in the global registry
                    if(!projectExists(projectName)) {
                        std::cout << "Project not found: " << projectName << '\n';
                        return 1;
                    }
                }

                // Store the active project in this terminal's session file
                setActiveProject(projectName);

                std::cout << "Active project: " << projectName << '\n';
                return 0;
            }

            case Command::Unknown:
                std::cout << "Unknown command: " << argv[1] << '\n';
                return 1;

            default:
                std::cout << "Command recognized but not implemented yet\n";
                return 0;
        }
    }
    catch(const std::exception& error) {
        // Convert internal failures into readable CLI error
        std::cerr << "gptb: " << error.what() << '\n';
        return 1;
    }
}
