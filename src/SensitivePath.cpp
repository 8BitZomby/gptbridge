#include "SensitivePath.hpp"

#include <algorithm>
#include <array>
#include <string>
#include <string_view>


namespace {
    // Exact filenames that are almost always secrets or credentials, regardless
    // of where they appear
    constexpr std::array<std::string_view, 20> sensitiveExactNames = {
        ".DS_Store",            // macOs filesystem metadata
        ".envrc",               // direnv contig that may export secrets
        ".gitattributes",       // Git path attributes and repo behaviour config
        ".git-credentials",     // Git credential-store contents
        ".gitignore",           // Git ignore rules
        ".gitmodules",          // Git submodule repo locations and config
        ".mailmap",             // Git author identity mapping metadata
        ".npmrc",               // NPM registry auth token
        ".netrc",               // Saved login credentials
        ".pypirc",              // Python package-index credentials
        ".terraformrc",         // Terraform CLI creds and local config
        "credentials.json",     // Dumped API/service credentials
        "desktop.ini",          // Windows folder metadata
        "id_ecdsa",             // SSH private key (ECDSA algorithm)
        "id_ed25519",           // SSH private key (Ed25519 algorithm)
        "id_dsa",               // SSH private key (DSA algorithm)
        "id_rsa",               // Unencrypted SSH private key (RSA)
        "secrets.json",         // Dumped API/service credentials
        "terraform.rc",         // Terraform CLI credentials/config
        "Thumbs.db"             // Windows filesystem metadata
    };

    // Directory names that, if they appear anywhere in the relative path, mean
    // every file underneath should be treated as sensitive
    constexpr std::array<std::string_view, 10> sensitiveDirectoryNames = {
        ".aws",                 // AWS credentials/config
        ".docker",              // Docker registry credentials
        ".git",                 // Git metadata and local metadata
        ".github",              // Git workflows and repo config
        ".gnupg",               // GPG private keys
        ".hg",                  // Mercurial repo metadata
        ".kube",                // Kubernetes config creds
        "secrets",              // Credential files
        ".ssh",                 // SSH keys and known_hosts
        ".svn"                  // Subversion repo metadata
    };

    // File extensions commonly used for private keys, certificates, and credential
    // bundles.
    constexpr std::array<std::string_view, 7> sensitiveExtensions = {
        ".pem",                 // PEM-encoded key or certificate
        ".key",                 // Generic private key file
        ".p12",                 // PKCS#12 certificate/key bundle
        ".pfx",                 // Windows certificate/key bundle
        ".keystore",            // Java keystore
        ".jks",                 // Java KeyStore file extension
        ".asc"                  // ASCII-armored file
    };

    /**
     * isEnvFileName()
     * Matches ".env" and every dotted variant (".env.local", ".env.production", ...)
     * with a single prefix check instead of listing each variant by hand
     */
    bool isEnvFileName(const std::string& filename) {
        // rfind normally searches backward from the end of the string, but
        // passing 0 as the second argument restricts it to only match starting
        // at position 0 - so this is really a "starts with" check
        return filename.rfind(".env", 0) == 0;
    }
}   // Namespace end

/**
 * isSensitiveProjectPath()
 *
 */
bool isSensitiveProjectPath(const std::filesystem::path& relativePath) {
    // Check every path component for a sensitive directory name. This
    // catches files nested inside e.g. "config/secrets/api-key.txt"
    // even though the lead filename itself looks harmless
    for(const auto& component : relativePath) {
        const std::string componentName = component.string();

        // any_of runs the lambda once per entry in sensitiveDirectoryNames
        // and stops as soon as one returns true
        const bool matchesSensitiveDirectory = std::any_of(
            sensitiveDirectoryNames.begin(),
            sensitiveDirectoryNames.end(),
            [&componentName](std::string_view name) {
                return componentName == name;
            }
        );

        if(matchesSensitiveDirectory) {
            return true;
        }
    }

    const std::string filename = relativePath.filename().string();

    if(filename.empty()) {
        return false;
    }

    // Checked before the exact-name list below since ".env" variants are
    // the most common way a secret ends up in a directory
    if(isEnvFileName(filename)) {
        return true;
    }

    const bool matchesExactName = std::any_of(
        sensitiveExactNames.begin(),
        sensitiveExactNames.end(),
        [&filename](std::string_view name) {
            return filename == name;
        }
    );

    if(matchesExactName) {
        return true;
    }

    // extension() includes the leading dot ("server.pem" -> ".pem"),
    // which is why sensitiveExtensions entries are written with a dot too
    const std::string extension = relativePath.extension().string();

    const bool matchesExtension = std::any_of(
        sensitiveExtensions.begin(),
        sensitiveExtensions.end(),
        [&extension](std::string_view sensitiveExtension) {
            return extension == sensitiveExtension;
        }
    );

    return matchesExtension;
}
