#include <gtest/gtest.h>
#include "CommandResolver.hpp"

#include <filesystem>
#include <fstream>
#include <string>
#include <vector>
#include <windows.h>
#include <shellapi.h>

// ============================================================================
// Fixture: creates a temporary executable so that resolve() succeeds and
// we can inspect the generated commandLine for escaping correctness.
// ============================================================================

class ArgumentEscapingTest : public ::testing::Test {
protected:
    std::filesystem::path m_origDir;
    std::filesystem::path m_testDir;
    std::wstring m_origPath;
    std::wstring m_origPathext;

    void SetUp() override {
        m_origDir = std::filesystem::current_path();
        m_testDir = std::filesystem::temp_directory_path() / "argesc_test";
        std::filesystem::create_directories(m_testDir);

        m_origPath = getEnv(L"PATH");
        m_origPathext = getEnv(L"PATHEXT");

        // Create a fake .exe and .bat for testing
        createFile(m_testDir, "testapp.exe");
        createFile(m_testDir, "script.bat");
        createFile(m_testDir, "script.cmd");

        // Set PATH to our test directory
        SetEnvironmentVariableW(L"PATH", m_testDir.wstring().c_str());
        SetEnvironmentVariableW(L"PATHEXT", L".EXE;.BAT;.CMD");
        SetEnvironmentVariableW(L"TERMINAL_ALLOW_CWD_EXEC", L"1");

        // Move CWD away so it doesn't interfere
        std::filesystem::current_path(m_testDir);
    }

    void TearDown() override {
        std::filesystem::current_path(m_origDir);
        std::filesystem::remove_all(m_testDir);

        SetEnvironmentVariableW(L"PATH", m_origPath.c_str());
        if (!m_origPathext.empty())
            SetEnvironmentVariableW(L"PATHEXT", m_origPathext.c_str());
        else
            SetEnvironmentVariableW(L"PATHEXT", nullptr);
        SetEnvironmentVariableW(L"TERMINAL_ALLOW_CWD_EXEC", nullptr);
    }

    void createFile(const std::filesystem::path& dir, const std::string& name) {
        std::filesystem::create_directories(dir);
        std::ofstream ofs(dir / name);
        ofs << "fake";
    }

    static std::wstring getEnv(const wchar_t* varName) {
        DWORD len = GetEnvironmentVariableW(varName, nullptr, 0);
        if (len == 0) return {};
        std::wstring buf(len, L'\0');
        DWORD actual = GetEnvironmentVariableW(varName, buf.data(), len);
        if (actual == 0 || actual >= len) return {};
        buf.resize(actual);
        return buf;
    }

    // Helper: resolve "testapp" with given args and return the commandLine
    std::wstring resolveCommandLine(const std::vector<std::string>& args) {
        auto result = CommandResolver::resolve(args[0], args);
        EXPECT_TRUE(result.has_value()) << "resolve() failed for: " << args[0];
        if (!result.has_value()) return {};
        return result->commandLine;
    }

    // Helper: parse a command line with CommandLineToArgvW and return the argv
    // as a vector of wide strings.
    std::vector<std::wstring> parseCommandLine(const std::wstring& cmdLine) {
        int argc = 0;
        LPWSTR* argv = CommandLineToArgvW(cmdLine.c_str(), &argc);
        if (!argv) return {};

        std::vector<std::wstring> result;
        result.reserve(argc);
        for (int i = 0; i < argc; ++i) {
            result.emplace_back(argv[i]);
        }
        LocalFree(argv);
        return result;
    }

    // Helper: convert UTF-8 string to wide string for comparison
    static std::wstring widen(const std::string& utf8) {
        if (utf8.empty()) return {};
        int len = MultiByteToWideChar(CP_UTF8, 0, utf8.data(),
                                      static_cast<int>(utf8.size()), nullptr, 0);
        if (len <= 0) return {};
        std::wstring result(len, L'\0');
        MultiByteToWideChar(CP_UTF8, 0, utf8.data(),
                            static_cast<int>(utf8.size()), result.data(), len);
        return result;
    }
};

// ============================================================================
// Test: argument with embedded double quotes
// ============================================================================

TEST_F(ArgumentEscapingTest, EmbeddedDoubleQuotes) {
    std::vector<std::string> args = {"testapp", "say \"hello\" world"};
    std::wstring cmdLine = resolveCommandLine(args);
    ASSERT_FALSE(cmdLine.empty());

    auto argv = parseCommandLine(cmdLine);
    ASSERT_GE(argv.size(), 2u);
    EXPECT_EQ(argv[1], L"say \"hello\" world");
}

TEST_F(ArgumentEscapingTest, OnlyDoubleQuotes) {
    std::vector<std::string> args = {"testapp", "\""};
    std::wstring cmdLine = resolveCommandLine(args);
    ASSERT_FALSE(cmdLine.empty());

    auto argv = parseCommandLine(cmdLine);
    ASSERT_GE(argv.size(), 2u);
    EXPECT_EQ(argv[1], L"\"");
}

