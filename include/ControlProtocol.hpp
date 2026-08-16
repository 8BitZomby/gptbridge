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
 * WorkingDirectoryEvent
 * Reports the current working directory decoded from a standard OSC 7
 * shell-integration sequence
 */
struct WorkingDirectoryEvent {

    // Filesystem directory reported by the shell before command execution
    std::filesystem::path workingDirectory;
};


/**
 * ExactCommandEvent
 * Reports the exact command text supplied by the shell through gptbridge's
 * private command-metadata OSC sequence
 */
struct ExactCommandEvent {

    // Exact command text that the shell is about to execute
    std::string command;
};

/**
 * ShellPresentationStartedEvent
 * Marks the point where zsh begins emitting prompt-related presentation bytes
 * that should remain visible in the terminal but should not be stored as
 * command output.
 */
struct ShellPresentationStartedEvent{};


/**
 * CommandOutputStartedEvent
 * Represents the standard OSC 133;C boundary indicating that command
 * execution has entered the command-output region
 */
struct CommandOutputStartedEvent {};


/**
 * CommandOutputFinishedEvent
 * Represents the standard OSC 133;D boundary indicating that the active
 * command has completed
 */
struct CommandOutputFinishedEvent {

    // Exit status carried by the OSC 133;D completion sequence
    int exitCode;
};


/**
 * ControlEvent
 * Represents one decoded shell-integration event. A ControlEvent stores
 * exactly one of the supported semantic event types at a time.
 *
 * std::variant<A, B, C, D, E, F> means:
 *     Object contains either an A, B, C, D, E, or F but never more than one
 */
using ControlEvent = std::variant <
    CommandStartedEvent,
    CommandFinishedEvent,
    WorkingDirectoryEvent,
    ExactCommandEvent,
    ShellPresentationStartedEvent,
    CommandOutputStartedEvent,
    CommandOutputFinishedEvent >;


/**
 * ControlProtocol
 * Defines terminal control-sequence constants used by gptbridge to identify
 * command lifecycle boundaries and metadata within the shared PTY byte stream.
 *
 * The new protocol uses standard OSC sequences where possible:
 *   - OSC 7 for current working directory
 *   - OSC133 for command lifecycle boundaries
 *
 * Legacy gptbridge JSON framing remains temporarily while the parser and shell
 * integration are migrated to the new protocol.
 */
namespace ControlProtocol {

    // ---- Standard Terminal OSC Sequences ---- //

    // OSC (Operating System Command) sequences begin with ESC ] and are
    // terminated with ST (String Terminator), represented by ESC \.
    inline constexpr std::string_view oscIntroducer = "\x1b]";
    inline constexpr std::string_view oscTerminator = "\x1b\\";

    // OSC 7 reports the shell's current working directory as a file:// URL
    inline constexpr std::string_view osc7Prefix = "\x1b]7;";

    // OSC 133 provides semantic shell-integration boundaries within the same
    // ordered PTY stream used for ordinary terminal input and output
    inline constexpr std::string_view osc133Prefix = "\x1b]133;";

    // OSC 133;C marks the transition from command metadata/input into the
    // command-output region. gptbridge will treat this as the authoritative
    // event that begins an active command interaction
    inline constexpr std::string_view commandOutputStart = "\x1b]133;C\x1b\\";

    // OSC 133;D marks command completion. The command's exit status is appended
    // after "D;" before the OSC terminator, so this sequence is constructed
    // dynamically rather than stored as one complete constant
    inline constexpr std::string_view commandFinishedPrefix = "\x1b]133;D;";


    // ---- Private gptbridge Shell Metadata ---- //

    // Private gptbridge OSC sequence carrying the exact command text reported
    // by the shell. "E" identifies exact-command metadata. The encoded command
    // and per-session nonce are appended before the OSC terminator.
    //
    // Format:
    //   Esc ] GPTB ; E ; <escaped-command> ; <nonce> ESC \
    //
    // Command escaping follows the same general strategy used by VS Code's
    // shell integration so protocol separators and control characters cannot
    // be mistaken for framing bytes.
    inline constexpr std::string_view exactCommandPrefix = "\x1b]GPTB;E;";

    // Private gptbridge OSC sequence marking the beginning of shell-generated
    // presentation bytes that should remain visible but should not be persisted
    // as command output.
    //
    // Format:
    //   ESC ] GPTB ; P ; <nonce> ESC \
    //
    inline constexpr std::string_view shellPresentationPrefix = "\x1b]GPTB;P;";


    // ---- Legacy gptbridge Control Framing ---- //

    // Fixed byte sequences that mark the beginning and end of a gptbridge
    // control frame inside the shared PTY output stream
    inline constexpr std::string_view framePrefix = "\x1b]GPTB;";
    inline constexpr std::string_view frameTerminator = oscTerminator;

    // OSC sequences may also be terminated by BEL (0x07). gptbridge emits ST
    // itself, but the parser accepts BEL for compatibility with terminal metadata
    // produced by other shell integrations.
    inline constexpr char oscBellTerminator = '\x07';

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
