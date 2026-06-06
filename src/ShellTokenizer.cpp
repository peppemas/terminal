#include "ShellTokenizer.hpp"

#include <cctype>

namespace shell {

namespace {

bool isPipe(char c) { return c == '|'; }
bool isWs(char c)   { return c == ' ' || c == '\t'; }

} // namespace

std::vector<std::vector<Token>> tokenizePipeline(const std::string& line) {
    std::vector<std::vector<Token>> stages;
    std::vector<Token> current;
    std::string token;
    bool inToken = false;
    bool tokenQuoted = false;
    char quote = 0; // 0, '\'' or '"'

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
        stages.push_back(std::move(current));
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

    // unterminated quote: treat as end of input
    quote = 0;
    pushStage();

    return stages;
}

} // namespace shell
