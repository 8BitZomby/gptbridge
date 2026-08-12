#ifndef GPTB_CONTROL_PROTOCOL_HPP
#define GPTB_CONTROL_PROTOCOL_HPP

#include <cstddef>
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

    // Fixed byte sequences that mark the beginning and end of a gptbridge
    // control frame inside the shared PTY output stream
    inline constexpr std::string_view framePrefix = "\x1b]GPTB;";
    inline constexpr std::string_view frameTerminator = "\x1b\\";

    // Separates variable-length fields inside the control-frame header
    inline constexpr char fieldSeparator = ';';

    // Prevents malformed or hostile frames from declaring unreasonably large
    // JSON payload and forcing the parser to buffer excessive amounts of data
    inline constexpr std::size_t maxPayloadSize = 1024 * 1024;

    // JSON field names used inside control-frame payloads. Keeping them
    // centralized avoid repeating protocol string literals throughout
    // the parser and shell-size frame generator
    inline constexpr std::string_view typeField = "type";
    inline constexpr std::string_view interactionIdField = "interaction_id";
    inline constexpr std::string_view commandField = "command";
    inline constexpr std::string_view startedAtField = "started_at";
    inline constexpr std::string_view workingDirectoryField = "working_directory";
    inline constexpr std::string_view exitCodeField = "exit_code";
    inline constexpr std::string_view finishedAtField = "finished_at";

    // Values stored in the "type" field to distinguish command lifecycle events
    inline constexpr std::string_view commandStartedType = "command_started";
    inline constexpr std::string_view commandFinishedType = "command_finished";
}
#endif
