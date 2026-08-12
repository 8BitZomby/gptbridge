#ifndef GPTB_CONTROL_PROTOCOL_HPP
#define GPTB_CONTROL_PROTOCOL_HPP

#include <filesystem>
#include <string>
#include <string_view>
#include <variant>


/**
 * CommandStartedEvent
 * Represents metadata reported by the shell immediately before a command
 * begins execution.
 */
struct CommandStartedEvent {

    // Unique identifier used to match this start event with its finish event
    std::string interactionId;

    // Exact command text about to be executed by the shell
    std::string command;

    // Working directory in which the command begins execution
    std::filesystem::path workingDirectory;

    // Timestamp recorded immediately before command execution begins
    std::string startedAt;
};


/**
 * CommandFinishedEvent
 * Represents metadata reported by the shell immediately after the active
 * command has completed.
 */
struct CommandFinishedEvent {

    // Unique identifier must match the corresponding CommandStartEvent
    std::string interactionId;

    // Exit status produced by the completed command
    int exitCode;

    // Timestamp recorded immediately after command execution completes
    std::string finishedAt;
};


/**
 * ControlEvent
 * Represents one decoded shell lifecycle event received through gptbridge's
 * private control protocol. A control event is exactly one of the supported
 * event structures.
 * std::variant<A, B> means:
 *     Object contains either an A or B, but never both
 */
using ControlEvent = std::variant <CommandStartedEvent, CommandFinishedEvent>;


/**
 * ControlProtocol
 * Defines the fixed framing values used to distinguish gptbridge control
 * messages from ordinary terminal output in the shared PTY byte stream.
 * When the parser sees framePrefix it will know that the following is
 * control protocol until frame terminator. Each control frame will also
 * include a per-session nonce/session token so only frames belonging to
 * the current capture session are accepted by the parser.
 */
namespace ControlProtocol {
    // Begins a gptbridge control frame. Ther per-session nonce and payload
    // length follow this prefix before the encoded event payload.
    inline constexpr std::string_view framePrefix = "\x1b]GPTB;";

    // Terminates a complete control frame after its length-delimited payload.
    inline constexpr std::string_view frameTerminator = "\x1b\\";
}
#endif
