#include <gtest/gtest.h>
#include "ShellTokenizer.hpp"

#include <string>
#include <vector>

// ============================================================================
// Simple command tokenization
// ============================================================================

TEST(TokenizerTest, SimpleCommandTokenization) {
    auto result = shell::tokenizePipeline("echo hello");
    ASSERT_FALSE(result.error.has_value()) << "Unexpected error: " << result.error.value_or("");
    ASSERT_EQ(result.stages.size(), 1u);
    ASSERT_EQ(result.stages[0].size(), 2u);
    EXPECT_EQ(result.stages[0][0].text, "echo");
    EXPECT_EQ(result.stages[0][1].text, "hello");
}

TEST(TokenizerTest, SimpleCommandWithFlags) {
    auto result = shell::tokenizePipeline("ls -la");
    ASSERT_FALSE(result.error.has_value());
    ASSERT_EQ(result.stages.size(), 1u);
    ASSERT_EQ(result.stages[0].size(), 2u);
    EXPECT_EQ(result.stages[0][0].text, "ls");
    EXPECT_EQ(result.stages[0][1].text, "-la");
}

TEST(TokenizerTest, MultipleArguments) {
    auto result = shell::tokenizePipeline("cp file1.txt file2.txt dest/");
    ASSERT_FALSE(result.error.has_value());
    ASSERT_EQ(result.stages.size(), 1u);
    ASSERT_EQ(result.stages[0].size(), 4u);
    EXPECT_EQ(result.stages[0][0].text, "cp");
    EXPECT_EQ(result.stages[0][1].text, "file1.txt");
    EXPECT_EQ(result.stages[0][2].text, "file2.txt");
    EXPECT_EQ(result.stages[0][3].text, "dest/");
}

TEST(TokenizerTest, SingleToken) {
    auto result = shell::tokenizePipeline("pwd");
    ASSERT_FALSE(result.error.has_value());
    ASSERT_EQ(result.stages.size(), 1u);
    ASSERT_EQ(result.stages[0].size(), 1u);
    EXPECT_EQ(result.stages[0][0].text, "pwd");
}

TEST(TokenizerTest, EmptyInput) {
    auto result = shell::tokenizePipeline("");
    ASSERT_FALSE(result.error.has_value());
    // Empty input still pushes one empty stage
    ASSERT_EQ(result.stages.size(), 1u);
    EXPECT_TRUE(result.stages[0].empty());
}

TEST(TokenizerTest, WhitespaceOnlyInput) {
    auto result = shell::tokenizePipeline("   \t  ");
    ASSERT_FALSE(result.error.has_value());
    ASSERT_EQ(result.stages.size(), 1u);
    EXPECT_TRUE(result.stages[0].empty());
}

TEST(TokenizerTest, LeadingAndTrailingWhitespace) {
    auto result = shell::tokenizePipeline("   ls   -la   ");
    ASSERT_FALSE(result.error.has_value());
    ASSERT_EQ(result.stages.size(), 1u);
    ASSERT_EQ(result.stages[0].size(), 2u);
    EXPECT_EQ(result.stages[0][0].text, "ls");
    EXPECT_EQ(result.stages[0][1].text, "-la");
}

TEST(TokenizerTest, TabSeparators) {
    auto result = shell::tokenizePipeline("echo\thello\tworld");
    ASSERT_FALSE(result.error.has_value());
    ASSERT_EQ(result.stages.size(), 1u);
    ASSERT_EQ(result.stages[0].size(), 3u);
    EXPECT_EQ(result.stages[0][0].text, "echo");
    EXPECT_EQ(result.stages[0][1].text, "hello");
    EXPECT_EQ(result.stages[0][2].text, "world");
}

// ============================================================================
// Quoted strings with spaces
// ============================================================================

