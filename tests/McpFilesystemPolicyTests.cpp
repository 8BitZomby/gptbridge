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
}


int main() {
    try {
        testSensitivePaths();
        testProjectVisibility();
        testGptIgnorePatterns();
        testMcpProjectFileReads();
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
