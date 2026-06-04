#include "ShellTokenizer.hpp"

#include <cctype>

namespace shell {

namespace {

bool isPipe(char c) { return c == '|'; }
bool isWs(char c)   { return c == ' ' || c == '\t'; }

} // namespace

std::vector<std::vector<std::string>> tokenizePipeline(const std::string& line) {
    std::vector<std::vector<std::string>> stages;
    std::vector<std::string> current;
    std::string token;
    bool inToken = false;
    char quote = 0; // 0, '\'' or '"'

    auto pushToken = [&]() {
        if (inToken) {
            current.push_back(std::move(token));
            token.clear();
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
            continue;
        }

        if (c == '\\' && i + 1 < line.size()) {
            token.push_back(line[i + 1]);
            inToken = true;
            ++i;
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