TEST(TokenizerTest, DoubleQuotedStringPreservesSpaces) {
    auto result = shell::tokenizePipeline("echo \"hello world\"");
    ASSERT_FALSE(result.error.has_value());
    ASSERT_EQ(result.stages.size(), 1u);
    ASSERT_EQ(result.stages[0].size(), 2u);
    EXPECT_EQ(result.stages[0][0].text, "echo");
    EXPECT_EQ(result.stages[0][1].text, "hello world");
    EXPECT_TRUE(result.stages[0][1].quoted);
}

TEST(TokenizerTest, SingleQuotedStringPreservesSpaces) {
    auto result = shell::tokenizePipeline("echo 'hello world'");
    ASSERT_FALSE(result.error.has_value());
    ASSERT_EQ(result.stages.size(), 1u);
    ASSERT_EQ(result.stages[0].size(), 2u);
    EXPECT_EQ(result.stages[0][0].text, "echo");
    EXPECT_EQ(result.stages[0][1].text, "hello world");
    EXPECT_TRUE(result.stages[0][1].quoted);
}

TEST(TokenizerTest, QuotedTokenMarkedAsQuoted) {
    auto result = shell::tokenizePipeline("echo \"test\"");
    ASSERT_FALSE(result.error.has_value());
    ASSERT_EQ(result.stages.size(), 1u);
    EXPECT_FALSE(result.stages[0][0].quoted);
    EXPECT_TRUE(result.stages[0][1].quoted);
}

TEST(TokenizerTest, UnquotedTokenNotMarkedAsQuoted) {
    auto result = shell::tokenizePipeline("echo test");
    ASSERT_FALSE(result.error.has_value());
    EXPECT_FALSE(result.stages[0][0].quoted);
    EXPECT_FALSE(result.stages[0][1].quoted);
}

TEST(TokenizerTest, EmptyDoubleQuotedString) {
    auto result = shell::tokenizePipeline("echo \"\"");
    ASSERT_FALSE(result.error.has_value());
    ASSERT_EQ(result.stages.size(), 1u);
    ASSERT_EQ(result.stages[0].size(), 2u);
    EXPECT_EQ(result.stages[0][1].text, "");
    EXPECT_TRUE(result.stages[0][1].quoted);
}

TEST(TokenizerTest, EmptySingleQuotedString) {
    auto result = shell::tokenizePipeline("echo ''");
    ASSERT_FALSE(result.error.has_value());
    ASSERT_EQ(result.stages.size(), 1u);
    ASSERT_EQ(result.stages[0].size(), 2u);
    EXPECT_EQ(result.stages[0][1].text, "");
    EXPECT_TRUE(result.stages[0][1].quoted);
}

TEST(TokenizerTest, QuotedStringWithPipeCharacter) {
    auto result = shell::tokenizePipeline("echo \"hello | world\"");
    ASSERT_FALSE(result.error.has_value());
    ASSERT_EQ(result.stages.size(), 1u);
    ASSERT_EQ(result.stages[0].size(), 2u);
    EXPECT_EQ(result.stages[0][1].text, "hello | world");
}

TEST(TokenizerTest, SingleQuotedStringNoEscapeProcessing) {
    // Inside single quotes, backslash is literal
    auto result = shell::tokenizePipeline("echo 'hello\\nworld'");
    ASSERT_FALSE(result.error.has_value());
    ASSERT_EQ(result.stages[0].size(), 2u);
    EXPECT_EQ(result.stages[0][1].text, "hello\\nworld");
}

// ============================================================================
// Pipe-separated stages
// ============================================================================

TEST(TokenizerTest, TwoStagesPipeSeparated) {
    auto result = shell::tokenizePipeline("ls | grep foo");
    ASSERT_FALSE(result.error.has_value());
    ASSERT_EQ(result.stages.size(), 2u);
    ASSERT_EQ(result.stages[0].size(), 1u);
    EXPECT_EQ(result.stages[0][0].text, "ls");
    ASSERT_EQ(result.stages[1].size(), 2u);
    EXPECT_EQ(result.stages[1][0].text, "grep");
    EXPECT_EQ(result.stages[1][1].text, "foo");
}

