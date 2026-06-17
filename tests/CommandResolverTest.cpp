#include <gtest/gtest.h>
#include "CommandResolver.hpp"

#include <filesystem>
#include <fstream>
#include <string>
#include <vector>
#include <windows.h>

// ============================================================================
// Fixture for filesystem-dependent resolve() tests
// ============================================================================

class CommandResolverTest : public ::testing::Test {
protected:
    std::filesystem::path m_origDir;
    std::filesystem::path m_testDir;
    std::wstring m_origPath;
    std::wstring m_origPathext;

    void SetUp() override {
        m_origDir = std::filesystem::current_path();
        m_testDir = std::filesystem::temp_directory_path() / "cmdresolver_test";
        std::filesystem::create_directories(m_testDir);

        // Save original environment
        m_origPath = getEnv(L"PATH");
        m_origPathext = getEnv(L"PATHEXT");
    }

    void TearDown() override {
        std::filesystem::current_path(m_origDir);
        std::filesystem::remove_all(m_testDir);

        // Restore original environment
        SetEnvironmentVariableW(L"PATH", m_origPath.c_str());
        if (!m_origPathext.empty())
            SetEnvironmentVariableW(L"PATHEXT", m_origPathext.c_str());
        else
            SetEnvironmentVariableW(L"PATHEXT", nullptr);

        // Restore CWD exec setting
        SetEnvironmentVariableW(L"TERMINAL_ALLOW_CWD_EXEC", nullptr);
    }

