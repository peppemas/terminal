#include <gtest/gtest.h>
#include "Commands.hpp"

#include <fstream>
#include <sstream>
#include <filesystem>
#include <string>

namespace fs = std::filesystem;

class TailEdgeCaseTest : public ::testing::Test {
protected:
    std::string tempDir;

    void SetUp() override {
        tempDir = (fs::temp_directory_path() / "tail_test").string();
        fs::create_directories(tempDir);
    }

    void TearDown() override {
        fs::remove_all(tempDir);
    }

    std::string createFile(const std::string& name, const std::string& content) {
        std::string path = tempDir + "/" + name;
        std::ofstream out(path, std::ios::binary);
        out << content;
        out.close();
        return path;
    }

    std::string runTail(const std::string& filePath, int lineCount = 10) {
        commands::Args args = {"tail", "-n", std::to_string(lineCount), filePath};
        std::ostringstream out;
        std::istringstream in;
        std::ostringstream err;
        commands::tail(args, out, in, err);
        return out.str();
    }
};

// Test with empty file (size == 0)
TEST_F(TailEdgeCaseTest, EmptyFileReturnsNothing) {
    std::string path = createFile("empty.txt", "");
    std::string result = runTail(path);
    EXPECT_EQ(result, "");
}

// Test with single-byte file (no newline)
TEST_F(TailEdgeCaseTest, SingleByteFileReturnsContent) {
    std::string path = createFile("single_byte.txt", "A");
    std::string result = runTail(path);
    EXPECT_EQ(result, "A\n");
}

// Test with file containing only a single newline
TEST_F(TailEdgeCaseTest, SingleNewlineFileReturnsNewline) {
    std::string path = createFile("single_newline.txt", "\n");
    std::string result = runTail(path);
    EXPECT_EQ(result, "\n");
}

// Test with file smaller than requested lines
TEST_F(TailEdgeCaseTest, FileSmallerThanRequestedLines) {
    std::string path = createFile("small.txt", "line1\nline2\nline3\n");
    std::string result = runTail(path, 100);
    EXPECT_EQ(result, "line1\nline2\nline3\n");
}

// Test normal case still works: last N lines of a multi-line file
TEST_F(TailEdgeCaseTest, LastNLinesOfMultiLineFile) {
    std::string path = createFile("multi.txt", "line1\nline2\nline3\nline4\nline5\n");
    std::string result = runTail(path, 2);
    EXPECT_EQ(result, "line4\nline5\n");
}

// Test file with exactly the number of requested lines
TEST_F(TailEdgeCaseTest, ExactLineCountMatch) {
    std::string path = createFile("exact.txt", "line1\nline2\nline3\n");
    std::string result = runTail(path, 3);
    EXPECT_EQ(result, "line1\nline2\nline3\n");
}

// Test two-byte file (no newline, very small)
TEST_F(TailEdgeCaseTest, TwoByteFileNoNewline) {
    std::string path = createFile("two_bytes.txt", "AB");
    std::string result = runTail(path);
    EXPECT_EQ(result, "AB\n");
}