TEST(TokenizerTest, ThreeStagesPipeSeparated) {
    auto result = shell::tokenizePipeline("cat file.txt | grep error | tail -5");
    ASSERT_FALSE(result.error.has_value());
    ASSERT_EQ(result.stages.size(), 3u);
    ASSERT_EQ(result.stages[0].size(), 2u);
    EXPECT_EQ(result.stages[0][0].text, "cat");
    EXPECT_EQ(result.stages[0][1].text, "file.txt");
    ASSERT_EQ(result.stages[1].size(), 2u);
    EXPECT_EQ(result.stages[1][0].text, "grep");
    EXPECT_EQ(result.stages[1][1].text, "error");
    ASSERT_EQ(result.stages[2].size(), 2u);
    EXPECT_EQ(result.stages[2][0].text, "tail");
    EXPECT_EQ(result.stages[2][1].text, "-5");
}

TEST(TokenizerTest, PipeWithNoSpaces) {
    auto result = shell::tokenizePipeline("ls|grep foo");
    ASSERT_FALSE(result.error.has_value());
    ASSERT_EQ(result.stages.size(), 2u);
    EXPECT_EQ(result.stages[0][0].text, "ls");
    EXPECT_EQ(result.stages[1][0].text, "grep");
    EXPECT_EQ(result.stages[1][1].text, "foo");
}

TEST(TokenizerTest, PipeInsideQuotesIsNotSeparator) {
    auto result = shell::tokenizePipeline("echo \"a|b\"");
    ASSERT_FALSE(result.error.has_value());
    ASSERT_EQ(result.stages.size(), 1u);
    ASSERT_EQ(result.stages[0].size(), 2u);
    EXPECT_EQ(result.stages[0][1].text, "a|b");
}

// ============================================================================
// Escaped characters outside quotes
// ============================================================================

TEST(TokenizerTest, BackslashOutsideQuotesIsLiteral) {
    // Current implementation: backslash outside quotes is treated as literal character
    auto result = shell::tokenizePipeline("echo hello\\nworld");
    ASSERT_FALSE(result.error.has_value());
    ASSERT_EQ(result.stages.size(), 1u);
    ASSERT_EQ(result.stages[0].size(), 2u);
    EXPECT_EQ(result.stages[0][0].text, "echo");
    EXPECT_EQ(result.stages[0][1].text, "hello\\nworld");
}

TEST(TokenizerTest, BackslashInsideDoubleQuotesEscapesQuote) {
    // \" inside double quotes produces a literal "
    auto result = shell::tokenizePipeline("echo \"hello\\\"world\"");
    ASSERT_FALSE(result.error.has_value());
    ASSERT_EQ(result.stages.size(), 1u);
    ASSERT_EQ(result.stages[0].size(), 2u);
    EXPECT_EQ(result.stages[0][1].text, "hello\"world");
}

TEST(TokenizerTest, BackslashInsideDoubleQuotesEscapesBackslash) {
    // \\ inside double quotes produces a literal backslash
    auto result = shell::tokenizePipeline("echo \"hello\\\\world\"");
    ASSERT_FALSE(result.error.has_value());
    ASSERT_EQ(result.stages.size(), 1u);
    ASSERT_EQ(result.stages[0].size(), 2u);
    EXPECT_EQ(result.stages[0][1].text, "hello\\world");
}

TEST(TokenizerTest, BackslashInsideDoubleQuotesNonSpecialIsLiteral) {
    // \n inside double quotes (not \" or \\) keeps the backslash
    auto result = shell::tokenizePipeline("echo \"hello\\nworld\"");
    ASSERT_FALSE(result.error.has_value());
    ASSERT_EQ(result.stages.size(), 1u);
    ASSERT_EQ(result.stages[0].size(), 2u);
    EXPECT_EQ(result.stages[0][1].text, "hello\\nworld");
}

// ============================================================================
// Unterminated quotes return error result
// ============================================================================

TEST(TokenizerTest, UnterminatedDoubleQuoteReturnsError) {
    auto result = shell::tokenizePipeline("echo \"hello world");
    ASSERT_TRUE(result.error.has_value());
    EXPECT_NE(result.error->find("unterminated"), std::string::npos);
    EXPECT_NE(result.error->find("double"), std::string::npos);
}

