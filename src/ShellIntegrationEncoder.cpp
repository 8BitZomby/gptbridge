#include "ShellIntegrationEncoder.hpp"
#include "ControlProtocol.hpp"

#include <array>
#include <stdexcept>
#include <string>
#include <string_view>
#include <unistd.h>


/**
 * encodeExactCommand()
 * Encodes the shell's exact command text and capture-session nonce into one
 * private gptbridge OSC sequence
 *
 * Format:
 *   ESC ] GPTB ; E ; <escaped-command> ; <nonce> ESC \
 */
std::string ShellIntegrationEncoder::encodeExactCommand(std::string_view command, std::string_view sessionNonce) {

    // The nonce identifies control metadata belonging to this capture session.
    // An empty nonce cannot provide that validation
    if(sessionNonce.empty()) {
        throw std::runtime_error("Cannot encode exact command with empty session");
    }

    // The nonce occupies its own semicolon-delimited field. Reject a nonce
    // containing the delimiter rather than producing an ambiguous sequence
    if(sessionNonce.find(ControlProtocol::fieldSeparator) != std::string_view::npos) {
        throw std::runtime_error("Session nonce contains invalid separator");
    }

    // Escape the command before constructing the sequence so command bytes
    // cannot be interpreted as protocol delimiters or control syntax
    const std::string escapedCommand = escapeCommand(command);

    std::string sequence;

    // Reserve the complete expected size so appending the individual protocol
    // fields does not require repeated reallocations
    sequence.reserve(
        ControlProtocol::exactCommandPrefix.size() +
        escapedCommand.size() +
        1 +                                     // Field separator
        sessionNonce.size() +
        ControlProtocol::oscTerminator.size()
    );

    sequence.append(ControlProtocol::exactCommandPrefix);
    sequence.append(escapedCommand);
    sequence.push_back(ControlProtocol::fieldSeparator);
    sequence.append(sessionNonce);
    sequence.append(ControlProtocol::oscTerminator);

    return sequence;
}

/**
 * encodeShellPresentationStart()
 * Encodes the private GPTB marker that identifies the beginning of shell
 * presentation bytes for the current capture session.
 *
 * Format:
 *   ESC ] GPTB ; P ; <nonce> ESC \
 */
std::string ShellIntegrationEncoder::encodeShellPresentationStart(std::string_view sessionNonce) {
    // The private marker must identify the capture session that produced it
    if(sessionNonce.empty()) {
        throw std::runtime_error("Cannot encode shell-presentation marker with empty session");
    }

    // The nonce occupies a semicolon-delimited protocol field, so allowing a
    // separator inside it would make the marker ambiguous to parse
    if(sessionNonce.find(ControlProtocol::fieldSeparator) != std::string_view::npos) {
        throw std::runtime_error("Session nonce contains invalid separator");
    }

    std::string sequence;

    // Reserve storage for the fixed prefix, nonce, and OSC terminator
    sequence.reserve(
            ControlProtocol::shellPresentationPrefix.size() +
            sessionNonce.size() +
            ControlProtocol::oscTerminator.size()
    );

    // Construct the complete private OSC sequence
    sequence.append(ControlProtocol::shellPresentationPrefix);
    sequence.append(sessionNonce);
    sequence.append(ControlProtocol::oscTerminator);

    return sequence;
}


/**
 * encodeCommandOutputStart()
 * Returns the standard OSC 133;C sequence marking the transition into the
 * command-output region
 */
std::string ShellIntegrationEncoder::encodeCommandOutputStart() {
    // commandOutputStart already contains the complete OSC 133;C sequence,
    // including both the OSC introducer and string terminator
    return std::string(ControlProtocol::commandOutputStart);
}


/**
 * encodeCommandFinished()
 * Encodes the standard OSC 133;D sequence with the completed command's
 * exit status appended as its parameter.
 *
 * Format:
 *   ESC ] 133 ; D ; <exit-code> ESC \
 */
std::string ShellIntegrationEncoder::encodeCommandFinished(int exitCode) {
    // Convert the numeric exit status into the decimal text representation
    // required by the OSC 133;D parameter field
    const std::string exitCodeString = std::to_string(exitCode);

    std::string sequence;

    // Reserve storage for fixed prefix, decimal exit status, and OSC terminator
    // to avoid reallocations
    sequence.reserve(
        ControlProtocol::commandFinishedPrefix.size() +
        exitCodeString.size() +
        ControlProtocol::oscTerminator.size()
    );

    // Build the complete OSC sequence in terminal-stream order
    sequence.append(ControlProtocol::commandFinishedPrefix);
    sequence.append(exitCodeString);
    sequence.append(ControlProtocol::oscTerminator);

    return sequence;
}


/**
 * escapeCommand()
 * Escapes bytes that could interfere with the private GPTB OSC syntax.
 * The encoding follows the escaping strategy used for exact-command metadata
 * by VS Code shell integration:
 *
 *   '\'            ->  '\\'
 *   ';'            ->  '\x3b'
 *   bytes <= 0x20  ->  '\xHH'
 *
 * This keeps the encoded command within one unambiguous OSC field while still
 * allowing the parser to reconstruct the shell's exact original command.
 */
