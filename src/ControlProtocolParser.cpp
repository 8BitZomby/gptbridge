#include "ControlProtocolParser.hpp"
#include "ControlProtocol.hpp"

#include <cstddef>
#include <stdexcept>
#include <string>
#include <string_view>
#include <utility>


/**
 * ControlProtocolParser()
 * Creates a parser for one capture session. The parser stores the session
 * nonce used to validate private gptbridge OSC metadata and the callbacks
 * used to deliver ordinary terminal output and decoded control events.
 */
ControlProtocolParser::ControlProtocolParser(std::string sessionNonce, OutputHandler outputHandler, EventHandler eventHandler) :
    sessionNonce(std::move(sessionNonce)), outputHandler(std::move(outputHandler)), eventHandler(std::move(eventHandler)) {}


/**
 * partialOscIntroducerLength()
 * Returns the number of trailing bytes in pendingBytes that could be
 * the beginning of the OSC intriducer split across PTY reads
 */
std::size_t ControlProtocolParser::partialOscIntroducerLength() const {
    // The OSC introducer is ESC ], so the only incomplete suffix worth
    // preserving is a single trailing ESC byte
    if(!pendingBytes.empty() && pendingBytes.back() == ControlProtocol::oscIntroducer.front()) {
        // Preserve the trailing ESC because the next PTY read may begin with
        // ']', completing the twp-byte OSC introducer
        return 1;
    }

    // No trailing bytes could become the start of an OSC sequence, so nothing
    // needs to be retained solely for introducer matching
    return 0;
}

/**
 * emitOutput()
 * Sends terminal bytes to the configured output handler together with their
 * command-output capture eligibility.
 */
void ControlProtocolParser::emitOutput(std::string_view bytes, bool captureEligible) {
    // An empty byte range does not represent any terminal output, so avoid
    // invoking the callback when there is nothing to deliver.
    if(bytes.empty()) {
        return;
    }

    // The parser classifies the bytes but leaves terminal forwarding and persistent
    // capture policy to the configured output handler.
    outputHandler(
            bytes,          // Exact terminal bytes to forward
            captureEligible // Whether these bytes may be stored as command output
    );
}


/**
 * tryParseOscSequence()
 * Examines one OSC sequence beginning at the start of pendingBytes. Relevant
 * shell-integration sequences are decoded into semantic events, while unrelated
 * OSC sequences remain ordinary terminal data and are forwarded unchanged
 *
 * OSC sequences may terminate with either ST (ESC \) or BEL (0x07). When both
 * forms appear in the buffered data, whichever occurs first terminates the
 * current OSC sequence.
 */
