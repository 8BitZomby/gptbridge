#include "GptIgnore.hpp"
#include "McpProjectFilesystem.hpp"
#include "ProjectVisibility.hpp"
#include "SensitivePath.hpp"

#include <cstdint>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <stdexcept>
#include <string>


namespace {

    // Number of expectations that failed during the current test run.
    int failureCount = 0;


    /**
     * expectTrue()
     * Records a failed boolean expectation while allowing the remaining
     * regression cases to continue running.
     */
    void expectTrue(bool condition, const std::string& description) {
        // A satisfied expectation requires no additional work.
        if(condition) {
            return;
        }

        // Report the failed expectation and record it for the final result.
        std::cerr << "FAIL: " << description << '\n';
        ++failureCount;
    }


    /**
     * expectFalse()
     * Convenience wrapper for expectations whose correct result is false.
     */
    void expectFalse(bool condition, const std::string& description) {
        // Reuse expectTrue() by negating the supplied condition.
        expectTrue(!condition, description);
    }


    /**
     * TemporaryProject()
     * Creates an isolated temporary project directory and removes it when the
     * test process leaves the scope.
     */
    class TemporaryProject {
        public:
            TemporaryProject() {
                // Use the operating system's temporary directory as the fixture root.
                const std::filesystem::path temporaryRoot =
                    std::filesystem::temp_directory_path();

                // Generate a changing value for the temporary project directory name.
                const auto timestamp =
                    std::filesystem::file_time_type::clock::now()
                        .time_since_epoch()
                        .count();

                // Build a project-specific temporary directory path.
                projectPath =
                    temporaryRoot /
                    std::filesystem::path(
                        "gptbridge-mcp-policy-tests-" +
                        std::to_string(
                            static_cast<long long>(timestamp)
                        )
                    );

                // Create the isolated project fixture directory.
                std::filesystem::create_directories(projectPath);
            }


            ~TemporaryProject() {
                // Cleanup should not throw while the fixture is being destroyed.
                std::error_code removalError;

                // Remove the complete temporary project tree.
                std::filesystem::remove_all(
                    projectPath,
                    removalError
                );
            }


            /**
             * path()
             * Returns the root directory of the temporary project fixture.
             */
            const std::filesystem::path& path() const {
                return projectPath;
            }


            /**
             * writeGptIgnore()
             * Writes the supplied ignore rules to the fixture's .gptignore file.
             */
            void writeGptIgnore(const std::string& contents) const {
                // Open the project-local ignore file for replacement.
                std::ofstream output(
                    projectPath / ".gptignore"
                );

                // Fixture creation cannot continue if the ignore file cannot be written.
                if(!output) {
                    throw std::runtime_error(
                        "Failed to create temporary .gptignore"
                    );
                }

                // Store the exact ignore rules required by the test.
                output << contents;
            }


            /**
             * writeFile()
             * Creates a project-relative test file, including any required parent
             * directories, so filesystem-policy cases can build isolated fixtures.
             */
            void writeFile(
                    const std::filesystem::path& relativePath,
                    const std::string& contents) const {

                // Resolve the fixture file beneath the temporary project root.
                const std::filesystem::path filePath =
                    projectPath / relativePath;

                // Create any directory hierarchy required by the fixture path.
                if(filePath.has_parent_path()) {
                    std::filesystem::create_directories(
                        filePath.parent_path()
                    );
                }

                // Binary mode preserves embedded bytes such as NUL characters.
                std::ofstream output(
                    filePath,
                    std::ios::binary
                );

                // Report fixture-creation failures immediately.
                if(!output) {
                    throw std::runtime_error(
                        "Failed to create temporary project file"
                    );
                }

                // Write the exact fixture contents.
                output << contents;
            }


            /**
             * writeLargeFile()
             * Creates a file of an exact byte size without constructing a second large
             * in-memory string in each test case.
             */
            void writeLargeFile(
                    const std::filesystem::path& relativePath,
                    std::uintmax_t size) const {

                // Resolve the requested fixture path beneath the project root.
                const std::filesystem::path filePath =
                    projectPath / relativePath;

                // Create parent directories when the fixture is nested.
                if(filePath.has_parent_path()) {
                    std::filesystem::create_directories(
                        filePath.parent_path()
                    );
                }

                // Binary mode makes each output character exactly one byte.
                std::ofstream output(
                    filePath,
                    std::ios::binary
                );

                // The size-limit test cannot proceed without its fixture.
                if(!output) {
                    throw std::runtime_error(
                        "Failed to create temporary large project file"
                    );
                }

                // Write exactly the requested number of bytes.
                for(std::uintmax_t index = 0; index < size; ++index) {
                    output.put('A');
                }
            }

