#ifndef GPTB_GPT_IGNORE_HPP
#define GPTB_GPT_IGNORE_HPP

#include <filesystem>
#include <string>
#include <vector>


/**
 * GptIgnore
 * Loads project-local .gptignore rules and determines whether project-relative
 * paths should be hidden from MCP file operations.
 */
class GptIgnore {
    public:
        /**
         * Rule
         * Stores one parsed .gptignore pattern together with the modifiers
         * needed when evaluating it against a project-relative path.
         */
        struct Rule {
            std::string pattern;
            bool directoryOnly = false;
            bool anchored = false;
        };

        /**
         * GptIgnore()
         * Loads .gptignore from the supplied project root when the file exists.
         * A project without .gptignore has no user-defined exclusions.
         */
        explicit GptIgnore(const std::filesystem::path& projectRoot);

        /**
         * isIgnored()
         * Returns true when the project-relative path is excluded by the
         * currently loaded .gptignore rules.
         */
        bool isIgnored(
                const std::filesystem::path& relativePath,
                bool isDirectory = false) const;

    private:
        // Parsed project-local ignore rules evaluated against MCP-visible paths
        std::vector<Rule> rules;
};


#endif