ControlProtocolParser::OscParseResult ControlProtocolParser::tryParseOscSequence() {
    // This helper is called only after consume() has positioned an OSC
    // introducer at index 0
    if(!pendingBytes.starts_with(ControlProtocol::oscIntroducer)) {
        throw std::runtime_error("OSC parser called without OSC introducer");
    }

    // Search independently for both supported OSC terminators:
    //
    //   ST  -> ESC \
    //   BEL -> 0x07
    //
    // Other shell integrations may use either form even though gptbridge emits
    // its own sequences using ST
    const std::size_t stPosition =
        pendingBytes.find(
            ControlProtocol::oscTerminator,
            ControlProtocol::oscIntroducer.size()
        );

    const std::size_t bellPosition =
        pendingBytes.find(
            ControlProtocol::oscBellTerminator,
            ControlProtocol::oscIntroducer.size()
        );

    // If neither terminator has arrived, the OSC sequence may be split across
    // PTY reads. Preserve the complete candidate until more bytes are received
    if(stPosition == std::string::npos && bellPosition == std::string::npos) {
        return OscParseResult::Incomplete;
    }

    // Determine which terminator closes the current OSC sequence. If only one
    // exists, use it. If both exist, the earlier one is the actual terminator
    std::size_t terminatorPosition = 0;
    std::size_t terminatorLength = 0;

    if(bellPosition != std::string::npos && (stPosition == std::string::npos || bellPosition < stPosition)) {
        terminatorPosition = bellPosition;
        terminatorLength = 1;
    }
    else {
        terminatorPosition = stPosition;
        terminatorLength = ControlProtocol::oscTerminator.size();
    }

    // Include the selected terminator in both the sequence view and the number
    // of bytes removed from pendingBytes
    const std::size_t sequenceLength = terminatorPosition + terminatorLength;

    const std::string_view sequence(
        pendingBytes.data(),    // First byte of the OSC introducer
        sequenceLength          // Complete OSC sequence including its terminator
    );

    bool suppressSequence = false;
    bool captureEligible = true;

    if(sequence.starts_with(ControlProtocol::osc7Prefix)) {
        // OSC 7 is standard terminal metadata. Decode it for gptbridge while
        // still forwarding the original sequence to the outer terminal
        parseOsc7(sequence);
        captureEligible = false;
    }
    else if(sequence.starts_with(ControlProtocol::osc133Prefix)) {
        // Only the OSC 133 lifecycle forms understood by gptbridge produce
        // semantic events. Other OSC 133 forms remain valid terminal data
        (void)parseOsc133(sequence);
        captureEligible = false;
    }
    else if(sequence.starts_with(ControlProtocol::exactCommandPrefix)) {
        // Private GPTB command metadata is consumed only when its nonce belongs
        // to this capture session. Foreign sequences are forwarded unchanged
        suppressSequence = parseExactCommand(sequence);
    }
    else if(sequence.starts_with(ControlProtocol::shellPresentationPrefix)) {
        // The private presentation marker identifies the point where shell
        // presentation bytes begin. A validated marker is consumed internally
        // while a marker from another session remains ordinary terminal data
        suppressSequence = parseShellPresentationStart(sequence);
    }
    if(!suppressSequence) {
        // Standard and unrelated OSC sequences remain in the visible terminal
        // stream after any gptbridge-relevant metadata has been observed
        emitOutput(
                sequence,           // Complete OSC sequence
                captureEligible     // False for OSC 7 and OSC 133 metadata
        );
    }

    // Remove the complete OSC sequence after it has either been forwarded or
    // privately consumed
    pendingBytes.erase(
        0,              // OSC sequence begins at the start of pendingBytes
        sequenceLength  // Remove the complete sequence including ST
    );

    return OscParseResult::Consumed;
}


/**
 * percentDecodePath()
 * Reconstructs the filesystem-path bytes represented by an OSC 7 URI path.
 * Each valid %HH escape is converted back into its original byte
 */
std::string ControlProtocolParser::percentDecodePath(std::string_view encodedPath) {
    std::string decodedPath;
    decodedPath.reserve(encodedPath.size());

    // Convert one hexadecimal digit into its numeric value. Returning -1
    // distinguishes an invalid hexadecimal character from the valid value zero
    const auto hexValue = [](char character) -> int {
        if(character >= '0' && character <= '9') {
            return character - '0';
        }
        if(character >= 'A' && character <= 'F') {
            return character - 'A' + 10;
        }
        if(character >= 'a' && character <= 'f') {
            return character - 'a' + 10;
        }
        // The character is not a valid hexadecimal digit
        return -1;
    };

    for(std::size_t idx = 0; idx < encodedPath.size(); ++idx) {
        // Bytes other than '%' are not percent-encoded and can be copied
        // directly into the reconstructed filesystem path
        if(encodedPath[idx] != '%') {
            decodedPath.push_back(encodedPath[idx]);
            continue;
        }

        // A percent escape requires exactly two hex digits after '%'
        if(idx + 2 >= encodedPath.size()) {
            throw std::runtime_error("Incomplete percent encoding in OSC 7 path");
        }

        const int highNibble = hexValue(encodedPath[idx + 1]);
        const int lowNibble = hexValue(encodedPath[idx + 2]);

        // Both characters following '%' must be hex digits
        if(highNibble < 0 || lowNibble < 0) {
            throw std::runtime_error("Invalid percent encoding in OSC 7 path");
        }

        // Combine the two four-bit hex values into the original byte
        const unsigned char decodedByte =
            static_cast<unsigned char>((highNibble << 4) | lowNibble);

        decodedPath.push_back(static_cast<char>(decodedByte));

        // The loop increment handles the '%' byte, so advance by two ir more
        // positions to skip the hex digits that were just decoded
        idx += 2;
    }

    // Return the exact filesystem-path byte sequence reconstructed from the URI
    return decodedPath;
}


