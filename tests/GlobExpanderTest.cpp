#include <gtest/gtest.h>
#include "GlobExpander.hpp"
#include "ShellTokenizer.hpp"

#include <filesystem>
#include <fstream>
#include <string>
#include <vector>

// ============================================================================
// matchPattern: '*' matches any sequence of characters
// ============================================================================

TEST(GlobExpanderMatchPattern, StarMatchesEmptyString) {
    EXPECT_TRUE(GlobExpander::matchPattern("*", ""));
}

TEST(GlobExpanderMatchPattern, StarMatchesSingleChar) {
    EXPECT_TRUE(GlobExpander::matchPattern("*", "a"));
}

TEST(GlobExpanderMatchPattern, StarMatchesMultipleChars) {
    EXPECT_TRUE(GlobExpander::matchPattern("*", "hello"));
}

TEST(GlobExpanderMatchPattern, StarSuffix) {
    EXPECT_TRUE(GlobExpander::matchPattern("hello*", "helloworld"));
    EXPECT_TRUE(GlobExpander::matchPattern("hello*", "hello"));
    EXPECT_FALSE(GlobExpander::matchPattern("hello*", "hell"));
}

TEST(GlobExpanderMatchPattern, StarPrefix) {
    EXPECT_TRUE(GlobExpander::matchPattern("*.txt", "file.txt"));
    EXPECT_TRUE(GlobExpander::matchPattern("*.txt", ".txt"));
    EXPECT_FALSE(GlobExpander::matchPattern("*.txt", "file.cpp"));
}

TEST(GlobExpanderMatchPattern, StarInMiddle) {
    EXPECT_TRUE(GlobExpander::matchPattern("a*z", "az"));
    EXPECT_TRUE(GlobExpander::matchPattern("a*z", "abcz"));
    EXPECT_FALSE(GlobExpander::matchPattern("a*z", "abcx"));
}

TEST(GlobExpanderMatchPattern, MultipleStars) {
    EXPECT_TRUE(GlobExpander::matchPattern("*.*", "file.txt"));
    EXPECT_TRUE(GlobExpander::matchPattern("*.*", "a.b"));
    EXPECT_FALSE(GlobExpander::matchPattern("*.*", "noextension"));
}

// ============================================================================
// matchPattern: '?' matches exactly one character
// ============================================================================

TEST(GlobExpanderMatchPattern, QuestionMarkMatchesSingleChar) {
    EXPECT_TRUE(GlobExpander::matchPattern("?", "a"));
    EXPECT_TRUE(GlobExpander::matchPattern("?", "z"));
}

TEST(GlobExpanderMatchPattern, QuestionMarkDoesNotMatchEmpty) {
    EXPECT_FALSE(GlobExpander::matchPattern("?", ""));
}

TEST(GlobExpanderMatchPattern, QuestionMarkDoesNotMatchMultipleChars) {
    EXPECT_FALSE(GlobExpander::matchPattern("?", "ab"));
}

TEST(GlobExpanderMatchPattern, QuestionMarkInPattern) {
    EXPECT_TRUE(GlobExpander::matchPattern("file?.txt", "file1.txt"));
    EXPECT_TRUE(GlobExpander::matchPattern("file?.txt", "fileA.txt"));
    EXPECT_FALSE(GlobExpander::matchPattern("file?.txt", "file10.txt"));
    EXPECT_FALSE(GlobExpander::matchPattern("file?.txt", "file.txt"));
}

TEST(GlobExpanderMatchPattern, MultipleQuestionMarks) {
    EXPECT_TRUE(GlobExpander::matchPattern("??", "ab"));
    EXPECT_FALSE(GlobExpander::matchPattern("??", "a"));
    EXPECT_FALSE(GlobExpander::matchPattern("??", "abc"));
}

// ============================================================================
// matchPattern: literal strings match exactly
// ============================================================================

TEST(GlobExpanderMatchPattern, LiteralExactMatch) {
    EXPECT_TRUE(GlobExpander::matchPattern("hello", "hello"));
}

TEST(GlobExpanderMatchPattern, LiteralNoMatch) {
    EXPECT_FALSE(GlobExpander::matchPattern("hello", "world"));
}

TEST(GlobExpanderMatchPattern, LiteralCaseSensitive) {
    // On any platform, the raw matchPattern is case-sensitive
    EXPECT_FALSE(GlobExpander::matchPattern("Hello", "hello"));
    EXPECT_FALSE(GlobExpander::matchPattern("hello", "Hello"));
}

TEST(GlobExpanderMatchPattern, LiteralEmptyPatternMatchesEmptyString) {
    EXPECT_TRUE(GlobExpander::matchPattern("", ""));
}

TEST(GlobExpanderMatchPattern, LiteralEmptyPatternDoesNotMatchNonEmpty) {
    EXPECT_FALSE(GlobExpander::matchPattern("", "a"));
}

TEST(GlobExpanderMatchPattern, LiteralSubstring) {
    // Must match entire string, not just a substring
    EXPECT_FALSE(GlobExpander::matchPattern("ell", "hello"));
}

// ============================================================================
// matchPattern: combined wildcards
// ============================================================================

TEST(GlobExpanderMatchPattern, StarAndQuestionCombined) {
    EXPECT_TRUE(GlobExpander::matchPattern("*.?", "file.c"));
    EXPECT_FALSE(GlobExpander::matchPattern("*.?", "file.cpp"));
    EXPECT_TRUE(GlobExpander::matchPattern("?*", "a"));
    EXPECT_TRUE(GlobExpander::matchPattern("?*", "abc"));
    EXPECT_FALSE(GlobExpander::matchPattern("?*", ""));
}

// ============================================================================
// expand: patterns with no filesystem matches return the original pattern
// ============================================================================

