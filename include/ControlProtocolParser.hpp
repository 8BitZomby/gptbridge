#ifndef GPTB_CONTROL_PROTOCOL_PARSER_HPP
#define GPTB_CONTROL_PROTOCOL_PARSER_HPP

#include "ControlProtocol.hpp"

#include <functional>
#include <string>
#include <string_view>


/**
 * ControlProtocolParser
 * Parses the PTY byte stream, separating ordinary terminal output from
 * gptbridge control frames. Valid control frames are decoded into ControlEvent
 * objects, while non-control bytes are passed through unchanged.
 */
class ControlProtocolParser {
    public:
        /**
         * OutputHandler
         * Receives ordinary terminal bytes that are not part of a valid
         * gptbridge control frame.
         */
        using OutputHandler = std::function<void(std::string_view)>;

        /**
         * EventHandler
         * Receives one decoded gptbridge control event
         */
        using EventHandler = std::function<void(const ControlEvent&)>;

        /**
         * ControlProtocolParser()
         * Creates a parser for one capture session using the nonce/
         * session token that legitimate control frames must contain.
         */
        ControlProtocolParser(std::string sessionNonce, OutputHandler outputHandler, EventHandler eventHandler);

        /**
         * consume()
         * Processes the next chunk of PTY bytes. The parser preserves any
         * incomplete control-frame data internally until more bytes arrive.
         */
        void consume(std::string_view bytes);

        /**
         * consumeControl()
         * Processes bytes received from the dedicated control channel. Unlike consume(),
         * this input is expected to contain only gptbridge control frames. Incomplete
         * frames are retained internally until later control-pipe reads provide the
         * remaining bytes.
         */
        void consumeControl(std::string_view bytes);

    private:
        // Nonce/session token required for control frames in this capture
        std::string sessionNonce;
        // Receives ordinary terminal bytes
        OutputHandler outputHandler;
        // Receives successfully decoded control events
        EventHandler eventHandler;

        /**
         * partialPrefixLength()
         * Returns the number of trailing bytes in pendingBytes that could be
         * the beginning of a control-frame prefix split actoss PTY reads
         */
        std::size_t partialPrefixLength() const;

        /**
         * FrameParseResult
         * Describes what happened when pendingBytes was examined for a control
         * frame beginning at its first byte
         */
        enum class FrameParseResult {
            Consumed,       // A complete valid control fram was parsed and removed
            Incomplete,     // The candidate may be valid, but more PTY bytes are needed
            NotControlFrame // The bytes do not belong to this capture session
        };

        /**
         * tryParseFrame()
         * Examines a control-frame candidate beginning at the start of pendingBytes.
         * The result distinguishes a complete frame, and incomplete frame, and bytes
         * that only resemble the protocol prefix but are not valid for this session
         */
        FrameParseResult tryParseFrame();

        /**
         * parsePayloadLength()
         * Converts and validates the payload-length field from a control header
         */
        std::size_t parsePayloadLength(std::size_t lengthStart, std::size_t lengthEnd) const;

        /**
         * emitOutput()
         * Sends ordinary terminal bytes to the configured output handler
         */
        void emitOutput(std::string_view bytes);

        /**
         * parseFrame()
         * Attempts to decode one complete gptbridge control-frame payload and
         * deliver the resulting semantic event to the configured event handler.
         */
        void parseFrame(std::string_view payload);

        // Holds bytes that cannot yet be classified because a control frame
        // may be split across multiple PTY reads
        std::string pendingBytes;
};


#endif