TEST_F(ArgumentEscapingTest, MultipleConsecutiveQuotes) {
    std::vector<std::string> args = {"testapp", "a\"\"b"};
    std::wstring cmdLine = resolveCommandLine(args);
    ASSERT_FALSE(cmdLine.empty());

    auto argv = parseCommandLine(cmdLine);
    ASSERT_GE(argv.size(), 2u);
    EXPECT_EQ(argv[1], L"a\"\"b");
}

// ============================================================================
// Test: argument with trailing backslashes
// ============================================================================

TEST_F(ArgumentEscapingTest, TrailingBackslashes) {
    // Trailing backslashes before the closing quote need to be doubled
    std::vector<std::string> args = {"testapp", "path\\to\\dir\\"};
    std::wstring cmdLine = resolveCommandLine(args);
    ASSERT_FALSE(cmdLine.empty());

    auto argv = parseCommandLine(cmdLine);
    ASSERT_GE(argv.size(), 2u);
    EXPECT_EQ(argv[1], L"path\\to\\dir\\");
}

TEST_F(ArgumentEscapingTest, MultipleTrailingBackslashes) {
    std::vector<std::string> args = {"testapp", "dir\\\\"};
    std::wstring cmdLine = resolveCommandLine(args);
    ASSERT_FALSE(cmdLine.empty());

    auto argv = parseCommandLine(cmdLine);
    ASSERT_GE(argv.size(), 2u);
    EXPECT_EQ(argv[1], L"dir\\\\");
}

TEST_F(ArgumentEscapingTest, BackslashesBeforeQuote) {
    // Backslashes immediately before an embedded quote
    std::vector<std::string> args = {"testapp", "path\\\"quoted"};
    std::wstring cmdLine = resolveCommandLine(args);
    ASSERT_FALSE(cmdLine.empty());

    auto argv = parseCommandLine(cmdLine);
    ASSERT_GE(argv.size(), 2u);
    EXPECT_EQ(argv[1], L"path\\\"quoted");
}

// ============================================================================
// Test: argument with spaces
// ============================================================================

TEST_F(ArgumentEscapingTest, ArgumentWithSpaces) {
    std::vector<std::string> args = {"testapp", "hello world"};
    std::wstring cmdLine = resolveCommandLine(args);
    ASSERT_FALSE(cmdLine.empty());

    auto argv = parseCommandLine(cmdLine);
    ASSERT_GE(argv.size(), 2u);
    EXPECT_EQ(argv[1], L"hello world");
}

TEST_F(ArgumentEscapingTest, ArgumentWithTabs) {
    std::vector<std::string> args = {"testapp", "hello\tworld"};
    std::wstring cmdLine = resolveCommandLine(args);
    ASSERT_FALSE(cmdLine.empty());

    auto argv = parseCommandLine(cmdLine);
    ASSERT_GE(argv.size(), 2u);
    EXPECT_EQ(argv[1], L"hello\tworld");
}

TEST_F(ArgumentEscapingTest, ArgumentWithNoSpecialChars) {
    // Simple arg without spaces/quotes should pass through as-is
    std::vector<std::string> args = {"testapp", "simple"};
    std::wstring cmdLine = resolveCommandLine(args);
    ASSERT_FALSE(cmdLine.empty());

    auto argv = parseCommandLine(cmdLine);
    ASSERT_GE(argv.size(), 2u);
    EXPECT_EQ(argv[1], L"simple");
}

// ============================================================================
// Test: empty argument produces ""
// ============================================================================

TEST_F(ArgumentEscapingTest, EmptyArgumentProducesQuotedEmpty) {
    std::vector<std::string> args = {"testapp", ""};
    std::wstring cmdLine = resolveCommandLine(args);
    ASSERT_FALSE(cmdLine.empty());

    // The command line should contain "" for the empty argument
    // CommandLineToArgvW should parse it as an empty string
    auto argv = parseCommandLine(cmdLine);
    ASSERT_GE(argv.size(), 2u);
    EXPECT_EQ(argv[1], L"");
}

// ============================================================================
// Test: cmd.exe metacharacter escaping for .bat/.cmd targets
// ============================================================================

TEST_F(ArgumentEscapingTest, BatchFileMetacharacterEscaping) {
    // When the target is a .bat file, metacharacters should be ^-escaped
    std::vector<std::string> args = {"script.bat", "echo & dir"};
    std::wstring cmdLine = resolveCommandLine(args);
    ASSERT_FALSE(cmdLine.empty());

    // The command line should contain ^ before & for .bat targets
    // Check that the raw command line contains ^& (the ^ escaping)
    EXPECT_NE(cmdLine.find(L"^&"), std::wstring::npos)
        << "Expected ^& in command line for .bat target";
}

