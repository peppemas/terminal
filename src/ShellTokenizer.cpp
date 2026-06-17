#include "ShellTokenizer.hpp"

#include <cctype>

namespace shell {

namespace {

bool isPipe(char c) { return c == '|'; }
bool isWs(char c)   { return c == ' ' || c == '\t'; }

} // namespace

TokenizeResult tokenizePipeline(const std::string& line) {
    TokenizeResult result;
    std::vector<Token> current;
    std::string token;
    bool inToken = false;
    bool tokenQuoted = false;
    char quote = 0;            // 0, '\'' or '"'
    std::size_t quoteOpenPos = 0; // position of the opening quote character

    auto pushToken = [&]() {
        if (inToken) {
            current.push_back(Token{std::move(token), tokenQuoted});
            token.clear();
            tokenQuoted = false;
            inToken = false;
        }
    };

    auto pushStage = [&]() {
        pushToken();
        result.stages.push_back(std::move(current));
        current.clear();
    };

    for (std::size_t i = 0; i < line.size(); ++i) {
        char c = line[i];

        if (quote) {
            if (c == quote) {
                quote = 0;
            } else if (quote == '"' && c == '\\' && i + 1 < line.size()) {
                char n = line[i + 1];
                if (n == '"' || n == '\\') {
                    token.push_back(n);
                    ++i;
                } else {
                    token.push_back(c);
                }
            } else {
                token.push_back(c);
            }
            inToken = true;
            continue;
        }

        if (c == '\'' || c == '"') {
            quote = c;
            quoteOpenPos = i;
            inToken = true;
            tokenQuoted = true;
            continue;
        }

        if (isPipe(c)) {
            pushStage();
            continue;
        }

        if (isWs(c)) {
            pushToken();
            continue;
        }

        token.push_back(c);
        inToken = true;
    }

    // Check for unterminated quote at end of input
    if (quote != 0) {
        result.error = "unterminated " + std::string(quote == '"' ? "double" : "single")
                     + " quote at position " + std::to_string(quoteOpenPos + 1);
    }

    // Still push whatever was parsed so far (partial results)
    quote = 0;
    pushStage();

    return result;
}

} // namespace shell
