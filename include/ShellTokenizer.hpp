#pragma once

#ifndef TERMINAL_SHELLTOKENIZER_HPP
#define TERMINAL_SHELLTOKENIZER_HPP

#include <string>
#include <vector>

namespace shell {

// Splits a line into a vector of stages. Each stage is a vector of argv tokens.
// - Whitespace separates tokens (unless inside quotes).
// - '|' outside of quotes ends the current stage and starts a new one.
// - Single quotes ('...') preserve everything literally until the next single quote.
//   No escapes inside single quotes.
// - Double quotes ("...") preserve everything literally except \" and \\.
// - Outside quotes, \ escapes the next character.
// Returns the parsed stages. An empty input line returns an empty vector.
// If a stage is empty (e.g. "cmd1 | | cmd2"), it is still included as an
// empty vector in the result; the executor should detect that and report an error.
std::vector<std::vector<std::string>> tokenizePipeline(const std::string& line);

} // namespace shell

#endif