/**
 * parseOsc7()
 * Decodes the working-directory file URI carried by a complete OSC 7 sequence
 * and delivers the resulting filesystem path as a WorkingDirectoryEvent
 */
void ControlProtocolParser::parseOsc7(std::string_view sequence) {
    // The caller classifies the sequence before dispatching it here, so an OSC 7
    // sequence must begin with the standard OSC 7 prefix
    if(!sequence.starts_with(ControlProtocol::osc7Prefix)) {
        throw std::runtime_error("OSC 7 parser called with invalid prefix");
    }

    // Determine which supported OSC terminator closes this sequence. This
    // length is needed so the terminator is excluded from the URI payload
    std::size_t terminatorLength = 0;

    if(sequence.ends_with(ControlProtocol::oscTerminator)) {
        // ST consists of the two bytes ESC \.
        terminatorLength = ControlProtocol::oscTerminator.size();
    }
    else if(!sequence.empty() && sequence.back() == ControlProtocol::oscBellTerminator) {
        // BEL terminates the OSC sequence with one byte
        terminatorLength = 1;
    }
    else {
        throw std::runtime_error("OSC 7 sequence missing supported terminator");
    }

    const std::size_t payloadStart = ControlProtocol::osc7Prefix.size();

    // The payload occupies everything between the fixed OSC 7 prefix and the
    // selected ST/BEL terminator
    const std::size_t payloadLength =
        sequence.size() -
        ControlProtocol::osc7Prefix.size() -
        terminatorLength;

    // View only the URI carried between the OSC 7 prefix and its
    // selected ST or BEL terminator
    const std::string_view uri(
            sequence.data() + payloadStart,
            payloadLength
    );

    constexpr std::string_view fileScheme = "file://";

    // OSC 7 working-directory metadata is represented as a file URI
    if(!uri.starts_with(fileScheme)) {
        throw std::runtime_error("OSC 7 payload is not a file URI");
    }

    // Skip "file://" and locate the slash that separates the URI authority
    // (normally the hostname) from the filesystem path
    const std::size_t authorityStart = fileScheme.size();
    const std::size_t pathStart = uri.find('/', authorityStart);

    if(pathStart == std::string_view::npos) {
        throw std::runtime_error("OSC 7 file URI does not contain a path");
    }

    // CaptureCoordinator needs the filesystem path rather than the URI
    // hostname, so decode only the path portion
    const std::string_view encodedPath = uri.substr(pathStart);

    const std::string decodedPath = percentDecodePath(encodedPath);

    if(decodedPath.empty()) {
        throw std::runtime_error("OSC 7 working directory is empty");
    }

    WorkingDirectoryEvent event{
        .workingDirectory = std::filesystem::path(decodedPath)
    };

    // Deliver the decoded directory independently of terminal forwarding.
    // tryParseOscSequence() is responsible for preserving the original standard
    // OSC 7 sequence in the terminal byte stream
    eventHandler(event);
}


/**
 * unescapeCommand()
 * Reconstructs the exact command text from the escaping used by the private
 * GPTB command-metadata sequence
 */