        private:
            // Root directory containing this test fixture.
            std::filesystem::path projectPath;
    };


    /**
     * testSensitivePaths()
     * Verifies filenames, extensions, and directories that must be treated as
     * sensitive by MCP filesystem policy.
     */
    void testSensitivePaths() {
        // A standard environment file must always be sensitive.
        expectTrue(
            isSensitiveProjectPath(".env"),
            ".env should be sensitive"
        );

        // Environment-file variants must inherit the same protection.
        expectTrue(
            isSensitiveProjectPath(".env.production"),
            ".env variants should be sensitive"
        );

        // SSH private-key filenames must be blocked regardless of directory.
        expectTrue(
            isSensitiveProjectPath("config/id_rsa"),
            "SSH private-key filenames should be sensitive"
        );

        // Private-key/certificate extensions must be recognized as sensitive.
        expectTrue(
            isSensitiveProjectPath("config/server.pem"),
            "private-key/certificate extensions should be sensitive"
        );

        // Any file beneath a sensitive directory must inherit that sensitivity.
        expectTrue(
            isSensitiveProjectPath("config/secrets/token.txt"),
            "files below sensitive directories should be sensitive"
        );

        // Git metadata is internal project state and must remain hidden.
        expectTrue(
            isSensitiveProjectPath(".git/config"),
            ".git contents should be sensitive"
        );

        // Ordinary source files must remain available to MCP policy.
        expectFalse(
            isSensitiveProjectPath("src/main.cpp"),
            "ordinary source files should not be sensitive"
        );
    }


    /**
     * testProjectVisibility()
     * Verifies which ordinary project file types are allowed through MCP and
     * confirms that sensitive-path rules override extension visibility.
     */
    void testProjectVisibility() {
        // C++ implementation files are part of the visible source allowlist.
        expectTrue(
            isProjectPathVisible("src/main.cpp"),
            ".cpp files should be MCP-visible"
        );

        // C++ headers are part of the visible source allowlist.
        expectTrue(
            isProjectPathVisible("include/example.hpp"),
            ".hpp files should be MCP-visible"
        );

        // README documentation is intentionally exposed to MCP.
        expectTrue(
            isProjectPathVisible("README.md"),
            "README.md should be MCP-visible"
        );

        // Supported template suffixes must remain visible.
        expectTrue(
            isProjectPathVisible("cmake/generated.hpp.in"),
            ".hpp.in templates should be MCP-visible"
        );

        // Unknown extensions are hidden unless explicitly supported.
        expectFalse(
            isProjectPathVisible("notes.txt"),
            "unknown file extensions should remain hidden"
        );

        // Sensitive filenames override otherwise general visibility decisions.
        expectFalse(
            isProjectPathVisible(".env"),
            "sensitive files must remain hidden regardless of extension policy"
        );

        // Sensitive directories remain hidden even when the filename resembles source code.
        expectFalse(
            isProjectPathVisible(".git/config.cpp"),
            "source-looking files inside sensitive directories must remain hidden"
        );
    }


