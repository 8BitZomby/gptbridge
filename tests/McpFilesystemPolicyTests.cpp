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

    int failureCount = 0;


    /**
     * expectTrue()
     * Records a failed boolean expectation while allowing the remaining
     * regression cases to continue running.
     */
    void expectTrue(bool condition, const std::string& description) {
        if(condition) {
            return;
        }

        std::cerr << "FAIL: " << description << '\n';
        ++failureCount;
    }


    /**
     * expectFalse()
     * Convenience wrapper for expectations whose correct result is false.
     */
    void expectFalse(bool condition, const std::string& description) {
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
                const std::filesystem::path temporaryRoot = std::filesystem::temp_directory_path();

                const auto timestamp = std::filesystem::file_time_type::clock::now()
                    .time_since_epoch()
                    .count();

                projectPath =
                    temporaryRoot /
                    std::filesystem::path(
                        "gptbridge-mcp-policy-tests-" +
                        std::to_string(
                            static_cast<long long>(timestamp)
                        )
                    );

                std::filesystem::create_directories(projectPath);
            }


            ~TemporaryProject() {
                std::error_code removalError;
                std::filesystem::remove_all(projectPath, removalError);
            }


            const std::filesystem::path& path() const {
                return projectPath;
            }


            void writeGptIgnore(const std::string& contents) const {
                std::ofstream output(projectPath / ".gptignore");
                if(!output) {
                    throw std::runtime_error(
                        "Failed to create temporary .gptignore"
                    );
                }
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

                const std::filesystem::path filePath =
                    projectPath / relativePath;

                if(filePath.has_parent_path()) {
                    std::filesystem::create_directories(
                        filePath.parent_path()
                    );
                }

                std::ofstream output(
                    filePath,
                    std::ios::binary
                );

                if(!output) {
                    throw std::runtime_error(
                        "Failed to create temporary project file"
                    );
                }

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

                const std::filesystem::path filePath =
                    projectPath / relativePath;

                if(filePath.has_parent_path()) {
                    std::filesystem::create_directories(
                        filePath.parent_path()
                    );
                }

                std::ofstream output(
                    filePath,
                    std::ios::binary
                );

                if(!output) {
                    throw std::runtime_error(
                        "Failed to create temporary large project file"
                    );
                }

                for(std::uintmax_t index = 0; index < size; ++index) {
                    output.put('A');
                }
            }

        private:
            std::filesystem::path projectPath;
    };


    void testSensitivePaths() {
        expectTrue(
            isSensitiveProjectPath(".env"),
            ".env should be sensitive"
        );

        expectTrue(
            isSensitiveProjectPath(".env.production"),
            ".env variants should be sensitive"
        );

        expectTrue(
            isSensitiveProjectPath("config/id_rsa"),
            "SSH private-key filenames should be sensitive"
        );

        expectTrue(
            isSensitiveProjectPath("config/server.pem"),
            "private-key/certificate extensions should be sensitive"
        );

        expectTrue(
            isSensitiveProjectPath("config/secrets/token.txt"),
            "files below sensitive directories should be sensitive"
        );

        expectTrue(
            isSensitiveProjectPath(".git/config"),
            ".git contents should be sensitive"
        );

        expectFalse(
            isSensitiveProjectPath("src/main.cpp"),
            "ordinary source files should not be sensitive"
        );
    }


    void testProjectVisibility() {
        expectTrue(
            isProjectPathVisible("src/main.cpp"),
            ".cpp files should be MCP-visible"
        );

        expectTrue(
            isProjectPathVisible("include/example.hpp"),
            ".hpp files should be MCP-visible"
        );

        expectTrue(
            isProjectPathVisible("README.md"),
            "README.md should be MCP-visible"
        );

        expectTrue(
            isProjectPathVisible("cmake/generated.hpp.in"),
            ".hpp.in templates should be MCP-visible"
        );

        expectFalse(
            isProjectPathVisible("notes.txt"),
            "unknown file extensions should remain hidden"
        );

        expectFalse(
            isProjectPathVisible(".env"),
            "sensitive files must remain hidden regardless of extension policy"
        );

        expectFalse(
            isProjectPathVisible(".git/config.cpp"),
            "source-looking files inside sensitive directories must remain hidden"
        );
    }


    void testGptIgnorePatterns() {
        TemporaryProject project;

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

        const GptIgnore ignore(project.path());

        expectTrue(
            ignore.isIgnored("generated.cpp"),
            "exact filename rule should match"
        );

        expectTrue(
            ignore.isIgnored("nested/generated.cpp"),
            "unanchored filename rule should match at any depth"
        );

        expectTrue(
            ignore.isIgnored("cache", true),
            "directory rule should match the directory itself"
        );

        expectTrue(
            ignore.isIgnored("cache/data.cpp"),
            "directory rule should hide descendants"
        );

        expectTrue(
            ignore.isIgnored("nested/cache/data.cpp"),
            "unanchored directory rule should match at any depth"
        );

        expectTrue(
            ignore.isIgnored("scratch.tmp"),
            "* should match within one path component"
        );

        expectTrue(
            ignore.isIgnored("nested/scratch.tmp"),
            "unanchored * filename rule should match at any depth"
        );

        expectTrue(
            ignore.isIgnored("logs/build.scratch"),
            "* should match within one path component of a path pattern"
        );

        expectFalse(
            ignore.isIgnored("logs/nested/build.scratch"),
            "* must not cross a directory separator"
        );

        expectTrue(
            ignore.isIgnored("file1.cpp"),
            "? should match exactly one character"
        );

        expectFalse(
            ignore.isIgnored("file10.cpp"),
            "? must not match more than one character"
        );

        expectTrue(
            ignore.isIgnored("src/generated.hpp"),
            "**/ should match zero intermediate directories"
        );

        expectTrue(
            ignore.isIgnored("src/a/b/generated.hpp"),
            "** should match multiple intermediate directories"
        );

        expectTrue(
            ignore.isIgnored("root-only.cpp"),
            "anchored rule should match at the project root"
        );

        expectFalse(
            ignore.isIgnored("nested/root-only.cpp"),
            "anchored rule should not match below the project root"
        );

        expectFalse(
            ignore.isIgnored("src/visible.cpp"),
            "unmatched paths should remain visible"
        );
    }

    /**
     * testMcpProjectFileReads()
     * Verifies direct MCP project reads against the same containment, visibility,
     * ignore, symlink, and size policies used by the live MCP server.
     */
    void testMcpProjectFileReads() {
        TemporaryProject project;

        project.writeFile(
            "src/visible.cpp",
            "int visible = 42;\n"
        );

        project.writeFile(
            "ignored.cpp",
            "int ignored = 1;\n"
        );

        project.writeFile(
            ".env",
            "SECRET=value\n"
        );

        project.writeFile(
            "internal-target.cpp",
            "int internalTarget = 7;\n"
        );

        // Create a visible file containing a NUL byte so it is classified as binary
        project.writeFile(
            "binary.cpp",
            std::string("binary\0contents\n", 16)
        );

        project.writeGptIgnore(
            "ignored.cpp\n"
        );

        const McpProjectFileResult normalRead =
            readMcpProjectFile(
                project.path(),
                "src/visible.cpp"
            );

        expectTrue(
            normalRead.success,
            "ordinary visible project files should be readable"
        );

        expectTrue(
            normalRead.text == "int visible = 42;\n",
            "successful reads should return the complete file contents"
        );

        // Attempt to read the visible binary file through the MCP filesystem policy
        const McpProjectFileResult binaryRead =
            readMcpProjectFile(
                project.path(),
                "binary.cpp"
            );

        // Binary files must be rejected instead of returned as MCP text
        expectFalse(
            binaryRead.success,
            "binary project files should not be directly readable"
        );

        // The rejection should identify the binary-file policy specifically
        expectTrue(
            binaryRead.text ==
                "Requested project file appears to be binary",
            "binary reads should report the binary-file policy failure"
        );

        const McpProjectFileResult ignoredRead =
            readMcpProjectFile(
                project.path(),
                "ignored.cpp"
            );

        expectFalse(
            ignoredRead.success,
            ".gptignore should block direct project-file reads"
        );

        expectTrue(
            ignoredRead.text ==
                "Requested project file is ignored by .gptignore",
            "ignored reads should report the .gptignore policy failure"
        );

        const McpProjectFileResult sensitiveRead =
            readMcpProjectFile(
                project.path(),
                ".env"
            );

        expectFalse(
            sensitiveRead.success,
            "sensitive project files should not be directly readable"
        );

        expectTrue(
            sensitiveRead.text ==
                "Requested project file is not visible to MCP",
            "sensitive reads should report the visibility failure"
        );

        const McpProjectFileResult escapingRead =
            readMcpProjectFile(
                project.path(),
                "../outside.cpp"
            );

        expectFalse(
            escapingRead.success,
            "lexical parent traversal should not escape the project"
        );

        expectTrue(
            escapingRead.text ==
                "Requested project path escapes the active project",
            "lexical project escapes should report the containment failure"
        );

        std::error_code internalSymlinkError;
        std::filesystem::create_symlink(
            project.path() / "internal-target.cpp",
            project.path() / "internal-link.cpp",
            internalSymlinkError
        );

        expectFalse(
            static_cast<bool>(internalSymlinkError),
            "internal symlink fixture should be created successfully"
        );

        if(!internalSymlinkError) {
            const McpProjectFileResult internalSymlinkRead =
                readMcpProjectFile(
                    project.path(),
                    "internal-link.cpp"
                );

            expectTrue(
                internalSymlinkRead.success,
                "symlinks to visible files inside the project should be readable"
            );

            expectTrue(
                internalSymlinkRead.text ==
                    "int internalTarget = 7;\n",
                "internal symlink reads should return the resolved target contents"
            );
        }

        const std::filesystem::path externalPath =
            project.path().parent_path() /
            (project.path().filename().string() + "-outside.cpp");

        {
            std::ofstream externalOutput(externalPath);

            if(!externalOutput) {
                throw std::runtime_error(
                    "Failed to create external symlink fixture"
                );
            }

            externalOutput << "int outside = 9;\n";
        }

        std::error_code externalSymlinkError;
        std::filesystem::create_symlink(
            externalPath,
            project.path() / "external-link.cpp",
            externalSymlinkError
        );

        expectFalse(
            static_cast<bool>(externalSymlinkError),
            "external symlink fixture should be created successfully"
        );

        if(!externalSymlinkError) {
            const McpProjectFileResult externalSymlinkRead =
                readMcpProjectFile(
                    project.path(),
                    "external-link.cpp"
                );

            expectFalse(
                externalSymlinkRead.success,
                "symlinks resolving outside the project should be rejected"
            );

            expectTrue(
                externalSymlinkRead.text ==
                    "Requested project path escapes the active project",
                "external symlinks should report the containment failure"
            );
        }

        std::error_code externalRemovalError;
        std::filesystem::remove(
            externalPath,
            externalRemovalError
        );

        constexpr std::uintmax_t oneMiB =
            1024 * 1024;

        project.writeLargeFile(
            "exact-limit.cpp",
            oneMiB
        );

        const McpProjectFileResult exactLimitRead =
            readMcpProjectFile(
                project.path(),
                "exact-limit.cpp"
            );

        expectTrue(
            exactLimitRead.success,
            "a file exactly at the 1 MiB limit should remain readable"
        );

        project.writeLargeFile(
            "over-limit.cpp",
            oneMiB + 1
        );

        const McpProjectFileResult overLimitRead =
            readMcpProjectFile(
                project.path(),
                "over-limit.cpp"
            );

        expectFalse(
            overLimitRead.success,
            "a file larger than 1 MiB should be rejected"
        );

        expectTrue(
            overLimitRead.text ==
                "Requested project file exceeds the 1 MiB read limit",
            "oversized reads should report the direct-read size limit"
        );
    }


    /**
     * testMcpProjectFileSearches()
     * Verifies project search filtering, symlink containment, size limits, result
     * formatting, and the maximum-result boundary independently from MCP transport.
     */
    void testMcpProjectFileSearches() {
        TemporaryProject project;

        project.writeFile(
            "src/first.cpp",
            "alpha\n"
            "needle first\n"
            "omega\n"
        );

        project.writeFile(
            "src/second.cpp",
            "needle second\n"
        );

        project.writeFile(
            "ignored.cpp",
            "needle ignored\n"
        );

        project.writeFile(
            ".env",
            "needle sensitive\n"
        );

        project.writeFile(
            "internal-target.cpp",
            "needle internal\n"
        );

        // Create a binary file containing the search term before its NUL byte.
        // This confirms search skips the whole file rather than matching its text-like bytes
        project.writeFile(
            "binary.cpp",
            std::string("needle\0binary\n", 14)
        );

        project.writeGptIgnore(
            "ignored.cpp\n"
        );

        const McpProjectFileResult normalSearch =
            searchMcpProjectFiles(
                project.path(),
                "needle"
            );

        expectTrue(
            normalSearch.success,
            "ordinary project searches should succeed"
        );

        expectTrue(
            normalSearch.text.find(
                "src/first.cpp:2: needle first"
            ) != std::string::npos,
            "search should report the alias path, line number, and matching line"
        );

        expectTrue(
            normalSearch.text.find(
                "src/second.cpp:1: needle second"
            ) != std::string::npos,
            "search should inspect multiple visible project files"
        );

        // The binary fixture contains "needle", but it must not appear in search results
        expectTrue(
            normalSearch.text.find("binary.cpp") == std::string::npos,
            "binary files should not appear in search results"
        );

        expectTrue(
            normalSearch.text.find("ignored.cpp") == std::string::npos,
            ".gptignore files should not appear in search results"
        );

        expectTrue(
            normalSearch.text.find(".env") == std::string::npos,
            "sensitive files should not appear in search results"
        );

        const McpProjectFileResult noMatchSearch =
            searchMcpProjectFiles(
                project.path(),
                "definitely-not-present"
            );

        expectTrue(
            noMatchSearch.success,
            "a search with no matches should still succeed"
        );

        expectTrue(
            noMatchSearch.text == "No matches found.",
            "a search with no matches should report that explicitly"
        );

        std::error_code internalSymlinkError;
        std::filesystem::create_symlink(
            project.path() / "internal-target.cpp",
            project.path() / "internal-link.cpp",
            internalSymlinkError
        );

        expectFalse(
            static_cast<bool>(internalSymlinkError),
            "internal search symlink fixture should be created successfully"
        );

        if(!internalSymlinkError) {
            const McpProjectFileResult internalSymlinkSearch =
                searchMcpProjectFiles(
                    project.path(),
                    "needle internal"
                );

            expectTrue(
                internalSymlinkSearch.success,
                "searching an internal symlink should succeed"
            );

            expectTrue(
                internalSymlinkSearch.text.find(
                    "internal-link.cpp:1: needle internal"
                ) != std::string::npos,
                "search results should preserve an internal symlink's alias path"
            );
        }

        const std::filesystem::path externalPath =
            project.path().parent_path() /
            (project.path().filename().string() + "-search-outside.cpp");

        {
            std::ofstream externalOutput(externalPath);

            if(!externalOutput) {
                throw std::runtime_error(
                    "Failed to create external search symlink fixture"
                );
            }

            externalOutput << "needle external\n";
        }

        std::error_code externalSymlinkError;
        std::filesystem::create_symlink(
            externalPath,
            project.path() / "external-search-link.cpp",
            externalSymlinkError
        );

        expectFalse(
            static_cast<bool>(externalSymlinkError),
            "external search symlink fixture should be created successfully"
        );

        if(!externalSymlinkError) {
            const McpProjectFileResult externalSymlinkSearch =
                searchMcpProjectFiles(
                    project.path(),
                    "needle external"
                );

            expectTrue(
                externalSymlinkSearch.success,
                "an external symlink should be skipped without failing the search"
            );

            expectTrue(
                externalSymlinkSearch.text == "No matches found.",
                "search must not inspect a symlink target outside the project"
            );
        }

        std::error_code externalRemovalError;
        std::filesystem::remove(
            externalPath,
            externalRemovalError
        );

        constexpr std::uintmax_t oneMiB =
            1024 * 1024;

        project.writeLargeFile(
            "oversized.cpp",
            oneMiB + 1
        );

        const McpProjectFileResult oversizedSearch =
            searchMcpProjectFiles(
                project.path(),
                "not-in-normal-files"
            );

        expectTrue(
            oversizedSearch.success,
            "oversized files should be skipped without failing the search"
        );

        expectTrue(
            oversizedSearch.text.find(
                "No matches found."
            ) != std::string::npos,
            "search should still report no matches when an oversized file is skipped"
        );

        expectTrue(
            oversizedSearch.text.find(
                "1 file was skipped because it exceeds the 1 MiB search limit."
            ) != std::string::npos,
            "search should report one skipped oversized file"
        );

        std::string fiftyOneMatches;

        for(std::size_t index = 0; index < 51; ++index) {
            fiftyOneMatches += "limit-match\n";
        }

        project.writeFile(
            "limit.cpp",
            fiftyOneMatches
        );

        const McpProjectFileResult limitedSearch =
            searchMcpProjectFiles(
                project.path(),
                "limit-match"
            );

        expectTrue(
            limitedSearch.success,
            "search should succeed when more than the maximum matches exist"
        );

        expectTrue(
            limitedSearch.text.find(
                "Search stopped after 50 matches."
            ) != std::string::npos,
            "search should report when the 50-match result limit is reached"
        );

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
        TemporaryProject project;

        project.writeFile(
            "src/zeta.cpp",
            "int zeta = 1;\n"
        );

        project.writeFile(
            "src/alpha.cpp",
            "int alpha = 1;\n"
        );

        project.writeFile(
            "ignored.cpp",
            "int ignored = 1;\n"
        );

        project.writeFile(
            "ignored-dir/hidden.cpp",
            "int hidden = 1;\n"
        );

        project.writeFile(
            ".git/config.cpp",
            "int gitConfig = 1;\n"
        );

        project.writeFile(
            "build/generated.cpp",
            "int generated = 1;\n"
        );

        project.writeFile(
            ".cache/cached.cpp",
            "int cached = 1;\n"
        );

        project.writeFile(
            "notes.txt",
            "not MCP visible\n"
        );

        project.writeFile(
            "internal-target.cpp",
            "int internalTarget = 7;\n"
        );

        project.writeGptIgnore(
            "ignored.cpp\n"
            "ignored-dir/\n"
        );

        std::error_code internalSymlinkError;
        std::filesystem::create_symlink(
            project.path() / "internal-target.cpp",
            project.path() / "internal-link.cpp",
            internalSymlinkError
        );

        expectFalse(
            static_cast<bool>(internalSymlinkError),
            "internal listing symlink fixture should be created successfully"
        );

        const std::filesystem::path externalPath =
            project.path().parent_path() /
            (project.path().filename().string() + "-list-outside.cpp");

        {
            std::ofstream externalOutput(externalPath);

            if(!externalOutput) {
                throw std::runtime_error(
                    "Failed to create external listing symlink fixture"
                );
            }

            externalOutput << "int outside = 9;\n";
        }

        std::error_code externalSymlinkError;
        std::filesystem::create_symlink(
            externalPath,
            project.path() / "external-link.cpp",
            externalSymlinkError
        );

        expectFalse(
            static_cast<bool>(externalSymlinkError),
            "external listing symlink fixture should be created successfully"
        );

        const McpProjectFileResult listing =
            listMcpProjectFiles(
                project.path()
            );

        expectTrue(
            listing.success,
            "ordinary project listings should succeed"
        );

        expectTrue(
            listing.text.find(
                "src/alpha.cpp\n"
            ) != std::string::npos,
            "visible source files should appear in project listings"
        );

        expectTrue(
            listing.text.find(
                "src/zeta.cpp\n"
            ) != std::string::npos,
            "multiple visible source files should appear in project listings"
        );

        expectTrue(
            listing.text.find(
                "ignored.cpp"
            ) == std::string::npos,
            ".gptignore files should not appear in project listings"
        );

        expectTrue(
            listing.text.find(
                "ignored-dir/hidden.cpp"
            ) == std::string::npos,
            ".gptignore directories should be pruned from project listings"
        );

        expectTrue(
            listing.text.find(
                ".git/config.cpp"
            ) == std::string::npos,
            "sensitive directories should be pruned from project listings"
        );

        expectTrue(
            listing.text.find(
                "build/generated.cpp"
            ) == std::string::npos,
            "build directories should be pruned from project listings"
        );

        expectTrue(
            listing.text.find(
                ".cache/cached.cpp"
            ) == std::string::npos,
            "cache directories should be pruned from project listings"
        );

        expectTrue(
            listing.text.find(
                "notes.txt"
            ) == std::string::npos,
            "files outside the MCP visibility policy should not be listed"
        );

        if(!internalSymlinkError) {
            expectTrue(
                listing.text.find(
                    "internal-link.cpp\n"
                ) != std::string::npos,
                "internal file symlinks should be listed using their alias path"
            );
        }

        if(!externalSymlinkError) {
            expectTrue(
                listing.text.find(
                    "external-link.cpp"
                ) == std::string::npos,
                "symlinks resolving outside the project should not be listed"
            );
        }

        const std::size_t alphaPosition =
            listing.text.find("src/alpha.cpp\n");

        const std::size_t zetaPosition =
            listing.text.find("src/zeta.cpp\n");

        expectTrue(
            alphaPosition != std::string::npos &&
            zetaPosition != std::string::npos &&
            alphaPosition < zetaPosition,
            "project listings should be sorted lexicographically"
        );

        std::error_code externalRemovalError;
        std::filesystem::remove(
            externalPath,
            externalRemovalError
        );
    }
}


int main() {
    try {
        testSensitivePaths();
        testProjectVisibility();
        testGptIgnorePatterns();
        testMcpProjectFileReads();
        testMcpProjectFileSearches();
        testMcpProjectFileListings();
    }
    catch(const std::exception& error) {
        std::cerr << "ERROR: " << error.what() << '\n';
        return 1;
    }

    if(failureCount != 0) {
        std::cerr << failureCount << " test(s) failed\n";
        return 1;
    }

    std::cout << "All MCP filesystem policy tests passed\n";
    return 0;
}