std::string ControlProtocolParser::unescapeCommand(std::string_view encodedCommand) {
    std::string command;
    command.reserve(encodedCommand.size());

    // Convert one hex digit into its numeric value. Returning -1 distinguishes
    // an invalid hex character from the valid value zero
    const auto hexValue = [](char character) -> int {
        if(character >= '0' && character <= '9') {
            return character - '0';
        }
        if(character >= 'A' && character <= 'F') {
            return character - 'A' + 10;
        }
        if(character >= 'a' && character <= 'f') {
            return character - 'a' + 10;
        }
        // The character is not a valid hexadecimal digit
        return -1;
    };

    for(std::size_t idx = 0; idx < encodedCommand.size(); ++idx) {
        // Ordinary bytes that are not escape introducers can be copied directly
        if(encodedCommand[idx] != '\\') {
            command.push_back(encodedCommand[idx]);
            continue;
        }

        // Every escape sequence requires at least one byte after the backslash
        if(idx + 1 >= encodedCommand.size()) {
            throw std::runtime_error("Incomplete escape in exact-command metadata");
        }

        // Two consecutive backslashes represent one literal backslash
        if(encodedCommand[idx + 1] == '\\') {
            command.push_back('\\');
            idx += 1;
            continue;
        }

        // Hexadecimal escapes use the exact form \xHH
        if(encodedCommand[idx + 1] != 'x') {
            throw std::runtime_error("Invalid escape in exact-command metadata");
        }

        if(idx + 3 >= encodedCommand.size()) {
            throw std::runtime_error("Incomplete hexadecimal escape in exact-command metadata");
        }

        const int highNibble = hexValue(encodedCommand[idx + 2]);
        const int lowNibble = hexValue(encodedCommand[idx + 3]);

        if(highNibble < 0 || lowNibble < 0) {
            throw std::runtime_error("Invalid hexadecimal escape in exact-command");
        }

        // Reconstruct the original byte from the two hexadecimal nibbles
        const unsigned char decodedByte = static_cast<unsigned char>((highNibble << 4) | lowNibble);
        command.push_back(static_cast<char>(decodedByte));

        // Skip the 'x'and both hexadecimal digits. The loop itself advances
        // past the leading backslash
        idx += 3;
    }

    // Return the exact shell command reconstructed from the encoded metadata
    return command;
}


/**
 * parseExactCommand()
 * Decodes gptbridge's private exact-command OSC metadata and validates that
 * the sequence belongs to the current capture session.
 *
 * Returns true when the private sequence was accepted and should be suppressed
 * from visible terminal output. A nonce mismatch returns false so unrelated
 * GPTB-looking terminal data can pass through unchanged.
 */
bool ControlProtocolParser::parseExactCommand(std::string_view sequence) {
    // The caller dispatches only sequences beginning with the private exact-
    // command prefix, so receiving any other prefix here is a parser error
    if(!sequence.starts_with(ControlProtocol::exactCommandPrefix)) {
        throw std::runtime_error("Exact-command parser called with invalid prefix");
    }
    if(!sequence.ends_with(ControlProtocol::oscTerminator)) {
        throw std::runtime_error("Exact-command sequence missing terminator");
    }

    const std::size_t payloadStart = ControlProtocol::exactCommandPrefix.size();
    const std::size_t payloadLength =
        sequence.size() -
        ControlProtocol::exactCommandPrefix.size() -
        ControlProtocol::oscTerminator.size();

    // The private payload contains:
    //   <escaped-command>;<session-nonce>
    const std::string_view payload(
            sequence.data() + payloadStart,
            payloadLength
    );

    // The command encoder escapes every literal semicolon in command text, so
    // the remaining literal separator marks the boundary before the nonce
    const std::size_t separatorPosition = payload.rfind(ControlProtocol::fieldSeparator);

    if(separatorPosition == std::string_view::npos) {
        throw std::runtime_error("Exact-command metadata missing session nonce");
    }

    const std::string_view encodedCommand = payload.substr(0, separatorPosition);
    const std::string_view candidateNonce = payload.substr(separatorPosition + 1);

    // A nonce belonging to another capture means this sequence is not private
    // metadata for the current parser instance
    if(candidateNonce != sessionNonce) {
        return false;
    }

    const std::string command = unescapeCommand(encodedCommand);

    ExactCommandEvent event{
        .command = command
    };

    // Deliver the decoded command metadata. tryParseOscSequence() suppresses the
    // accepted private sequence from visible terminal output
    eventHandler(event);

    return true;
}