    /**
     * testGptIgnorePatterns()
     * Verifies supported .gptignore rule forms and wildcard behavior.
     */
    void testGptIgnorePatterns() {
        // Create an isolated project containing only the ignore rules under test.
        TemporaryProject project;

        // Define examples for filenames, directories, wildcards, globstars, and anchoring.
        project.writeGptIgnore(
            "# exact file\n"
            "generated.cpp\n"
            "\n"
            "# directory\n"
            "cache/\n"
            "\n"
            "# ordinary wildcard\n"
            "*.tmp\n"
            "logs/*.scratch\n"
            "\n"
            "# single-character wildcard\n"
            "file?.cpp\n"
            "\n"
            "# globstar\n"
            "src/**/generated.hpp\n"
            "\n"
            "# root anchored\n"
            "/root-only.cpp\n"
        );

        // Load the fixture's ignore rules once for all pattern checks.
        const GptIgnore ignore(project.path());

        // A direct filename rule must match the same filename at the root.
        expectTrue(
            ignore.isIgnored("generated.cpp"),
            "exact filename rule should match"
        );

        // Slashless filename rules must also match nested components.
        expectTrue(
            ignore.isIgnored("nested/generated.cpp"),
            "unanchored filename rule should match at any depth"
        );

        // A trailing-slash rule must match the named directory itself.
        expectTrue(
            ignore.isIgnored("cache", true),
            "directory rule should match the directory itself"
        );

        // Directory rules must hide files below the matching directory.
        expectTrue(
            ignore.isIgnored("cache/data.cpp"),
            "directory rule should hide descendants"
        );

        // Unanchored directory rules must work below the project root.
        expectTrue(
            ignore.isIgnored("nested/cache/data.cpp"),
            "unanchored directory rule should match at any depth"
        );

        // "*" may consume any number of characters inside one component.
        expectTrue(
            ignore.isIgnored("scratch.tmp"),
            "* should match within one path component"
        );

        // Slashless wildcard rules must work on nested filenames as well.
        expectTrue(
            ignore.isIgnored("nested/scratch.tmp"),
            "unanchored * filename rule should match at any depth"
        );

        // "*" may consume characters in the final component of a path rule.
        expectTrue(
            ignore.isIgnored("logs/build.scratch"),
            "* should match within one path component of a path pattern"
        );

        // "*" must stop at a directory separator.
        expectFalse(
            ignore.isIgnored("logs/nested/build.scratch"),
            "* must not cross a directory separator"
        );

        // "?" must consume exactly one non-separator character.
        expectTrue(
            ignore.isIgnored("file1.cpp"),
            "? should match exactly one character"
        );

        // "?" must not consume multiple characters.
        expectFalse(
            ignore.isIgnored("file10.cpp"),
            "? must not match more than one character"
        );

        // "**/" must be allowed to represent zero intermediate directories.
        expectTrue(
            ignore.isIgnored("src/generated.hpp"),
            "**/ should match zero intermediate directories"
        );

        // "**" must also cross multiple directory components.
        expectTrue(
            ignore.isIgnored("src/a/b/generated.hpp"),
            "** should match multiple intermediate directories"
        );

        // A leading "/" anchors the rule at the project root.
        expectTrue(
            ignore.isIgnored("root-only.cpp"),
            "anchored rule should match at the project root"
        );

        // Root-anchored rules must not match the same filename at deeper levels.
        expectFalse(
            ignore.isIgnored("nested/root-only.cpp"),
            "anchored rule should not match below the project root"
        );

        // Paths matching no rule must remain accessible.
        expectFalse(
            ignore.isIgnored("src/visible.cpp"),
            "unmatched paths should remain visible"
        );
    }


