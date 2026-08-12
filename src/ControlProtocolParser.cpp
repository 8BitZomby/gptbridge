#include "ControlProtocolParser.hpp"
#include "ControlProtocol.hpp"

#include <algorithm>
#include <limits>
#include <nlohmann/json.hpp>
#include <stdexcept>
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
 * partialPrefixLength()
 * Returns the number of trailing bytes in pendingBytes that could be
 * the beginning of a control-frame prefix split actoss PTY reads
 */
std::size_t ControlProtocolParser::partialPrefixLength() const {
    // A partial match can never be as long as the complete frame prefix.
    // If the entire prefix were present, consume() would already have found it
    const std::size_t maxLength = std::min(
            pendingBytes.size(),                        // Number of bytes currently waiting to be parsed
            ControlProtocol::framePrefix.size() - 1);   // Longest possible match that is still incomplete

    // Try the longest possible partial match first. As soon as one matches,
    // it is the max number of trailing bytes that must be preserved
    for(std::size_t length = maxLength; length > 0; --length) {

        // Calculate where a suffix of this length begins inside pendingBytes
        const std::size_t suffixStart = pendingBytes.size() - length;

        // View the last 'length' bytes currently stored in pendingBytes
        const std::string_view suffix(
                pendingBytes.data() + suffixStart,      // Pointer to the first byte of the suffix
                length);                                // Number of trailing bytes to examine

        // View the first 'length' bytes of the known control-frame prefix
        const std::string_view prefixStart(
                ControlProtocol::framePrefix.data(),    // Pointer to the beginning of the frame prefix
                length);                                // Compare the same number of bytes as the suffix

        // If the buffered suffix matches the beginning of the frame prefix,
        // these bytes may be the start of a frame continued by the next PTY read
        if(suffix == prefixStart) {
            return length;                              // Preserve this many trailing bytes
        }
    }
    // No trailing bytes resemble the beginning of a control-frame prefix
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
 * Processes the next chunk of bytes read from the PTY. Incomplete data is
 * retained between calls because a control frame may span multiple PTY reads
 */
void ControlProtocolParser::consume(std::string_view bytes) {
    // PTY reads have arbitrary boundaries, so a read may contain only part of
    // a control frame. Append new bytes to anything retained from the previous
    // call before attempting to classify the stream
    pendingBytes.append(bytes);

    // Continue processing until the remaining bytes are either exhausted or
    // incomplete enough that another PTY read is required
    while(!pendingBytes.empty()) {
        // Look for the next complete occurence of the control-frame prefix
        const std::size_t prefixPosition = pendingBytes.find(ControlProtocol::framePrefix);

        // If a complete prefix is present, every byte before it is ordinary
        // terminal output and can be emitted immediately
        // std::String_view::npos -> means "not found"
        if(prefixPosition != std::string::npos) {
            emitOutput(std::string_view(pendingBytes.data(), prefixPosition));

            // Remove the ordinary output, leaving the control-frame prefix at
            // the beginning of pendingBytes for the next parsing step
            pendingBytes.erase(0, prefixPosition);

            // Try to classify and consume the frame that now begins at index 0
            const FrameParseResult frameResult = tryParseFrame();

            // A complete valid frame was parsed and removed. Continue because
            // more output or additional frames may already follow in the buffer
            if(frameResult == FrameParseResult::Consumed) {
                continue;
            }

            // The candidate may be a valid frame, but some of its bytes have
            // not arrived yet. Preserve pendingBytes unchanged until next read
            if(frameResult == FrameParseResult::Incomplete) {
                break;
            }

            // The fixed prefix matched, but the nonce/session token did not.
            // There this is ordinary terminal data rather than private control frame
            if(frameResult == FrameParseResult::NotControlFrame) {
                // Emit one byte so the parser makes forward progress without
                // discarding bytes that might contain another valid prefix
                emitOutput(
                    std::string_view(
                        pendingBytes.data(),
                        1
                    )
                );

                // Remove exactly the one byte that was emitted
                pendingBytes.erase(0, 1);

                // Search the remaining stream again from its new beginning
                continue;
            }
        }

        // No complete frame prefix is currently present. Keep only the trailing
        // bytes that could still become the beginning of a prefix on the next read
        const std::size_t preservedLength = partialPrefixLength();

        // Everything before the possible partial prefix is definitely ordinary
        // terminal output and can be emitted now
        const std::size_t outputLength = pendingBytes.size() - preservedLength;

        // Forward the confirmed ordinary-output portion of the buffer
        emitOutput(
            std::string_view(
                pendingBytes.data(),    // Start at the beginning of the buffered bytes
                outputLength            // Emit everything except the preserved suffix
            )
        );

        // Remove the bytes that were just emitted. Any possible partial prefix
        // remains in pendingBytes for the next consume() call
        pendingBytes.erase(
                0,                      // Begin erasing at the first byte
                outputLength            // Remove exactly the bytes that were emitted
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