/**
 * parseShellPresentationStart()
 * Validates gptbridge's private shell-presentation boundary and delivers a
 * ShellPresentationStartedEvent when the marker belongs to this capture session.
 *
 * Format:
 *   ESC ] GPTB ; P ; <nonce> ESC \
 *
 * Returns true when the marker is accepted and should be suppressed from
 * visible terminal output. A nonce mismatch returns false so unrelated
 * GPTB-looking terminal data can pass through unchanged.
 */
bool ControlProtocolParser::parseShellPresentationStart(std::string_view sequence) {
    // The caller dispatches only sequences beginning with the private
    // shell-presentation prefix, so any other prefix is a parser error
    if(!sequence.starts_with(ControlProtocol::shellPresentationPrefix)) {
        throw std::runtime_error("Shell-presentation parser called with invalid prefix");
    }

    // Private GPTB sequences always use ST (ESC \) as their terminator
    if(!sequence.ends_with(ControlProtocol::oscTerminator)) {
        throw std::runtime_error("Shell-presentation sequence missing terminator");
    }

    const std::size_t nonceStart = ControlProtocol::shellPresentationPrefix.size();

    const std::size_t nonceLength =
        sequence.size() -
        ControlProtocol::shellPresentationPrefix.size() -
        ControlProtocol::oscTerminator.size();

    // The presentation marker payload consists only of the capture-session
    // nonce between the fixed GPTB;P prefix and the OSC terminator
    const std::string_view candidateNonce(
        sequence.data() + nonceStart,
        nonceLength
    );

    // A different nonce means this private-looking marker was not emitted for
    // the capture session handled by this parser
    if(candidateNonce != sessionNonce) {
        return false;
    }

    // The marker has no additional payload. Its position in the ordered PTY
    // stream is itself the semantic event that presentation bytes begin here
    eventHandler(ShellPresentationStartedEvent{});

    // A validated GPTB private marker must not be shown in the user's terminal
    return true;
}



/**
 * parseOsc133()
 * Decodes supported OSC 133 command-lifecycle markers.
 *
 * Returns true when the sequence represents a lifecycle event understood by
 * gptbridge. Other OSC 133 forms return false so they can remain ordinary
 * terminal metadata and still be forwarded unchanged
 *
 * Standard OSC 133 sequences may terminate with either ST (ESC \) or BEL
 */
bool ControlProtocolParser::parseOsc133(std::string_view sequence) {
    // The caller dispatches only OSC 133 sequences to this helper
    if(!sequence.starts_with(ControlProtocol::osc133Prefix)) {
        throw std::runtime_error("OSC 133 parser called with invalid prefix");
    }

    // Determine which supported OSC terminator closes this sequence
    std::size_t terminatorLength = 0;

    if(sequence.ends_with(ControlProtocol::oscTerminator)) {
        terminatorLength = ControlProtocol::oscTerminator.size();
    }
    else if(!sequence.empty() && sequence.back() == ControlProtocol::oscBellTerminator) {
        terminatorLength = 1;
    }
    else {
        throw std::runtime_error("OSC 133 sequence missing supported terminator");
    }

    // OSC 133;C has no payload after C. Compare the semantic portion without
    // depending on whether the sender chose ST or BEL as its terminator
    constexpr std::string_view commandOutputStartPrefix = "\x1b]133;C";

    if(sequence.size() == commandOutputStartPrefix.size() + terminatorLength && sequence.starts_with(commandOutputStartPrefix)) {
        eventHandler(CommandOutputStartedEvent{});

        // The C marker was recognized as a command-output lifecycle boundary
        return true;
    }

    // OSC 133;D;<status> begins with the fixed completion prefix
    if(sequence.starts_with(ControlProtocol::commandFinishedPrefix)) {
        const std::size_t statusStart = ControlProtocol::commandFinishedPrefix.size();

        const std::size_t statusLength =
            sequence.size() -
            ControlProtocol::commandFinishedPrefix.size() -
            terminatorLength;

        // Extract only the decimal exit-status field between "D;" and the selected OSC terminator
        const std::string statusText = std::string(sequence.substr(statusStart, statusLength));

        if(statusText.empty()) {
            throw std::runtime_error("OSC 133 command-finished marker missing exit status");
        }

        int exitCode = 0;

        try {
            // Require the entire status field to be a decimal integer rather
            // than accepting an initial numeric prefix
            std::size_t parsedLength = 0;

            exitCode = std::stoi(
                statusText,     // Decimal exit status from OSC 133;D
                &parsedLength   // Number of characters consumed by std::stoi()
            );

            if(parsedLength != statusText.size()) {
                throw std::runtime_error("Invalid OSC 133 exit status");
            }
        }
        catch(const std::invalid_argument&) {
            throw std::runtime_error("Invalid OSC 133 exit status");
        }
        catch(const std::out_of_range&) {
            throw std::runtime_error("OSC 133 exit status is out of range");
        }

        CommandOutputFinishedEvent event{
            .exitCode = exitCode
        };

        eventHandler(event);

        // The D marker was recognized and converted into a completion event
        return true;
    }

    // OSC 133 also defines prompt/input-region markers such as A and B. They remain
    // valid terminal metadata but do not affect gptbridge's capture lifecycle
    return false;
}