    /**
     * testMcpProjectFileReads()
     * Verifies direct MCP project reads against the same containment, visibility,
     * ignore, symlink, size, and binary policies used by the live MCP server.
     */
    void testMcpProjectFileReads() {
        // Create one isolated project for all direct-read policy cases.
        TemporaryProject project;

        // Visible source file used to verify the normal successful read path.
        project.writeFile(
            "src/visible.cpp",
            "int visible = 42;\n"
        );

        // Visible source file that will be blocked by .gptignore.
        project.writeFile(
            "ignored.cpp",
            "int ignored = 1;\n"
        );

        // Sensitive file used to verify visibility-policy rejection.
        project.writeFile(
            ".env",
            "SECRET=value\n"
        );

        // Visible target used to verify an internal symlink remains accessible.
        project.writeFile(
            "internal-target.cpp",
            "int internalTarget = 7;\n"
        );

        // Visible file containing a NUL byte used to verify binary rejection.
        project.writeFile(
            "binary.cpp",
            std::string("binary\0contents\n", 16)
        );

        // Block only ignored.cpp through the project-local ignore policy.
        project.writeGptIgnore(
            "ignored.cpp\n"
        );

        // Read an ordinary visible source file.
        const McpProjectFileResult normalRead =
            readMcpProjectFile(
                project.path(),
                "src/visible.cpp"
            );

        // A normal visible file should pass every read policy.
        expectTrue(
            normalRead.success,
            "ordinary visible project files should be readable"
        );

        // Successful reads must return the file contents unchanged.
        expectTrue(
            normalRead.text == "int visible = 42;\n",
            "successful reads should return the complete file contents"
        );

        // Attempt to read the visible binary fixture.
        const McpProjectFileResult binaryRead =
            readMcpProjectFile(
                project.path(),
                "binary.cpp"
            );

        // Binary files must not be exposed as MCP text.
        expectFalse(
            binaryRead.success,
            "binary project files should not be directly readable"
        );

        // Binary rejection should report the specific policy that blocked the file.
        expectTrue(
            binaryRead.text ==
                "Requested project file appears to be binary",
            "binary reads should report the binary-file policy failure"
        );

        // Attempt to read a file explicitly excluded by .gptignore.
        const McpProjectFileResult ignoredRead =
            readMcpProjectFile(
                project.path(),
                "ignored.cpp"
            );

        // .gptignore must block otherwise visible files.
        expectFalse(
            ignoredRead.success,
            ".gptignore should block direct project-file reads"
        );

        // Ignore-policy rejection should return the expected diagnostic.
        expectTrue(
            ignoredRead.text ==
                "Requested project file is ignored by .gptignore",
            "ignored reads should report the .gptignore policy failure"
        );

        // Attempt to read a path classified as sensitive.
        const McpProjectFileResult sensitiveRead =
            readMcpProjectFile(
                project.path(),
                ".env"
            );

        // Sensitive paths must not be exposed directly.
        expectFalse(
            sensitiveRead.success,
            "sensitive project files should not be directly readable"
        );

        // Sensitive-path rejection should be reported as an MCP visibility failure.
        expectTrue(
            sensitiveRead.text ==
                "Requested project file is not visible to MCP",
            "sensitive reads should report the visibility failure"
        );

        // Attempt lexical traversal above the registered project root.
        const McpProjectFileResult escapingRead =
            readMcpProjectFile(
                project.path(),
                "../outside.cpp"
            );

        // Parent traversal must be rejected before filesystem access escapes the project.
        expectFalse(
            escapingRead.success,
            "lexical parent traversal should not escape the project"
        );

        // Lexical traversal should report the containment-policy failure.
        expectTrue(
            escapingRead.text ==
                "Requested project path escapes the active project",
            "lexical project escapes should report the containment failure"
        );

        // Store any error produced while creating the internal symlink fixture.
        std::error_code internalSymlinkError;

        // Create a project-local alias pointing to another visible project file.
        std::filesystem::create_symlink(
            project.path() / "internal-target.cpp",
            project.path() / "internal-link.cpp",
            internalSymlinkError
        );

        // Verify the internal symlink fixture was created correctly.
        expectFalse(
            static_cast<bool>(internalSymlinkError),
            "internal symlink fixture should be created successfully"
        );

        // Only test the symlink read when fixture creation succeeded.
        if(!internalSymlinkError) {
            // Read through the project-local symlink alias.
            const McpProjectFileResult internalSymlinkRead =
                readMcpProjectFile(
                    project.path(),
                    "internal-link.cpp"
                );

            // Internal symlinks to visible targets should remain readable.
            expectTrue(
                internalSymlinkRead.success,
                "symlinks to visible files inside the project should be readable"
            );

            // The read should return the resolved target's contents.
            expectTrue(
                internalSymlinkRead.text ==
                    "int internalTarget = 7;\n",
                "internal symlink reads should return the resolved target contents"
            );
        }

        // Create a path beside the temporary project to represent external data.
        const std::filesystem::path externalPath =
            project.path().parent_path() /
            (project.path().filename().string() + "-outside.cpp");

        {
            // Create the file outside the registered project root.
            std::ofstream externalOutput(externalPath);

            // The external-symlink test requires this file to exist.
            if(!externalOutput) {
                throw std::runtime_error(
                    "Failed to create external symlink fixture"
                );
            }

            // Give the external target recognizable source contents.
            externalOutput << "int outside = 9;\n";
        }

        // Store any error produced while creating the escaping symlink.
        std::error_code externalSymlinkError;

        // Create a project-local alias whose target resides outside the project.
        std::filesystem::create_symlink(
            externalPath,
            project.path() / "external-link.cpp",
            externalSymlinkError
        );

        // Verify the external symlink fixture was created.
        expectFalse(
            static_cast<bool>(externalSymlinkError),
            "external symlink fixture should be created successfully"
        );

        // Only test external-target rejection when the symlink exists.
        if(!externalSymlinkError) {
            // Attempt to read through the alias that escapes the project.
            const McpProjectFileResult externalSymlinkRead =
                readMcpProjectFile(
                    project.path(),
                    "external-link.cpp"
                );

            // Resolved symlink targets must remain within the registered project.
            expectFalse(
                externalSymlinkRead.success,
                "symlinks resolving outside the project should be rejected"
            );

            // Symlink escape rejection should report the containment failure.
            expectTrue(
                externalSymlinkRead.text ==
                    "Requested project path escapes the active project",
                "external symlinks should report the containment failure"
            );
        }

        // Remove the external fixture without allowing cleanup errors to throw.
        std::error_code externalRemovalError;
        std::filesystem::remove(
            externalPath,
            externalRemovalError
        );

        // Shared byte count representing the direct-read size boundary.
        constexpr std::uintmax_t oneMiB =
            1024 * 1024;

        // Create a file exactly at the permitted maximum size.
        project.writeLargeFile(
            "exact-limit.cpp",
            oneMiB
        );

        // Attempt to read the boundary-size file.
        const McpProjectFileResult exactLimitRead =
            readMcpProjectFile(
                project.path(),
                "exact-limit.cpp"
            );

        // The limit is inclusive, so exactly 1 MiB must remain readable.
        expectTrue(
            exactLimitRead.success,
            "a file exactly at the 1 MiB limit should remain readable"
        );

        // Create a file one byte beyond the maximum direct-read size.
        project.writeLargeFile(
            "over-limit.cpp",
            oneMiB + 1
        );

        // Attempt to read the oversized fixture.
        const McpProjectFileResult overLimitRead =
            readMcpProjectFile(
                project.path(),
                "over-limit.cpp"
            );

        // Files beyond the read limit must be rejected.
        expectFalse(
            overLimitRead.success,
            "a file larger than 1 MiB should be rejected"
        );

        // Oversized reads should identify the size-limit policy.
        expectTrue(
            overLimitRead.text ==
                "Requested project file exceeds the 1 MiB read limit",
            "oversized reads should report the direct-read size limit"
        );
    }


