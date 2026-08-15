#include "ControlProtocolParser.hpp"
#include "ControlProtocol.hpp"

#include <algorithm>
#include <limits>
#include <nlohmann/json.hpp>
#include <stdexcept>
#include <string>
#include <string_view>
#include <utility>


/**
 * ControlProtocolParser()
 * Creates a parser for one capture session. The parser stores the session
 * nonce used to validate control frames and the callbacks used to deliver
 * ordinary terminal output and decoded control events.
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
 * Sends ordinary terminal bytes to the configured output handler.
 */
void ControlProtocolParser::emitOutput(std::string_view bytes) {
    // An empty byte range does not represent any terminal output, so avoid
    // invoking the callback when there is nothing to deliver.
    if(bytes.empty()) {
        return;
    }

    // The parser does not decide what ordinary output means or where it goes.
    // It only identifies that these bytes are not part of a control frame and
    // passes them to the component responsible for terminal/output handling.
    outputHandler(bytes);
}


/**
 * tryParseOscSequence()
 * Examines one OSC sequence beginning at the start of pendingBytes. Relevant
 * shell-integration sequences are decoded into semantic events, while unrelated
 * OSC sequences remain ordinary terminal data and are forwarded unchanged
 */
ControlProtocolParser::OscParseResult ControlProtocolParser::tryParseOscSequence() {
    // This helper is called only after consume() has positioned an OSC
    // introducer at index 0
    if(!pendingBytes.starts_with(ControlProtocol::oscIntroducer)) {
        throw std::runtime_error("OSC parser called without OSC introducer");
    }

    // Search for the ST terminator that closes this OSC sequence
    const std::size_t terminatorPosition =
        pendingBytes.find(
            ControlProtocol::oscTerminator,
            ControlProtocol::oscIntroducer.size()
        );

    // The OSC sequence may be split across arbitrary PTY reads. Preserve all
    // currently buffered bytes until its terminator arrives
    if(terminatorPosition == std::string::npos) {
        return OscParseResult::Incomplete;
    }

    // Include the complete ST terminator in the sequence view and removal size
    const std::size_t sequenceLength =
        terminatorPosition + ControlProtocol::oscTerminator.size();

    const std::string_view sequence(
        pendingBytes.data(),    // First byte of the OSC introducer
        sequenceLength          // Complete OSC sequence including its terminator
    );

    bool suppressSequence = false;

    if(sequence.starts_with(ControlProtocol::osc7Prefix)) {
        // OSC 7 is standard terminal metadata. Decode it for gptbridge while
        // still forwarding the original sequence to the outer terminal
        parserOsc7(sequence);
    }
    else if(sequence.starts_with(ControlProtocol::osc133Prefix)) {
        // Only the OSC 133 lifecycle forms understood by gptbridge produce
        // semantic events. Other OSC 133 forms remain valid terminal data
        (void)parseOsc133(sequence);
    }
    else if(sequence.starts_with(ControlProtocol::exactCommandPrefix)) {
        // Private GPTB command metadata is consumed only when its nonce belongs
        // to this capture session. Foreign sequences are forwarded unchanged
        suppressSequence = parseExactCommand(sequence);
    }
    else if(sequence.starts_with(ControlProtocol::framePrefix)) {
        // The JSON-based GPTB frame parser remains available while that
        // protocol is still accepted by the capture path
        const FrameParseResult frameResult = tryParseFrame();

        if(frameResult == FrameParseResult::Incomplete) {
            return OscParseResult::Incomplete;
        }
        if(frameResult == FrameParseResult::Consumed) {
            return OscParseResult::Consumed;
        }

        // A GPTB-looking sequence with another session nonce is ordinary
        // terminal data and therefore falls through to forwarding below
    }

    if(!suppressSequence) {
        emitOutput(sequence);
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
void ControlProtocolParser::parserOsc7(std::string_view sequence) {
    // The caller classifies the sequence before dispatching it here, so an OSC 7
    // sequence must begin with the standard OSC 7 prefix
    if(!sequence.starts_with(ControlProtocol::osc7Prefix)) {
        throw std::runtime_error("OSC 7 parser called with invalud prefix");
    }

    // A complete OSC sequence must end with the standard ST terminator
    if(!sequence.ends_with(ControlProtocol::oscTerminator)) {
        throw std::runtime_error("OSC 7 sequence missing terminator");
    }

    const std::size_t payloadStart = ControlProtocol::osc7Prefix.size();
    const std::size_t payloadLength =
        sequence.size() -
        ControlProtocol::osc7Prefix.size() -
        ControlProtocol::oscTerminator.size();

    // View only the URI carried between "OSC7;" and the ST terminator
    const std::string_view uri(
            sequence.data() - payloadStart,
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

    // The hostname identifies where the path resides. The current capture
    // backend interprets OSC 7 only as working-directory metadata for the local
    // shell, so the path itself is the value needed by the semantic event
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
 * Returns true when the private sequence was accepted and should be supressed
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
 * parseOsc133()
 * Decodes supported OSC 133 command-lifecycle markers.
 *
 * Returns true when the sequence represents a lifecycle event understood by
 * gptbridge. Other OSC 133 forms return false so they can remain ordinary
 * terminal metadata and still be forwarded unchanged
 */
bool ControlProtocolParser::parseOsc133(std::string_view sequence) {
    // The caller dispatches only OSC 133 sequences to this helper
    if(!sequence.starts_with(ControlProtocol::osc133Prefix)) {
        throw std::runtime_error("OSC 133 parser called with invalid prefix");
    }

    // Every complete OSc sequence must end with the ST terminator
    if(!sequence.ends_with(ControlProtocol::oscTerminator)) {
        throw std::runtime_error("OSC 133 sequence missing terminator");
    }

    // OSC 133;C is a complete fixed sequence marking the transition into the
    // command-output region
    if(sequence == ControlProtocol::commandOutputStart) {
        eventHandler(CommandOutputStartedEvent{});

        // The sequence was recognized and produce a semantic lifecycle event
        return true;
    }

    // OSC 133;D;<status> begins with the fixed completion prefix
    if(sequence.starts_with(ControlProtocol::commandFinishedPrefix)) {
        const std::size_t statusStart = ControlProtocol::commandFinishedPrefix.size();

        const std::size_t statusLength =
            sequence.size() -
            ControlProtocol::commandFinishedPrefix.size() -
            ControlProtocol::oscTerminator.size();

        // Extract only the decimal exit-status field between "D;" and ST
        const std::string statusText = std::string(sequence.substr(statusStart, statusLength));

        if(statusText.empty()) {
            throw std::runtime_error("OSC 133 command-finished marker missing exit status");
        }

        int exitCode = 0;

        try {
            // Require the entire field to be a valid decimal integer rather
            // than accepting a numeric prefix followed by invalid characters
            std::size_t parsedLength = 0;

            exitCode = std::stoi(
                statusText,     // Decimal exit status from OSC 133;D
                &parsedLength   // Receives the number of character passed
            );

            if(parsedLength != statusText.size()) {
                throw std::runtime_error("Invalid OSC 133 exit status");
            }
        }
        catch(const std::invalid_argument&) {
            throw std::runtime_error("Invalid OSC 133 exit status");
        }
        catch(const std::out_of_range&) {
            throw std::runtime_error("Osc 133 exit status is out of range");
        }

        CommandOutputFinishedEvent event{
            .exitCode = exitCode
        };

        eventHandler(event);

        // The completion marker was recognized and converted into an event
        return true;
    }

    // OSC 133 also defines promt/input-region markers such as A and B. Those
    // are valid terminal metadata byt are not lifecycle events gptbridge needs
    return false;
}


/**
 * parseFrame()
 * Decodes the JSON payload from one complete, validated control frame and
 * converts it into the corresponding strongly typed control event.
 */
void ControlProtocolParser::parseFrame(std::string_view payload) {
    nlohmann::json frame;

    // Parse only the payload portion of the frame. Framing markers and the
    // session nonce are handled by consume() before this function is called
    try {
        frame = nlohmann::json::parse(payload);
    }
    catch(const nlohmann::json::parse_error&) {
        throw std::runtime_error("Failed to parse control frame payload");
    }

    // Every control event identifies its semantic type through this field.
    // Using at() makes a missing type an error rather than silently inventing
    // a default value
    const std::string type = frame.at(ControlProtocol::typeField).get<std::string>();

    if(type == ControlProtocol::commandStartedType) {
        CommandStartedEvent event{
            .interactionId = frame.at(ControlProtocol::interactionIdField).get<std::string>(),
            .command = frame.at(ControlProtocol::commandField).get<std::string>(),
            .workingDirectory = frame.at(ControlProtocol::workingDirectoryField).get<std::string>(),
            .startedAt = frame.at(ControlProtocol::startedAtField).get<std::string>()
        };
        eventHandler(event);
        return;
    }

    if(type == ControlProtocol::commandFinishedType) {
        CommandFinishedEvent event{
            .interactionId = frame.at(ControlProtocol::interactionIdField).get<std::string>(),
            .exitCode = frame.at(ControlProtocol::exitCodeField).get<int>(),
            .finishedAt = frame.at(ControlProtocol::finishedAtField).get<std::string>()
        };
        eventHandler(event);
        return;
    }
    throw std::runtime_error("Unknown control frame type: " + type);
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


/**
 * tryParseFrame()
 * Examines a control-frame candidate beginning at the start of pendingBytes.
 * The result distinguishes a complete frame, and incomplete frame, and bytes
 * that only resemble the protocol prefix but are not valid for this session
 */
ControlProtocolParser::FrameParseResult ControlProtocolParser::tryParseFrame() {
    // A candidate frame must begin with the complete fixed protocol prefix
    if(!pendingBytes.starts_with(ControlProtocol::framePrefix)) {
        return FrameParseResult::NotControlFrame;
    }

    // The nonce begins immediately after the fixed fram prefix
    const std::size_t nonceStart = ControlProtocol::framePrefix.size();

    // Search for the separator that marks the ennd of the nonce field
    const std::size_t nonceEnd = pendingBytes.find(ControlProtocol::fieldSeparator, nonceStart);

    // No separator yet means the nonce field may be split across reads
    if(nonceEnd == std::string::npos) {
        return FrameParseResult::Incomplete;
    }

    // View the nonce from the cadidate frame without copying it
    const std::string_view candidateNonce(
            pendingBytes.data() + nonceStart,
            nonceEnd - nonceStart
    );

    // A different nonce means this prefix-like sequence was not emitted by
    // the shell integration for the current capture session
    if(candidateNonce != sessionNonce) {
        return FrameParseResult::NotControlFrame;
    }

    // The payload-length field begins immediately after the nonce separator
    const std::size_t lengthStart = nonceEnd + 1;

    // Search for the separator that terminates the payload-length field
    const std::size_t lengthEnd = pendingBytes.find(ControlProtocol::fieldSeparator, lengthStart);

    // The length field may also be split across PTY reads
    if(lengthEnd == std::string::npos) {
        return FrameParseResult::Incomplete;
    }

    // Convert and validate the payload-length field from the frame header
    const std::size_t payloadLength = parsePayloadLength(lengthStart, lengthEnd);

    // The JSON payload begins immediately after the length-field separator
    const std::size_t payloadStart = lengthEnd + 1;

    // Verify that adding the declared payload length cannot overflow size_t
    if(payloadLength > std::numeric_limits<std::size_t>::max() - payloadStart) {
        throw std::runtime_error("Control frame payload length overflow");
    }

    // Calculate where the payload ends based on its declared byte length
    const std::size_t payloadEnd = payloadStart + payloadLength;

    // A complete frame also needs the fixed terminator after the payload
    const std::size_t completeFrameLength = payloadEnd + ControlProtocol::frameTerminator.size();

    // If those bytes have not all arrived yet, preserve the buffer unchanged
    if(pendingBytes.size() < completeFrameLength) {
        return FrameParseResult::Incomplete;
    }

    // Verify that the bytes immediately following the payload are the
    // expected control-frame terminator
    if(pendingBytes.compare(
                payloadEnd,
                ControlProtocol::frameTerminator.size(),
                ControlProtocol::frameTerminator
            ) != 0) {
        throw std::runtime_error("Invalid control frame terminator");
    }

    // View exactly the declared JSON payload bytes without copying them
    const std::string_view payload(
        pendingBytes.data() + payloadStart,
        payloadLength
    );

    // Decode the payload and deliver the resulting semantic event
    parseFrame(payload);

    // Remove the complete control frame while preserving any terminal byte
    // or additional frame that followed it in the same PTY read
    pendingBytes.erase(0, completeFrameLength);

    return FrameParseResult::Consumed;
}



/**
 * parsePayloadLength()
 * Converts and validates the payload-length field from a control header
 */
std::size_t ControlProtocolParser::parsePayloadLength(std::size_t lengthStart, std::size_t lengthEnd) const {

    // Copy only the characters that represent the decimal payload length
    const std::string lengthText =
        pendingBytes.substr(
            lengthStart,                    // First character of the length field
            lengthEnd - lengthStart         // Number of characters in the field
    );

    std::size_t payloadLength = 0;

    try {
        // Convert the decimal length text into the number of payload bytes
        payloadLength = std::stoull(lengthText);
    }
    catch(const std::exception&) {
        // A matching nonce has already identified this as one of our
        // frames, so an invalid length is a malformed protocol message
        throw std::runtime_error("Invalid control frame payload length");
    }

    // Prevent a frame from declaring an unreasonably large JSON payload
    if(payloadLength > ControlProtocol::maxPayloadSize) {
        throw std::runtime_error("Control frame payload exceeds maximum size");
    }
    return payloadLength;
}
