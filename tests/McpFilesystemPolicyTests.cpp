#include "GptIgnore.hpp"
#include "ProjectVisibility.hpp"
#include "SensitivePath.hpp"

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

}


int main() {
    try {
        testSensitivePaths();
        testProjectVisibility();
        testGptIgnorePatterns();
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