    /**
     * testMcpProjectFileSearches()
     * Verifies project search filtering, symlink containment, size limits,
     * binary filtering, result formatting, and the maximum-result boundary
     * independently from MCP transport.
     */
    void testMcpProjectFileSearches() {
        // Create one isolated project containing every search-policy fixture.
        TemporaryProject project;

        // First visible text file containing a match on its second line.
        project.writeFile(
            "src/first.cpp",
            "alpha\n"
            "needle first\n"
            "omega\n"
        );

        // Second visible text file proving search traverses multiple files.
        project.writeFile(
            "src/second.cpp",
            "needle second\n"
        );

        // Visible source file that will be removed from search by .gptignore.
        project.writeFile(
            "ignored.cpp",
            "needle ignored\n"
        );

        // Sensitive file containing the query so visibility filtering is exercised.
        project.writeFile(
            ".env",
            "needle sensitive\n"
        );

        // Visible target used for the internal-symlink search case.
        project.writeFile(
            "internal-target.cpp",
            "needle internal\n"
        );

        // Binary file containing the query before a NUL byte.
        project.writeFile(
            "binary.cpp",
            std::string("needle\0binary\n", 14)
        );

        // Hide ignored.cpp while leaving the other fixtures visible.
        project.writeGptIgnore(
            "ignored.cpp\n"
        );

        // Search all eligible project files for the common fixture query.
        const McpProjectFileResult normalSearch =
            searchMcpProjectFiles(
                project.path(),
                "needle"
            );

        // A normal project search should complete successfully.
        expectTrue(
            normalSearch.success,
            "ordinary project searches should succeed"
        );

        // Results must contain the alias path, source line, and matching text.
        expectTrue(
            normalSearch.text.find(
                "src/first.cpp:2: needle first"
            ) != std::string::npos,
            "search should report the alias path, line number, and matching line"
        );

        // Search must continue across more than one eligible file.
        expectTrue(
            normalSearch.text.find(
                "src/second.cpp:1: needle second"
            ) != std::string::npos,
            "search should inspect multiple visible project files"
        );

        // Binary contents must be skipped even when their bytes contain the query.
        expectTrue(
            normalSearch.text.find("binary.cpp") == std::string::npos,
            "binary files should not appear in search results"
        );

        // .gptignore must remove matching files from search results.
        expectTrue(
            normalSearch.text.find("ignored.cpp") == std::string::npos,
            ".gptignore files should not appear in search results"
        );

        // Sensitive files must not appear even when they contain the query.
        expectTrue(
            normalSearch.text.find(".env") == std::string::npos,
            "sensitive files should not appear in search results"
        );

        // Search for text absent from every eligible project file.
        const McpProjectFileResult noMatchSearch =
            searchMcpProjectFiles(
                project.path(),
                "definitely-not-present"
            );

        // No-match searches are valid operations rather than errors.
        expectTrue(
            noMatchSearch.success,
            "a search with no matches should still succeed"
        );

        // The no-match result should use the expected explicit message.
        expectTrue(
            noMatchSearch.text == "No matches found.",
            "a search with no matches should report that explicitly"
        );

        // Store any failure while creating the internal symlink fixture.
        std::error_code internalSymlinkError;

        // Create an alias to a visible target within the project.
        std::filesystem::create_symlink(
            project.path() / "internal-target.cpp",
            project.path() / "internal-link.cpp",
            internalSymlinkError
        );

        // Verify the internal search symlink exists before using it.
        expectFalse(
            static_cast<bool>(internalSymlinkError),
            "internal search symlink fixture should be created successfully"
        );

        // Only test alias-preserving search behavior when symlink creation succeeded.
        if(!internalSymlinkError) {
            // Search for text contained by the internal symlink target.
            const McpProjectFileResult internalSymlinkSearch =
                searchMcpProjectFiles(
                    project.path(),
                    "needle internal"
                );

            // Searching through an internal visible symlink should succeed.
            expectTrue(
                internalSymlinkSearch.success,
                "searching an internal symlink should succeed"
            );

            // Search output should expose the alias path rather than the target path.
            expectTrue(
                internalSymlinkSearch.text.find(
                    "internal-link.cpp:1: needle internal"
                ) != std::string::npos,
                "search results should preserve an internal symlink's alias path"
            );
        }

        // Build an external target beside the temporary project.
        const std::filesystem::path externalPath =
            project.path().parent_path() /
            (project.path().filename().string() + "-search-outside.cpp");

        {
            // Create the out-of-project file used as the escaping symlink target.
            std::ofstream externalOutput(externalPath);

            // The external-symlink search case requires this fixture.
            if(!externalOutput) {
                throw std::runtime_error(
                    "Failed to create external search symlink fixture"
                );
            }

            // Include the search term so an improper traversal would be observable.
            externalOutput << "needle external\n";
        }

        // Store any failure produced while creating the external symlink.
        std::error_code externalSymlinkError;

        // Create a visible-looking project alias to the external target.
        std::filesystem::create_symlink(
            externalPath,
            project.path() / "external-search-link.cpp",
            externalSymlinkError
        );

        // Verify the escaping symlink fixture was successfully created.
        expectFalse(
            static_cast<bool>(externalSymlinkError),
            "external search symlink fixture should be created successfully"
        );

        // Only test containment behavior when the symlink exists.
        if(!externalSymlinkError) {
            // Search specifically for text that exists only outside the project.
            const McpProjectFileResult externalSymlinkSearch =
                searchMcpProjectFiles(
                    project.path(),
                    "needle external"
                );

            // Skipping an escaping symlink must not fail the entire search.
            expectTrue(
                externalSymlinkSearch.success,
                "an external symlink should be skipped without failing the search"
            );

            // The external target must never contribute a search result.
            expectTrue(
                externalSymlinkSearch.text == "No matches found.",
                "search must not inspect a symlink target outside the project"
            );
        }

        // Remove the external fixture without throwing during cleanup.
        std::error_code externalRemovalError;
        std::filesystem::remove(
            externalPath,
            externalRemovalError
        );

        // Define the byte boundary used by the MCP search size policy.
        constexpr std::uintmax_t oneMiB =
            1024 * 1024;

        // Create one searchable-looking file that exceeds the size limit.
        project.writeLargeFile(
            "oversized.cpp",
            oneMiB + 1
        );

        // Search for text absent from all normal files so skip reporting is isolated.
        const McpProjectFileResult oversizedSearch =
            searchMcpProjectFiles(
                project.path(),
                "not-in-normal-files"
            );

        // Oversized files should be skipped without failing the search operation.
        expectTrue(
            oversizedSearch.success,
            "oversized files should be skipped without failing the search"
        );

        // No eligible file contains the query, so the normal no-match message remains.
        expectTrue(
            oversizedSearch.text.find(
                "No matches found."
            ) != std::string::npos,
            "search should still report no matches when an oversized file is skipped"
        );

        // Search should also report how many oversized files were omitted.
        expectTrue(
            oversizedSearch.text.find(
                "1 file was skipped because it exceeds the 1 MiB search limit."
            ) != std::string::npos,
            "search should report one skipped oversized file"
        );

        // Build contents containing one more match than the result limit.
        std::string fiftyOneMatches;

        // Add 51 independently searchable matching lines.
        for(std::size_t index = 0; index < 51; ++index) {
            fiftyOneMatches += "limit-match\n";
        }

        // Store the repeated matches in one visible project file.
        project.writeFile(
            "limit.cpp",
            fiftyOneMatches
        );

        // Search for the repeated term to trigger the result cap.
        const McpProjectFileResult limitedSearch =
            searchMcpProjectFiles(
                project.path(),
                "limit-match"
            );

        // Hitting the result limit must still count as a successful search.
        expectTrue(
            limitedSearch.success,
            "search should succeed when more than the maximum matches exist"
        );

        // The response should explicitly indicate that the limit was reached.
        expectTrue(
            limitedSearch.text.find(
                "Search stopped after 50 matches."
            ) != std::string::npos,
            "search should report when the 50-match result limit is reached"
        );

        // The 51st matching line must never be returned.
        expectTrue(
            limitedSearch.text.find(
                "limit.cpp:51:"
            ) == std::string::npos,
            "search should not return matches beyond the 50-result limit"
        );
    }


