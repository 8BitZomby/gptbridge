#include "TerminalSecretDetector.hpp"

#include <array>
#include <string>
#include <string_view>


namespace {
    // Private-key headers are strong indicators that terminal text contains
    // cryptographic key material that should not be pushed without warning.
    constexpr std::array<std::string_view, 4> privateKeyHeaders = {
        "-----BEGIN PRIVATE KEY-----",
        "-----BEGIN RSA PRIVATE KEY-----",
        "-----BEGIN OPENSSH PRIVATE KEY-----",
        "-----BEGIN EC PRIVATE KEY-----"
    };


    /**
     * containsPrivateKey()
     * Returns true when terminal text contains a recognized private-key header.
     */
    bool containsPrivateKey(const std::string& text) {
        for(const std::string_view header : privateKeyHeaders) {
            if(text.find(header) != std::string::npos) {
                return true;
            }
        }

        return false;
    }

    /**
     * containsBearerToken()
     * Returns true when terminal text contains a Bearer authentication token marker
     */
    bool containsBearerToken(const std::string& text) {
        return text.find("Bearer ") != std::string::npos;
    }

    /**
     * containsCredentialAssignment()
     * Returns true when terminal text contains a common credential assignment marker.
     */
    bool containsCredentialAssignment(const std::string& text) {
        constexpr std::array<std::string_view, 6> credentialMarkers = {
            "API_KEY=",
            "TOKEN=",
            "ACCESS_TOKEN=",
            "SECRET=",
            "PASSWORD=",
            "PASSWD="
        };

        for(const std::string_view marker : credentialMarkers) {
            if(text.find(marker) != std::string::npos) {
                return true;
            }
        }

        return false;
    }
}


/**
 * detectTerminalSecrets()
 * Returns suspicious secret-like findings from selected terminal interactions.
 */
std::vector<SecretFinding> detectTerminalSecrets(const std::vector<TerminalInteraction>& interactions) {
    std::vector<SecretFinding> findings;

    // Inspect each selected interaction independently so findings can identify
    // whether the suspicious content came from the command or its output.
    for(std::size_t index = 0; index < interactions.size(); ++index) {
        const TerminalInteraction& interaction = interactions[index];

        // Flag private-key material typed directly into the command
        if(containsPrivateKey(interaction.command)) {
            findings.push_back({
                "Possible private key material",
                "Interaction " + std::to_string(index + 1) + " command"
            });
        }

        // Flag private-key material that appeared in the command output
        if(containsPrivateKey(interaction.output)) {
            findings.push_back({
                "Possible private key material",
                "Interaction " + std::to_string(index + 1) + " output"
            });
        }

        // Flag Bearer authentication tokens typed directly into the command
        if(containsBearerToken(interaction.command)) {
        findings.push_back({
            "Possible Bearer authentication token",
            "Interaction " + std::to_string(index + 1) + " command"
            });
        }

        // Flag Bearer authentication tokens that appeared in the command output
        if(containsBearerToken(interaction.output)) {
            findings.push_back({
                "Possible Bearer authentication token",
                "Interaction " + std::to_string(index + 1) + " output"
            });
        }

        // Flag common credential assignments typed directly into the command.
        if(containsCredentialAssignment(interaction.command)) {
            findings.push_back({
                "Possible credential assignment",
                "Interaction " + std::to_string(index + 1) + " command"
            });
        }

        // Flag common credential assignments that appeared in the command output.
        if(containsCredentialAssignment(interaction.output)) {
            findings.push_back({
                "Possible credential assignment",
                "Interaction " + std::to_string(index + 1) + " output"
            });
        }
    }

    return findings;
}
