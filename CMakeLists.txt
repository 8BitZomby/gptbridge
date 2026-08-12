cmake_minimum_required(VERSION 3.20)

project(gptbridge LANGUAGES CXX)

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

add_executable(gptb
    src/CaptureCoordinator.cpp
    src/Config.cpp
    src/CommandLine.cpp
    src/InteractionHistory.cpp
    src/main.cpp
    src/ProjectManager.cpp
    src/PtyCaptureBackend.cpp
    src/Storage.cpp
    src/SessionManager.cpp
    src/SessionNonce.cpp
)

target_include_directories(gptb PRIVATE include)
target_link_libraries(gptb PRIVATE
    nlohmann_json::nlohmann_json
    "-framework Security"
)
