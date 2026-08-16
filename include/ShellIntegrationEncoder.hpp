#ifndef GPTB_SHELL_INTEGRATION_ENCODER_HPP
#define GPTB_SHELL_INTEGRATION_ENCODER_HPP

#include <filesystem>
#include <string>
#include <string_view>


/**
 * ShellIntegrationEncoder
 *
 * Encodes the terminal control sequences used by gptbridge shell integration.
 *
 * Standard OSC sequences are used where established semantics already exist:
 *   - OSC 7 reports the current working directory
 *   - OSC 133;C marks the beginning of command output
 *   - OSC 133;D marks command completion and carries the exit status
 *
 * Gptbridge-specific metadata that is not represented by those standards,
 * such as the shell's exact command text, uses a private GPTB OSC sequence.
 */
class ShellIntegrationEncoder {
    public:
        /**
         * encodeExactCommand()
         * Encodes the exact command text reported by the shell together with
         * the capture-session nonce in gptbridge's private OSC metadata format.
         */
        static std::string encodeExactCommand(std::string_view command, std::string_view sessionNonce);

        /**
         * encodeShellPresentationStart()
         * Encodes the private GPTB marker that identifies the beginning of the shell
         * presentation bytes that should remain visible but should not be persisted
         * as command output
         */
        static std::string encodeShellPresentationStart(std::string_view sessionNonce);


        /**
         * encodeWorkingDirectory()
         * Encodes the current working directory using standard OSC 7
         */
        static std::string encodeWorkingDirectory(const std::filesystem::path& workingDirectory);

        /**
         * encodeCommandFinished()
         * Encodes standard OSC 133;D with the completed command's exit status
         */
        static std::string encodeCommandFinished(int exitCode);

        /**
         * encodeCommandOutputStart()
         * Returns the standard OSC 133;C sequence that marks the authoratative
         * transition into the active command-output region
         */
        static std::string encodeCommandOutputStart();

    private:
        /**
         * escapeCommand()
         * Escapes exact command text so delimiters, control characters, and
         * backslashes cannot be confused with private OSC framing syntax
         */
        static std::string escapeCommand(std::string_view command);

        /**
         * percentEncodePath()
         * Converts a filesystem path into a URI-safe path component for OSC 7.
         * Directory-separator '/' bytes are preserved, URI-unreserved bytes are
         * copied directly, and all other bytes are percent-encoded as %HH
         */
        static std::string percentEncodePath(std::string_view path);

        /**
         * getLocalHostname()
         * Returns the hostname used as the authority component of the OSC 7
         * file:// URL describing the current working directory
         */
        static std::string getLocalHostname();
};


#endif