TEST(GlobExpanderExpand, NoMatchReturnsOriginalPattern) {
    // Use a pattern that won't match anything in the current directory
    std::vector<shell::Token> tokens = {{"zzz_nonexistent_pattern_xyz_*.qqqq", false}};
    auto result = GlobExpander::expand(tokens);
    ASSERT_EQ(result.size(), 1u);
    EXPECT_EQ(result[0], "zzz_nonexistent_pattern_xyz_*.qqqq");
}

TEST(GlobExpanderExpand, QuotedTokenSkipsGlobExpansion) {
    // A quoted token with wildcards should not be expanded
    std::vector<shell::Token> tokens = {{"*.txt", true}};
    auto result = GlobExpander::expand(tokens);
    ASSERT_EQ(result.size(), 1u);
    EXPECT_EQ(result[0], "*.txt");
}

TEST(GlobExpanderExpand, TokenWithoutWildcardsPassedThrough) {
    std::vector<shell::Token> tokens = {{"hello.txt", false}};
    auto result = GlobExpander::expand(tokens);
    ASSERT_EQ(result.size(), 1u);
    EXPECT_EQ(result[0], "hello.txt");
}

TEST(GlobExpanderExpand, MultipleTokensMixed) {
    std::vector<shell::Token> tokens = {
        {"ls", false},
        {"zzz_no_match_abc_*.xyz", false},
        {"literal", false}
    };
    auto result = GlobExpander::expand(tokens);
    ASSERT_EQ(result.size(), 3u);
    EXPECT_EQ(result[0], "ls");
    EXPECT_EQ(result[1], "zzz_no_match_abc_*.xyz");
    EXPECT_EQ(result[2], "literal");
}

// ============================================================================
// expand: filesystem-dependent tests using temporary files
// ============================================================================

class GlobExpanderFilesystemTest : public ::testing::Test {
protected:
    std::filesystem::path m_origDir;
    std::filesystem::path m_testDir;

    void SetUp() override {
        m_origDir = std::filesystem::current_path();
        m_testDir = std::filesystem::temp_directory_path() / "glob_test_dir";
        std::filesystem::create_directories(m_testDir);

        // Create test files
        createFile("file1.txt");
        createFile("file2.txt");
        createFile("file3.cpp");
        createFile("readme.md");
        createFile("a.c");

        std::filesystem::current_path(m_testDir);
    }

    void TearDown() override {
        std::filesystem::current_path(m_origDir);
        std::filesystem::remove_all(m_testDir);
    }

    void createFile(const std::string& name) {
        std::ofstream ofs(m_testDir / name);
        ofs << "test";
    }
};

TEST_F(GlobExpanderFilesystemTest, StarTxtMatchesTxtFiles) {
    std::vector<shell::Token> tokens = {{"*.txt", false}};
    auto result = GlobExpander::expand(tokens);
    ASSERT_EQ(result.size(), 2u);
    // Results are sorted
    EXPECT_EQ(result[0], "file1.txt");
    EXPECT_EQ(result[1], "file2.txt");
}

TEST_F(GlobExpanderFilesystemTest, QuestionMarkInFilename) {
    std::vector<shell::Token> tokens = {{"file?.txt", false}};
    auto result = GlobExpander::expand(tokens);
    ASSERT_EQ(result.size(), 2u);
    EXPECT_EQ(result[0], "file1.txt");
    EXPECT_EQ(result[1], "file2.txt");
}

TEST_F(GlobExpanderFilesystemTest, StarMatchesAll) {
    std::vector<shell::Token> tokens = {{"*", false}};
    auto result = GlobExpander::expand(tokens);
    // Should match all non-hidden files
    ASSERT_EQ(result.size(), 5u);
    EXPECT_EQ(result[0], "a.c");
    EXPECT_EQ(result[1], "file1.txt");
    EXPECT_EQ(result[2], "file2.txt");
    EXPECT_EQ(result[3], "file3.cpp");
    EXPECT_EQ(result[4], "readme.md");
}

TEST_F(GlobExpanderFilesystemTest, SingleCharExtensionMatch) {
    std::vector<shell::Token> tokens = {{"*.?", false}};
    auto result = GlobExpander::expand(tokens);
    ASSERT_EQ(result.size(), 1u);
    EXPECT_EQ(result[0], "a.c");
}

TEST_F(GlobExpanderFilesystemTest, LiteralMatchExact) {
    std::vector<shell::Token> tokens = {{"readme.md", false}};
    auto result = GlobExpander::expand(tokens);
    // No wildcards, so it just passes through as-is (not expanded against filesystem)
    ASSERT_EQ(result.size(), 1u);
    EXPECT_EQ(result[0], "readme.md");
}

// ============================================================================
// Case sensitivity behavior on Windows
// ============================================================================

#ifdef _WIN32
TEST_F(GlobExpanderFilesystemTest, WindowsCaseInsensitiveFilesystem) {
    // On Windows, the filesystem is typically case-insensitive.
    // However, matchPattern itself is case-sensitive.
    // The expand function matches against actual filenames from the filesystem,
    // so the pattern case must match what the filesystem returns.
    // This test verifies that matchPattern is case-sensitive by default.
    EXPECT_FALSE(GlobExpander::matchPattern("FILE*.txt", "file1.txt"));
    EXPECT_TRUE(GlobExpander::matchPattern("file*.txt", "file1.txt"));
}

TEST_F(GlobExpanderFilesystemTest, WindowsMatchPatternIsCaseSensitive) {
    // matchPattern does exact character comparison (case-sensitive)
    EXPECT_FALSE(GlobExpander::matchPattern("README.MD", "readme.md"));
    EXPECT_TRUE(GlobExpander::matchPattern("readme.md", "readme.md"));
}
#endif