TEST_F(ArgumentEscapingTest, BatchFilePipeMetacharacter) {
    std::vector<std::string> args = {"script.cmd", "a|b"};
    std::wstring cmdLine = resolveCommandLine(args);
    ASSERT_FALSE(cmdLine.empty());

    // Should contain ^| for the pipe metacharacter
    EXPECT_NE(cmdLine.find(L"^|"), std::wstring::npos)
        << "Expected ^| in command line for .cmd target";
}

TEST_F(ArgumentEscapingTest, BatchFileCaretEscaping) {
    std::vector<std::string> args = {"script.bat", "hello^world"};
    std::wstring cmdLine = resolveCommandLine(args);
    ASSERT_FALSE(cmdLine.empty());

    // The ^ itself should be escaped as ^^
    EXPECT_NE(cmdLine.find(L"^^"), std::wstring::npos)
        << "Expected ^^ in command line for .bat target";
}

TEST_F(ArgumentEscapingTest, BatchFileAngleBrackets) {
    std::vector<std::string> args = {"script.cmd", "a<b>c"};
    std::wstring cmdLine = resolveCommandLine(args);
    ASSERT_FALSE(cmdLine.empty());

    EXPECT_NE(cmdLine.find(L"^<"), std::wstring::npos)
        << "Expected ^< in command line for .cmd target";
    EXPECT_NE(cmdLine.find(L"^>"), std::wstring::npos)
        << "Expected ^> in command line for .cmd target";
}

TEST_F(ArgumentEscapingTest, BatchFileParentheses) {
    std::vector<std::string> args = {"script.bat", "(foo)"};
    std::wstring cmdLine = resolveCommandLine(args);
    ASSERT_FALSE(cmdLine.empty());

    EXPECT_NE(cmdLine.find(L"^("), std::wstring::npos)
        << "Expected ^( in command line for .bat target";
    EXPECT_NE(cmdLine.find(L"^)"), std::wstring::npos)
        << "Expected ^) in command line for .bat target";
}

TEST_F(ArgumentEscapingTest, NonBatchFileNoMetacharEscaping) {
    // For .exe targets, metacharacters should NOT be ^-escaped
    std::vector<std::string> args = {"testapp", "echo & dir"};
    std::wstring cmdLine = resolveCommandLine(args);
    ASSERT_FALSE(cmdLine.empty());

    // Should NOT contain ^& for .exe targets
    EXPECT_EQ(cmdLine.find(L"^&"), std::wstring::npos)
        << "Unexpected ^& in command line for .exe target";
}

// ============================================================================
// Test: round-trip through CommandLineToArgvW for generated command lines
// ============================================================================

TEST_F(ArgumentEscapingTest, RoundTripSimpleArgs) {
    std::vector<std::string> args = {"testapp", "one", "two", "three"};
    std::wstring cmdLine = resolveCommandLine(args);
    ASSERT_FALSE(cmdLine.empty());

    auto argv = parseCommandLine(cmdLine);
    ASSERT_EQ(argv.size(), 4u);
    EXPECT_EQ(argv[1], L"one");
    EXPECT_EQ(argv[2], L"two");
    EXPECT_EQ(argv[3], L"three");
}

TEST_F(ArgumentEscapingTest, RoundTripMixedComplexArgs) {
    std::vector<std::string> args = {
        "testapp",
        "hello world",          // spaces
        "say \"hi\"",           // embedded quotes
        "path\\to\\",           // trailing backslash
        "",                     // empty
        "no-special",           // plain
        "back\\\"slash-quote",  // backslash before quote
    };
    std::wstring cmdLine = resolveCommandLine(args);
    ASSERT_FALSE(cmdLine.empty());

    auto argv = parseCommandLine(cmdLine);
    ASSERT_EQ(argv.size(), 7u);
    EXPECT_EQ(argv[1], L"hello world");
    EXPECT_EQ(argv[2], L"say \"hi\"");
    EXPECT_EQ(argv[3], L"path\\to\\");
    EXPECT_EQ(argv[4], L"");
    EXPECT_EQ(argv[5], L"no-special");
    EXPECT_EQ(argv[6], L"back\\\"slash-quote");
}

TEST_F(ArgumentEscapingTest, RoundTripPathWithMultipleBackslashes) {
    std::vector<std::string> args = {"testapp", "C:\\Program Files\\My App\\bin\\"};
    std::wstring cmdLine = resolveCommandLine(args);
    ASSERT_FALSE(cmdLine.empty());

    auto argv = parseCommandLine(cmdLine);
    ASSERT_GE(argv.size(), 2u);
    EXPECT_EQ(argv[1], L"C:\\Program Files\\My App\\bin\\");
}

TEST_F(ArgumentEscapingTest, RoundTripAllBackslashesAndQuotes) {
    // Stress test: multiple backslashes before a quote
    std::vector<std::string> args = {"testapp", "a\\\\\"b"};
    std::wstring cmdLine = resolveCommandLine(args);
    ASSERT_FALSE(cmdLine.empty());

    auto argv = parseCommandLine(cmdLine);
    ASSERT_GE(argv.size(), 2u);
    EXPECT_EQ(argv[1], L"a\\\\\"b");
}