TEST(TokenizerTest, UnterminatedSingleQuoteReturnsError) {
    auto result = shell::tokenizePipeline("echo 'hello world");
    ASSERT_TRUE(result.error.has_value());
    EXPECT_NE(result.error->find("unterminated"), std::string::npos);
    EXPECT_NE(result.error->find("single"), std::string::npos);
}

TEST(TokenizerTest, UnterminatedQuoteReportsPosition) {
    // Quote opens at position 6 (0-indexed=5), reported as 1-indexed=6
    auto result = shell::tokenizePipeline("echo \"hello");
    ASSERT_TRUE(result.error.has_value());
    EXPECT_NE(result.error->find("6"), std::string::npos);
}

TEST(TokenizerTest, UnterminatedQuoteStillReturnsPartialStages) {
    auto result = shell::tokenizePipeline("echo \"hello world");
    ASSERT_TRUE(result.error.has_value());
    // Partial results are still available
    ASSERT_GE(result.stages.size(), 1u);
    ASSERT_GE(result.stages[0].size(), 1u);
}

TEST(TokenizerTest, ClosedQuoteFollowedByUnterminatedQuote) {
    auto result = shell::tokenizePipeline("echo \"ok\" \"not closed");
    ASSERT_TRUE(result.error.has_value());
    EXPECT_NE(result.error->find("unterminated"), std::string::npos);
}

// ============================================================================
// Empty stages (consecutive pipes) are preserved
// ============================================================================

TEST(TokenizerTest, EmptyStageFromConsecutivePipes) {
    auto result = shell::tokenizePipeline("ls | | grep foo");
    ASSERT_FALSE(result.error.has_value());
    ASSERT_EQ(result.stages.size(), 3u);
    EXPECT_EQ(result.stages[0].size(), 1u); // "ls"
    EXPECT_TRUE(result.stages[1].empty());   // empty stage
    EXPECT_EQ(result.stages[2].size(), 2u); // "grep foo"
}

TEST(TokenizerTest, EmptyStageAtBeginning) {
    auto result = shell::tokenizePipeline("| ls");
    ASSERT_FALSE(result.error.has_value());
    ASSERT_EQ(result.stages.size(), 2u);
    EXPECT_TRUE(result.stages[0].empty());
    EXPECT_EQ(result.stages[1].size(), 1u);
    EXPECT_EQ(result.stages[1][0].text, "ls");
}

TEST(TokenizerTest, EmptyStageAtEnd) {
    auto result = shell::tokenizePipeline("ls |");
    ASSERT_FALSE(result.error.has_value());
    ASSERT_EQ(result.stages.size(), 2u);
    EXPECT_EQ(result.stages[0].size(), 1u);
    EXPECT_EQ(result.stages[0][0].text, "ls");
    EXPECT_TRUE(result.stages[1].empty());
}

TEST(TokenizerTest, MultipleConsecutivePipes) {
    auto result = shell::tokenizePipeline("a | | | b");
    ASSERT_FALSE(result.error.has_value());
    ASSERT_EQ(result.stages.size(), 4u);
    EXPECT_EQ(result.stages[0].size(), 1u);
    EXPECT_TRUE(result.stages[1].empty());
    EXPECT_TRUE(result.stages[2].empty());
    EXPECT_EQ(result.stages[3].size(), 1u);
}

// ============================================================================
// Round-trip property: tokenize → reconstruct → re-tokenize produces
// equivalent tokens
// ============================================================================

