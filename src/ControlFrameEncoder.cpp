#include "ControlFrameEncoder.hpp"
#include "ControlProtocol.hpp"

#include <nlohmann/json.hpp>
#include <stdexcept>
#include <string_view>
#include <type_traits>


namespace {

    /**
     * serializeEvents()
     *
     * Converts one concrete ControlEvent alternative into the JSON payload expected
     * by ControlProtocolParser. Keeping serialization here makes the encoder the
     * single source of truth for producing protocol payloads
     */
    std::string serializeEvent(const ControlEvent& event) {
        nlohmann::json payload;

        // ControlEvent is a variant, so std::visit() supplies the concrete event
        // type currently stored in it
        std::visit([&payload](const auto& concreteEvent) {
                // Remove const/reference qualifiers so we can compare the underlying
                // event type with the supported ControlEvent alternatives
                using EventType = std::decay_t<decltype(concreteEvent)>;

                // For the CommandStartedEvent version of this generic lambda,
                // compile the fields required by a command_started payload.
                if constexpr(std::is_same_v<EventType, CommandStartedEvent>) {
                    // Build the JSON object expected by ControlProtocolParser.
                    // Each inner pair is { JSON field name, value for this specific event }.
                    // ControlProtocol::*Field constants provide the JSON keys, while
                    // concreteEvent.* provides the actual event data stored in the C++ struct.
                    payload = {
                        { ControlProtocol::typeField,
                          ControlProtocol::commandStartedType },
                        { ControlProtocol::interactionIdField,
                          concreteEvent.interactionId },
                        { ControlProtocol::commandField,
                          concreteEvent.command },
                        { ControlProtocol::workingDirectoryField,
                          concreteEvent.workingDirectory.string() },
                        { ControlProtocol::startedAtField,
                          concreteEvent.startedAt }
                    };
                }
                // CommendFinishedEvent is serialized in the same way, but with the fields
                // available on a finished event: interaction ID, exit code, and finish time.
                else if constexpr(std::is_same_v<EventType, CommandFinishedEvent>) {
                    payload = {
                        { ControlProtocol::typeField,
                          ControlProtocol::commandFinishedType },
                        { ControlProtocol::interactionIdField,
                          concreteEvent.interactionId },
                        { ControlProtocol::exitCodeField,
                          concreteEvent.exitCode },
                        { ControlProtocol::finishedAtField,
                          concreteEvent.finishedAt }
                    };
                }
            },
            event
        );
        // dump() serializes the JSON object into the exact UTF-8 byte string that
        // will become the control frame payload
        return payload.dump();
    }
}


/**
 * encode()
 *
 * Converts one structured control event into a complete control-protocol frame.
 * The JSON payload is serialized first so its exact byte length can be included
 * in the frame header for ControlProtocolParser to validate when decoding.
 */
std::string ControlFrameEncoder::encode(const ControlEvent& event, std::string_view sessionNonce) {

    // Serialize the event before constructing the frame because the protocol
    // header must contain the exact number of bytes in the JSON payload
    const std::string payload = serializeEvent(event);

    // The parser rejects payloads larger than the protocol's maximum size.
    // Enforce the same limit here so the encoder never creates a frame that
    // our own parser would reject
    if(payload.size() > ControlProtocol::maxPayloadSize) {
        throw std::runtime_error("Control frame payload exceeds maximum size");
    }

    // A nonce is required to associate the frame with one capture session
    if(sessionNonce.empty()) {
        throw std::runtime_error("Cannot encode control frame with empty session");
    }

    // The nonce occupies a delimeter-separated header field, so allowing the
    // delimeter inside it would make the resulting frame ambiguous to parse.
    if(sessionNonce.find(ControlProtocol::fieldSeparator) != std::string_view::npos) {
        throw std::runtime_error("Control frame session nonce contains invalid separator");
    }

    std::string frame;

    // Reserve enough storage for the complete frame. This is an optimization:
    // it avoids repeated reallocations while the individual pieces are appended
    frame.reserve(
            ControlProtocol::framePrefix.size() +   // Frame prefix
            sessionNonce.size() +                   // Session nonce
            1 +                                     // Field separator
            20 +    // Enough space for a decimal size_t payload length on 64-bit systems
            1 +                                     // Field separator
            payload.size() +                        // Size of payload
            ControlProtocol::frameTerminator.size() // Frame terminator
    );

    // frame.append() adds an entire string, whereas frame.push_back() adds only one character
    frame.append(ControlProtocol::framePrefix);
    frame.append(sessionNonce);
    frame.push_back(ControlProtocol::fieldSeparator);
    frame.append(std::to_string(payload.size()));
    frame.push_back(ControlProtocol::fieldSeparator);
    frame.append(payload);
    frame.append(ControlProtocol::frameTerminator);

    return frame;
}
