cmake_minimum_required(VERSION 3.20)

project(gptbridge VERSION 0.2.5 LANGUAGES CXX)

include(FetchContent)
# Fetch the JSON library at configure time so it does not require
# a separate system-wide install when building gptbridge
FetchContent_Declare(
    nlohmann_json
    GIT_REPOSITORY https://github.com/nlohmann/json.git
    GIT_TAG v3.11.3
)
FetchContent_MakeAvailable(nlohmann_json)

set(CMAKE_EXPORT_COMPILE_COMMANDS ON)

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)

include(CTest)

# Re-run CMake configuration when the embedded zsh integration changes so a
# normal build always regenerates the C++ header with the latest script.
set_property(
    DIRECTORY
    APPEND
    PROPERTY CMAKE_CONFIGURE_DEPENDS
    "${CMAKE_CURRENT_SOURCE_DIR}/shell/gptbridge.zsh"
)

# Read the zsh integration script at configure time so it can be embedded
# directly into the gptb executable without requiring the source file at runtime.
file(
    READ
    "${CMAKE_CURRENT_SOURCE_DIR}/shell/gptbridge.zsh"
    GPTB_ZSH_INTEGRATION
)

# Generate a C++ header containing the script as a raw string literal.
# @ONLY ensures only @VARIABLE@ placeholders are substituted by CMake.
configure_file(
    "${CMAKE_CURRENT_SOURCE_DIR}/cmake/EmbeddedZshIntegration.hpp.in"
    "${CMAKE_CURRENT_BINARY_DIR}/generated/EmbeddedZshIntegration.hpp"
    @ONLY
)

# Generate a C++ header containing the semantic version declared by project().
configure_file(
    "${CMAKE_CURRENT_SOURCE_DIR}/cmake/Version.hpp.in"
    "${CMAKE_CURRENT_BINARY_DIR}/generated/Version.hpp"
    @ONLY
)

add_executable(gptb
    src/CaptureCoordinator.cpp
    src/CommandLine.cpp
    src/ContextCommands.cpp
    src/ControlProtocolParser.cpp
    src/ExecutablePath.cpp
    src/GptIgnore.cpp
    src/HelpCommand.cpp
    src/InitCommand.cpp
    src/main.cpp
    src/McpCommands.cpp
    src/McpProjectFilesystem.cpp
    src/McpServer.cpp
    src/McpState.cpp
    src/PersistentSessionStorage.cpp
    src/ProjectManager.cpp
    src/ProjectVisibility.cpp
    src/PtyCaptureBackend.cpp
    src/ProjectCommands.cpp
    src/Random.cpp
    src/RestoreCommand.cpp
    src/SensitivePath.cpp
    src/SessionAttachment.cpp
    src/SessionCommands.cpp
    src/SessionId.cpp
    src/SessionManager.cpp
    src/SessionNonce.cpp
    src/Settings.cpp
    src/ShellCommands.cpp
    src/ShellIntegration.cpp
    src/ShellIntegrationEncoder.cpp
    src/StatusCommands.cpp
    src/Storage.cpp
    src/TemporaryInteractionHistory.cpp
    src/TerminalContext.cpp
    src/TerminalInteractionJson.cpp
    src/TerminalSecretDetector.cpp
    src/TimeUtils.cpp
    src/UseCommand.cpp
    src/VersionCommand.cpp
)

target_include_directories(gptb PRIVATE
    include
    "${CMAKE_CURRENT_BINARY_DIR}/generated"
)

target_link_libraries(gptb PRIVATE
    nlohmann_json::nlohmann_json
    "-framework Security"
)

if(BUILD_TESTING)
    add_executable(mcp_filesystem_policy_tests
        tests/McpFilesystemPolicyTests.cpp
        src/GptIgnore.cpp
        src/McpProjectFilesystem.cpp
        src/ProjectVisibility.cpp
        src/SensitivePath.cpp
    )

    target_include_directories(mcp_filesystem_policy_tests PRIVATE
        include
    )

    add_test(
        NAME mcp_filesystem_policy_tests
        COMMAND mcp_filesystem_policy_tests
    )
endif()