    /**
     * testMcpProjectFileListings()
     * Verifies project listing visibility, ignore rules, directory pruning,
     * symlink containment, alias preservation, and stable sorted output.
     */
    void testMcpProjectFileListings() {
        // Create an isolated project containing visible and hidden listing fixtures.
        TemporaryProject project;

        // Visible source file intentionally ordered after alpha lexicographically.
        project.writeFile(
            "src/zeta.cpp",
            "int zeta = 1;\n"
        );

        // Visible source file used with zeta.cpp to verify sorted output.
        project.writeFile(
            "src/alpha.cpp",
            "int alpha = 1;\n"
        );

        // Visible source file that will be excluded by an exact .gptignore rule.
        project.writeFile(
            "ignored.cpp",
            "int ignored = 1;\n"
        );

        // File beneath a directory that will be excluded by .gptignore.
        project.writeFile(
            "ignored-dir/hidden.cpp",
            "int hidden = 1;\n"
        );

        // Source-looking file beneath a sensitive directory.
        project.writeFile(
            ".git/config.cpp",
            "int gitConfig = 1;\n"
        );

        // Generated source file beneath the build directory.
        project.writeFile(
            "build/generated.cpp",
            "int generated = 1;\n"
        );

        // Source file beneath the project cache directory.
        project.writeFile(
            ".cache/cached.cpp",
            "int cached = 1;\n"
        );

        // Unsupported file extension used to verify visibility filtering.
        project.writeFile(
            "notes.txt",
            "not MCP visible\n"
        );

        // Visible target used by the internal-symlink listing case.
        project.writeFile(
            "internal-target.cpp",
            "int internalTarget = 7;\n"
        );

        // Exclude one file and one entire directory from listings.
        project.writeGptIgnore(
            "ignored.cpp\n"
            "ignored-dir/\n"
        );

        // Store any failure while creating the internal symlink fixture.
        std::error_code internalSymlinkError;

        // Create an alias to a visible target inside the project.
        std::filesystem::create_symlink(
            project.path() / "internal-target.cpp",
            project.path() / "internal-link.cpp",
            internalSymlinkError
        );

        // Verify the internal symlink exists before testing listing behavior.
        expectFalse(
            static_cast<bool>(internalSymlinkError),
            "internal listing symlink fixture should be created successfully"
        );

        // Build an external file path beside the temporary project.
        const std::filesystem::path externalPath =
            project.path().parent_path() /
            (project.path().filename().string() + "-list-outside.cpp");

        {
            // Create the out-of-project target used by the escaping symlink.
            std::ofstream externalOutput(externalPath);

            // The external symlink fixture requires a valid target.
            if(!externalOutput) {
                throw std::runtime_error(
                    "Failed to create external listing symlink fixture"
                );
            }

            // Store recognizable source contents in the external target.
            externalOutput << "int outside = 9;\n";
        }

        // Store any error produced while creating the escaping alias.
        std::error_code externalSymlinkError;

        // Create a project-local symlink whose target is outside the project.
        std::filesystem::create_symlink(
            externalPath,
            project.path() / "external-link.cpp",
            externalSymlinkError
        );

        // Verify the external symlink fixture was created.
        expectFalse(
            static_cast<bool>(externalSymlinkError),
            "external listing symlink fixture should be created successfully"
        );

        // Request the complete MCP-visible project listing.
        const McpProjectFileResult listing =
            listMcpProjectFiles(
                project.path()
            );

        // A normal project listing should complete successfully.
        expectTrue(
            listing.success,
            "ordinary project listings should succeed"
        );

        // The first ordinary source fixture must be present.
        expectTrue(
            listing.text.find(
                "src/alpha.cpp\n"
            ) != std::string::npos,
            "visible source files should appear in project listings"
        );

        // The second ordinary source fixture must also be present.
        expectTrue(
            listing.text.find(
                "src/zeta.cpp\n"
            ) != std::string::npos,
            "multiple visible source files should appear in project listings"
        );

        // Exact .gptignore rules must remove matching files.
        expectTrue(
            listing.text.find(
                "ignored.cpp"
            ) == std::string::npos,
            ".gptignore files should not appear in project listings"
        );

        // Ignored directories must be pruned before their descendants are listed.
        expectTrue(
            listing.text.find(
                "ignored-dir/hidden.cpp"
            ) == std::string::npos,
            ".gptignore directories should be pruned from project listings"
        );

        // Sensitive directories must be excluded from traversal.
        expectTrue(
            listing.text.find(
                ".git/config.cpp"
            ) == std::string::npos,
            "sensitive directories should be pruned from project listings"
        );

        // Generated build output must not appear in MCP listings.
        expectTrue(
            listing.text.find(
                "build/generated.cpp"
            ) == std::string::npos,
            "build directories should be pruned from project listings"
        );

        // Cached project data must not appear in MCP listings.
        expectTrue(
            listing.text.find(
                ".cache/cached.cpp"
            ) == std::string::npos,
            "cache directories should be pruned from project listings"
        );

        // Files outside the extension/name visibility allowlist must remain hidden.
        expectTrue(
            listing.text.find(
                "notes.txt"
            ) == std::string::npos,
            "files outside the MCP visibility policy should not be listed"
        );

        // Only inspect internal-symlink output when fixture creation succeeded.
        if(!internalSymlinkError) {
            // Internal symlinks should appear under the alias visible to the client.
            expectTrue(
                listing.text.find(
                    "internal-link.cpp\n"
                ) != std::string::npos,
                "internal file symlinks should be listed using their alias path"
            );
        }

        // Only inspect external-symlink output when fixture creation succeeded.
        if(!externalSymlinkError) {
            // Symlinks resolving outside the project must not be exposed.
            expectTrue(
                listing.text.find(
                    "external-link.cpp"
                ) == std::string::npos,
                "symlinks resolving outside the project should not be listed"
            );
        }

        // Locate alpha.cpp in the returned listing.
        const std::size_t alphaPosition =
            listing.text.find("src/alpha.cpp\n");

        // Locate zeta.cpp in the returned listing.
        const std::size_t zetaPosition =
            listing.text.find("src/zeta.cpp\n");

        // Both files must exist and alpha.cpp must appear first lexicographically.
        expectTrue(
            alphaPosition != std::string::npos &&
            zetaPosition != std::string::npos &&
            alphaPosition < zetaPosition,
            "project listings should be sorted lexicographically"
        );

        // Remove the external fixture without throwing during test cleanup.
        std::error_code externalRemovalError;
        std::filesystem::remove(
            externalPath,
            externalRemovalError
        );
    }

}


/**
 * main()
 * Runs every MCP filesystem policy regression group and reports one combined
 * process result.
 */
int main() {
    try {
        // Verify sensitive-path classification.
        testSensitivePaths();

        // Verify MCP file visibility rules.
        testProjectVisibility();

        // Verify supported .gptignore matching behavior.
        testGptIgnorePatterns();

        // Verify direct project-file read policy.
        testMcpProjectFileReads();

        // Verify project-file search policy.
        testMcpProjectFileSearches();

        // Verify project-file listing policy.
        testMcpProjectFileListings();
    }
    catch(const std::exception& error) {
        // Unexpected fixture/setup failures abort the test executable.
        std::cerr << "ERROR: " << error.what() << '\n';
        return 1;
    }

    // Any failed expectation makes the complete test executable fail.
    if(failureCount != 0) {
        std::cerr << failureCount << " test(s) failed\n";
        return 1;
    }

    // Reaching this point means every expectation succeeded.
    std::cout << "All MCP filesystem policy tests passed\n";
    return 0;
}