std::string ShellIntegrationEncoder::escapeCommand(std::string_view command) {
    std::string escapedCommand;

    // A command may grow when characters require escaping, so reserve at least
    // its original size while allowing std::string to expand when necessary.
    escapedCommand.reserve(command.size());

    for(const unsigned char byte : command) {
        // Backslash introduces our escape syntax and therefore must itself be
        // escaped so a literal backslash survives decoding unambiguously
        if(byte == '\\') {
            escapedCommand.append("\\\\");
        }
        // Semicolon separates fields in the private OSC sequence, so it cannot
        // appear literally inside the encoded command field
        else if(byte == ';') {
            escapedCommand.append("\\x3b");
        }
        // Control characters and spaces are represented as hexadecimal escapes.
        // This includes newlines, tabs, and other bytes <= ASCII 0x20
        else if(byte <= 0x20) {
            constexpr char hexDigits[] = "0123456789abcdef";
            escapedCommand.append("\\x");
            escapedCommand.push_back(hexDigits[byte >> 4]);
            escapedCommand.push_back(hexDigits[byte & 0x0f]);
        }
        else {
            // All other bytes are safe to preserve exactly
            escapedCommand.push_back(static_cast<char>(byte));
        }
    }

    return escapedCommand;
}


/**
 * percentEncodePath()
 * Encodes filesystem-path bytes for use as the path component of an OSC 7
 * file:// URI. Forward slashes remain literal so directory boundaries are
 * preserved. URI-unreserved bytes remain unchanged, while every other byte
 * is represented by its hexadecimal percent encoding
 */
std::string ShellIntegrationEncoder::percentEncodePath(std::string_view path) {
    constexpr char hexDigits[] = "0123456789ABCDEF";

    std::string encodedPath;

    // Percent encoding can expand one input byte into three output characters,
    // so reserve at least the original path size and grow only when necessary
    encodedPath.reserve(path.size());

    for(const unsigned char byte : path) {
        // RFC 3986 unreserved characters can appear directly in a URI without
        // changing their meaning. '/' is also retained here because it separates
        // filesystem path segments inside the OSC 7 file:// URL
        const bool isUnreserved =
            (byte >= 'A' && byte <= 'Z') ||
            (byte >= 'a' && byte <= 'z') ||
            (byte >= '0' && byte <= '9') ||
            byte == '-' ||
            byte == '.' ||
            byte == '_' ||
            byte == '~';

        if(isUnreserved || byte == '/') {
            encodedPath.push_back(static_cast<char>(byte));
            continue;
        }

        // Encode every other byte as %HH so spaces, control bytes, reserved
        // characters, and non-ASCII UTF-8 bytes cannot alter the URI syntax
        encodedPath.push_back('%');
        encodedPath.push_back(hexDigits[byte >> 4]);
        encodedPath.push_back(hexDigits[byte & 0x0f]);
    }

    return encodedPath;
}


/**
 * getLocalHostname()
 * Reads the hostname of the machine running the captured shell. OSC 7 places
 * this value in the authority portion of its file:// URL
 */
std::string ShellIntegrationEncoder::getLocalHostname() {
    // A generously sized buffer avoids depending on a platform-specific
    // compile-time hostname limit while still bounding stack usage
    std::array<char, 256> hostname{};

    // Leave one byte unused so a successful hostname always has room for a
    // terminator that we control rather than relying on gethostname() to add it
    if(gethostname(hostname.data(), hostname.size() - 1) == -1) {
        throw std::runtime_error("Failed to determine hostname for OSC 7");
    }

    // The array was zero-initialized, and gethostname() was given one byte less
    // than its full capacity, so the final byte remains a guaranteed terminator.
    // If every preceding byte was filled, the hostname did not fit completely
    // and must not be used to construct an incorrect OSC 7 URI
    if(hostname[hostname.size() - 2] != '\0') {
        throw std::runtime_error("Hostname exceeds OSC 7 hostname buffer");
    }

    return std::string(hostname.data());
}


/**
 * encodeWorkingDirectory()
 * Encodes the current working directory as a standard OSC 7 file:// URI.
 * The machine hostname forms the URI authority, while the filesystem path is
 * percent-encoded so characters with special URI meaning remain unambiguous.
 *
 * Format:
 *   ESC ] 7 ; file://<hostname><encoded-path> ESC \
 */
std::string ShellIntegrationEncoder::encodeWorkingDirectory(const std::filesystem::path& workingDirectory) {

    // OSC 7 describes the working directory using a file:// URI, so obtain the
    // local hostname for the URI authority and encode the path for the URI syntax
    const std::string hostname = getLocalHostname();
    const std::string encodedPath = percentEncodePath(workingDirectory.string());
    std::string sequence;

    // "file://" is the URI scheme and separator placed between the OSC 7 prefix
    // and the hostname
    constexpr std::string_view fileScheme = "file://";

    // Reserve enough storage for every component of the complete OSC sequence
    // so constructing it does not require unnecessary reallocations
    sequence.reserve(
        ControlProtocol::osc7Prefix.size() +
        fileScheme.size() +
        hostname.size() +
        encodedPath.size() +
        ControlProtocol::oscTerminator.size()
    );

    // Construct the sequence in the exact order required by osc 7:
    // prefix -> file URI -> hostname -> encoded path -> OSC terminator
    sequence.append(ControlProtocol::osc7Prefix);
    sequence.append(fileScheme);
    sequence.append(hostname);
    sequence.append(encodedPath);
    sequence.append(ControlProtocol::oscTerminator);

    return sequence;
}