namespace {

// Reconstruct a command line string from tokenized stages.
// Uses double-quoting for tokens that were quoted, and for tokens that
// contain spaces or pipes.
std::string reconstructFromStages(const std::vector<std::vector<shell::Token>>& stages) {
    std::string result;
    for (size_t s = 0; s < stages.size(); ++s) {
        if (s > 0) result += " | ";
        for (size_t t = 0; t < stages[s].size(); ++t) {
            if (t > 0) result += ' ';
            const auto& tok = stages[s][t];
            bool needsQuoting = tok.quoted ||
                                tok.text.find(' ') != std::string::npos ||
                                tok.text.find('\t') != std::string::npos ||
                                tok.text.find('|') != std::string::npos;
            if (needsQuoting) {
                result += '"';
                for (char c : tok.text) {
                    if (c == '"' || c == '\\') result += '\\';
                    result += c;
                }
                result += '"';
            } else {
                result += tok.text;
            }
        }
    }
    return result;
}

// Compare two tokenized results structurally (same stages, same token texts).
bool tokensEquivalent(const shell::TokenizeResult& a, const shell::TokenizeResult& b) {
    if (a.stages.size() != b.stages.size()) return false;
    for (size_t s = 0; s < a.stages.size(); ++s) {
        if (a.stages[s].size() != b.stages[s].size()) return false;
        for (size_t t = 0; t < a.stages[s].size(); ++t) {
            if (a.stages[s][t].text != b.stages[s][t].text) return false;
        }
    }
    return true;
}

} // namespace

TEST(TokenizerTest, RoundTripSimpleCommand) {
    const std::string input = "ls -la";
    auto first = shell::tokenizePipeline(input);
    ASSERT_FALSE(first.error.has_value());
    std::string reconstructed = reconstructFromStages(first.stages);
    auto second = shell::tokenizePipeline(reconstructed);
    ASSERT_FALSE(second.error.has_value());
    EXPECT_TRUE(tokensEquivalent(first, second))
        << "Round-trip failed.\n  Original: " << input
        << "\n  Reconstructed: " << reconstructed;
}

TEST(TokenizerTest, RoundTripQuotedString) {
    const std::string input = "echo \"hello world\"";
    auto first = shell::tokenizePipeline(input);
    ASSERT_FALSE(first.error.has_value());
    std::string reconstructed = reconstructFromStages(first.stages);
    auto second = shell::tokenizePipeline(reconstructed);
    ASSERT_FALSE(second.error.has_value());
    EXPECT_TRUE(tokensEquivalent(first, second))
        << "Round-trip failed.\n  Original: " << input
        << "\n  Reconstructed: " << reconstructed;
}

TEST(TokenizerTest, RoundTripPipeline) {
    const std::string input = "cat file.txt | grep error | tail -5";
    auto first = shell::tokenizePipeline(input);
    ASSERT_FALSE(first.error.has_value());
    std::string reconstructed = reconstructFromStages(first.stages);
    auto second = shell::tokenizePipeline(reconstructed);
    ASSERT_FALSE(second.error.has_value());
    EXPECT_TRUE(tokensEquivalent(first, second))
        << "Round-trip failed.\n  Original: " << input
        << "\n  Reconstructed: " << reconstructed;
}

TEST(TokenizerTest, RoundTripEmbeddedQuotes) {
    const std::string input = "echo \"she said \\\"hi\\\"\"";
    auto first = shell::tokenizePipeline(input);
    ASSERT_FALSE(first.error.has_value());
    std::string reconstructed = reconstructFromStages(first.stages);
    auto second = shell::tokenizePipeline(reconstructed);
    ASSERT_FALSE(second.error.has_value());
    EXPECT_TRUE(tokensEquivalent(first, second))
        << "Round-trip failed.\n  Original: " << input
        << "\n  Reconstructed: " << reconstructed;
}

TEST(TokenizerTest, RoundTripTokenWithPipeInside) {
    const std::string input = "echo \"a|b\" | grep x";
    auto first = shell::tokenizePipeline(input);
    ASSERT_FALSE(first.error.has_value());
    std::string reconstructed = reconstructFromStages(first.stages);
    auto second = shell::tokenizePipeline(reconstructed);
    ASSERT_FALSE(second.error.has_value());
    EXPECT_TRUE(tokensEquivalent(first, second))
        << "Round-trip failed.\n  Original: " << input
        << "\n  Reconstructed: " << reconstructed;
}
