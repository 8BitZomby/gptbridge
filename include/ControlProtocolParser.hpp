#ifndef GPTB_CONTROL_PROTOCOL_PARSER_HPP
#define GPTB_CONTROL_PROTOCOL_PARSER_HPP

#include "ControlProtocol.hpp"

#include <cstddef>
#include <functional>
#include <string>
#include <string_view>


/**
 * ControlProtocolParser
 * Parses the PTY byte stream, separating ordinary terminal output from
 * shell-integration control sequences. Relevant OSC sequences are decoded into
 * ControlEvent objects, while ordinary and unrelated terminal bytes are
 * passed through unchanged
 */
class ControlProtocolParser {
    public:
        /**
         * OutputHandler
         * Receives terminal bytes that should be forwarded to the real terminal.
         *
         * captureEligible distinguishes ordinary command output from standard terminal
         * metadata that must remain visible but must not be persisted as command output
         */
        using OutputHandler = std::function<void(std::string_view, bool captureEligible)>;

        /**
         * EventHandler
         * Receives one decoded gptbridge control event
         */
        using EventHandler = std::function<void(const ControlEvent&)>;

        /**
         * ControlProtocolParser()
         * Creates a parser for one capture session using the nonce required to
         * validate gptbridge's private OSC metadata
         */
        ControlProtocolParser(std::string sessionNonce, OutputHandler outputHandler, EventHandler eventHandler);

        /**
         * consume()
         * Processes the next chunk of PTY bytes. The parser preserves incomplete
         * OSC sequence data internally until more bytes arrive.
         */
        void consume(std::string_view bytes);

    private:
        // Nonce required to validate private gptbridge OSC metadata for this capture
        std::string sessionNonce;
        // Receives ordinary terminal bytes
        OutputHandler outputHandler;
        // Receives successfully decoded control events
        EventHandler eventHandler;

        /**
         * OscParseResult
         * Describes whether an OSC sequence beginning at the start of
         * pendingBytes was completely processed or requires additional bytes
         */
        enum class OscParseResult {
            Consumed,   // A complete OSC sequence was classified and removed
            Incomplete  // The OSC sequence is split across PTY reads
        };

        /**
         * tryParseOscSequence()
         * Examines one OSC sequence beginning at the start of pendingBytes.
         * Relevant OSC 7, OSC 133, and private GPTB sequences are decoded,
         * while unrelated OSC sequences are forwarded unchanged
         */
        OscParseResult tryParseOscSequence();

        /**
         * parseOsc7()
         * Decodes the working-directory file URI carried by an OSC 7 sequence
         * and delivers the resulting WorkingDirectoryEvent
         */
        void parseOsc7(std::string_view sequence);

        /**
         * parseExactCommand()
         * Decodes and validates gptbridge's private exact-command OSC metadata
         * and delivers the resulting ExactCommandEvent.
         *
         * Returns false when the sequence carries a nonce belonging to another
         * capture session, allowing those bytes to be forwarded unchanged
         */
        bool parseExactCommand(std::string_view sequence);

        /**
        * parseShellPresentationStart()
        * Decodes and validates gptbridge's private shell-presentation boundary.
        * A valid marker delivers a ShellPresentationStartedEvent identifying where
        * subsequent shell presentation bytes begin.
        *
        * Returns false when the sequence carries a nonce belonging to another
        * capture session, allowing those bytes to be forwarded unchanged.
        */
        bool parseShellPresentationStart(std::string_view sequence);

        /**
         * parseOsc133()
         * Decodes supported OSC 133 command lifecycle markers and delivers
         * CommandOutputStartedEvent or CommandOutputFinishedEvent as appropriate.
         *
         * Returns false when the OSC 133 sequence is not one of the lifecycle
         * forms currently interpreted by gptbridge
         */
        bool parseOsc133(std::string_view sequence);

        /**
         * unescapeCommand()
         * Reconstructs the exact command text from the escaping used by the
         * private GPTB command-metadata sequence
         */
        static std::string unescapeCommand(std::string_view encodedCommand);

        /**
         * percentDecodePath()
         * Reconstructs a filesystem path from the percent-encoded URI path
         * carried by OSC 7
         */
        static std::string percentDecodePath(std::string_view encodedPath);

        /**
         * partialOscIntroducerLength()
         * Returns the number of trailing bytes in pendingBytes that could be
         * the beginning of the OSC introducer split across PTY reads
         */
        std::size_t partialOscIntroducerLength() const;

        /**
        * emitOutput()
        * Sends terminal bytes to the configured output handler and identifies whether
        * those bytes are eligible to be persisted as command output.
        */
        void emitOutput(std::string_view bytes, bool captureEligible = true);

        // Holds bytes that cannot yet be classified because an OSC sequence
        // may be split across multiple PTY reads
        std::string pendingBytes;
};


#endif