    void createExe(const std::filesystem::path& dir, const std::string& name) {
        std::filesystem::create_directories(dir);
        std::ofstream ofs(dir / name);
        ofs << "fake exe";
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
};

// ============================================================================
// resolve: PATH search order (first match wins)
// ============================================================================

TEST_F(CommandResolverTest, PathSearchOrderFirstMatchWins) {
    // Create two PATH directories, each containing the same executable name
    auto dir1 = m_testDir / "pathdir1";
    auto dir2 = m_testDir / "pathdir2";
    createExe(dir1, "myapp.exe");
    createExe(dir2, "myapp.exe");

    // Set PATH: dir1 comes before dir2
    std::wstring newPath = dir1.wstring() + L";" + dir2.wstring();
    SetEnvironmentVariableW(L"PATH", newPath.c_str());
    SetEnvironmentVariableW(L"PATHEXT", L".EXE");

    // Change CWD to somewhere else so CWD doesn't interfere
    std::filesystem::current_path(m_testDir);

    std::vector<std::string> args = {"myapp", "--flag"};
    auto result = CommandResolver::resolve("myapp", args);

    ASSERT_TRUE(result.has_value());
    // The resolved executable should be from dir1 (the first PATH entry)
    std::wstring resolved = result->executable.wstring();
    EXPECT_TRUE(resolved.find(L"pathdir1") != std::wstring::npos)
        << "Expected path from pathdir1, got: " << result->executable.string();
}

// ============================================================================
// resolve: CWD is searched after PATH
// ============================================================================

TEST_F(CommandResolverTest, CwdSearchedAfterPath) {
    // Put an executable on PATH and also in CWD
    auto pathDir = m_testDir / "pathdir";
    auto cwdDir = m_testDir / "cwddir";
    createExe(pathDir, "sharedcmd.exe");
    createExe(cwdDir, "sharedcmd.exe");

    std::wstring newPath = pathDir.wstring();
    SetEnvironmentVariableW(L"PATH", newPath.c_str());
    SetEnvironmentVariableW(L"PATHEXT", L".EXE");

    // Change CWD to cwdDir
    std::filesystem::current_path(cwdDir);
    SetEnvironmentVariableW(L"TERMINAL_ALLOW_CWD_EXEC", L"1");

    std::vector<std::string> args = {"sharedcmd"};
    auto result = CommandResolver::resolve("sharedcmd", args);

    ASSERT_TRUE(result.has_value());
    // Should resolve from PATH (pathdir), NOT CWD
    std::wstring resolved = result->executable.wstring();
    EXPECT_TRUE(resolved.find(L"pathdir") != std::wstring::npos)
        << "Expected from pathdir (PATH), got: " << result->executable.string();
}

TEST_F(CommandResolverTest, CwdUsedWhenNotOnPath) {
    // Executable only in CWD, not on PATH
    auto cwdDir = m_testDir / "cwdonly";
    auto emptyPathDir = m_testDir / "emptypath";
    createExe(cwdDir, "localcmd.exe");
    std::filesystem::create_directories(emptyPathDir);

    std::wstring newPath = emptyPathDir.wstring();
    SetEnvironmentVariableW(L"PATH", newPath.c_str());
    SetEnvironmentVariableW(L"PATHEXT", L".EXE");
    SetEnvironmentVariableW(L"TERMINAL_ALLOW_CWD_EXEC", L"1");

    std::filesystem::current_path(cwdDir);

    std::vector<std::string> args = {"localcmd"};
    auto result = CommandResolver::resolve("localcmd", args);

    ASSERT_TRUE(result.has_value());
    std::wstring resolved = result->executable.wstring();
    EXPECT_TRUE(resolved.find(L"cwdonly") != std::wstring::npos)
        << "Expected from cwdonly (CWD), got: " << result->executable.string();
}

// ============================================================================
// resolve: PATHEXT extension appending
// ============================================================================

TEST_F(CommandResolverTest, PathextAppendsFindsCmdExtension) {
    auto pathDir = m_testDir / "pathext_test";
    createExe(pathDir, "npm.cmd");

    std::wstring newPath = pathDir.wstring();
    SetEnvironmentVariableW(L"PATH", newPath.c_str());
    SetEnvironmentVariableW(L"PATHEXT", L".COM;.EXE;.BAT;.CMD");

    std::filesystem::current_path(m_testDir);

    std::vector<std::string> args = {"npm", "install"};
    auto result = CommandResolver::resolve("npm", args);

    ASSERT_TRUE(result.has_value());
    std::wstring resolved = result->executable.wstring();
    // Should find npm.cmd via PATHEXT appending
    EXPECT_TRUE(resolved.find(L"npm.cmd") != std::wstring::npos ||
                resolved.find(L"npm.CMD") != std::wstring::npos)
        << "Expected npm.cmd, got: " << result->executable.string();
}

TEST_F(CommandResolverTest, PathextOrderDeterminesPriority) {
    // Both .bat and .exe exist; PATHEXT order determines which is found
    auto pathDir = m_testDir / "pathext_order";
    createExe(pathDir, "tool.bat");
    createExe(pathDir, "tool.exe");

    std::wstring newPath = pathDir.wstring();
    SetEnvironmentVariableW(L"PATH", newPath.c_str());
    // .EXE comes before .BAT in PATHEXT
    SetEnvironmentVariableW(L"PATHEXT", L".EXE;.BAT");

    std::filesystem::current_path(m_testDir);

    std::vector<std::string> args = {"tool"};
    auto result = CommandResolver::resolve("tool", args);

    ASSERT_TRUE(result.has_value());
    std::wstring resolved = result->executable.wstring();
    // .EXE should win since it's listed first in PATHEXT
    EXPECT_TRUE(resolved.find(L"tool.exe") != std::wstring::npos ||
                resolved.find(L"tool.EXE") != std::wstring::npos)
        << "Expected tool.exe (.EXE first in PATHEXT), got: " << result->executable.string();
}

TEST_F(CommandResolverTest, PathextNotFoundReturnsNullopt) {
    auto emptyDir = m_testDir / "empty_dir";
    std::filesystem::create_directories(emptyDir);

    std::wstring newPath = emptyDir.wstring();
    SetEnvironmentVariableW(L"PATH", newPath.c_str());
    SetEnvironmentVariableW(L"PATHEXT", L".EXE");

    std::filesystem::current_path(m_testDir);

    std::vector<std::string> args = {"nonexistent"};
    auto result = CommandResolver::resolve("nonexistent", args);

    EXPECT_FALSE(result.has_value());
}

// ============================================================================
// resolve: qualified paths bypass PATH search
// ============================================================================

TEST_F(CommandResolverTest, QualifiedPathBypassesPathSearch) {
    // Create an executable at a specific path
    auto specificDir = m_testDir / "specific";
    createExe(specificDir, "app.exe");

    // Set PATH to an empty directory (should not matter for qualified paths)
    auto emptyDir = m_testDir / "empty";
    std::filesystem::create_directories(emptyDir);
    SetEnvironmentVariableW(L"PATH", emptyDir.wstring().c_str());
    SetEnvironmentVariableW(L"PATHEXT", L".EXE");

    std::filesystem::current_path(m_testDir);

    std::string qualifiedName = (specificDir / "app.exe").string();
    std::vector<std::string> args = {qualifiedName};
    auto result = CommandResolver::resolve(qualifiedName, args);

    ASSERT_TRUE(result.has_value());
    EXPECT_TRUE(std::filesystem::equivalent(
        result->executable,
        std::filesystem::absolute(specificDir / "app.exe")));
}

TEST_F(CommandResolverTest, QualifiedPathNonexistentReturnsNullopt) {
    auto emptyDir = m_testDir / "empty2";
    std::filesystem::create_directories(emptyDir);
    SetEnvironmentVariableW(L"PATH", emptyDir.wstring().c_str());

    std::filesystem::current_path(m_testDir);

    std::string qualifiedName = ".\\does_not_exist.exe";
    std::vector<std::string> args = {qualifiedName};
    auto result = CommandResolver::resolve(qualifiedName, args);

    EXPECT_FALSE(result.has_value());
}

TEST_F(CommandResolverTest, DotSlashQualifiedResolvesFromCwdDirectly) {
    // ./app.exe is qualified — it skips PATH and checks existence directly
    auto cwdDir = m_testDir / "dotslash";
    createExe(cwdDir, "app.exe");

    auto emptyDir = m_testDir / "empty3";
    std::filesystem::create_directories(emptyDir);
    SetEnvironmentVariableW(L"PATH", emptyDir.wstring().c_str());
    SetEnvironmentVariableW(L"PATHEXT", L".EXE");

    std::filesystem::current_path(cwdDir);

    std::vector<std::string> args = {"./app.exe"};
    auto result = CommandResolver::resolve("./app.exe", args);

    ASSERT_TRUE(result.has_value());
    EXPECT_TRUE(std::filesystem::equivalent(
        result->executable,
        std::filesystem::absolute(cwdDir / "app.exe")));
}

// ============================================================================
// resolve: CWD execution refused when TERMINAL_ALLOW_CWD_EXEC=0
// ============================================================================

TEST_F(CommandResolverTest, CwdExecutionRefusedWhenDisabled) {
    auto cwdDir = m_testDir / "refused_cwd";
    auto emptyDir = m_testDir / "empty4";
    createExe(cwdDir, "dangerous.exe");
    std::filesystem::create_directories(emptyDir);

    SetEnvironmentVariableW(L"PATH", emptyDir.wstring().c_str());
    SetEnvironmentVariableW(L"PATHEXT", L".EXE");
    SetEnvironmentVariableW(L"TERMINAL_ALLOW_CWD_EXEC", L"0");

    std::filesystem::current_path(cwdDir);

    std::vector<std::string> args = {"dangerous"};
    auto result = CommandResolver::resolve("dangerous", args);

    // Should be nullopt because CWD execution is disabled
    EXPECT_FALSE(result.has_value());
}