/**
 * consume()
 * Processes the next chunk of bytes read from the PTY. Incomplete OSC data is
 * retained between calls because terminal control sequences may span multiple
 * PTY reads
 */
void ControlProtocolParser::consume(std::string_view bytes) {
    // PTY reads have arbitrary boundaries, so a read may contain only part of
    // an OSC sequence. Append new bytes to anything retained from the previous
    // call before attempting to classify the stream
    pendingBytes.append(bytes);

    // Continue processing until the remaining bytes are either exhausted or
    // incomplete enough that another PTY read is required
    while(!pendingBytes.empty()) {
        // Search for the next OSC introducer. Ever byte before it is ordinary
        // terminal data and can be forwarded immediately
        const std::size_t oscPosition = pendingBytes.find(ControlProtocol::oscIntroducer);

        // std::String_view::npos -> means "not found"
        if(oscPosition != std::string::npos) {
            emitOutput(
                std::string_view(
                    pendingBytes.data(),    // First ordinary terminal byte
                    oscPosition             // Number of bytes before ESC ]
                )
            );

            // Remove the ordinary bytes so the OSC introducer is now positioned
            // at index 0 for tryParseOscSequence()
            pendingBytes.erase(
                    0,              // Begin removing at the start of the buffer
                    oscPosition     // Remove only the bytes before the OSC sequence
            );

            // Try to classify and consume the frame that now begins at index 0
            const OscParseResult oscResult = tryParseOscSequence();

            if(oscResult == OscParseResult::Consumed) {
                // A complete OSC sequence was processed and removed. Continue
                // because additional termainal data may already follow it
                continue;
            }

            if(oscResult == OscParseResult::Incomplete) {
                // Preserve the incomplete OSC sequence exactly as received so
                // the next PTY read can provide its remaining bytes
                break;
            }
        }

        // No complete OSC introducer is currently present. Preserve only a
        // trailing ESC byte that could become ESC ] when the next read arrives
        const std::size_t preservedLength = partialOscIntroducerLength();

        // Everything before the possible partial introducer is confirmed
        // ordinary terminal output and can be emitted now
        const std::size_t outputLength = pendingBytes.size() - preservedLength;

        // Forward the confirmed ordinary-output portion of the buffer
        emitOutput(
            std::string_view(
                pendingBytes.data(),    // Beginning of confirmed terminal output
                outputLength            // Exclude any trailing possible ESC prefix
            )
        );

        // Remove the bytes hust forwarded while retaining any trailing ESC that
        // may begin an OSC sequence in the next PTY read
        pendingBytes.erase(
                0,                      // Begin erasing at the first buffered byte
                outputLength            // Remove exactly the bytes that were forwarded
        );

        // Nothing more can be classified until another PTY read arrives
        break;
    }
}
