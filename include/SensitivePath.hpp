#ifndef GPTB_SENSITIVE_PATH_HPP
#define GPTB_SENSITIVE_PATH_HPP

#include <filesystem>


/**
 * isSensitiveProjectPath()
 * Returns true when a project-relative path matches a known pattern for
 * secrets or credentials (env files, private keys, credential dumps, etc).
 *
 * This is a denylist, not a guarantee.
 */
bool isSensitiveProjectPath(const std::filesystem::path& relativePath);


#endif
