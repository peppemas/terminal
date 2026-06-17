#include "Commands.hpp"
#include "Config.hpp"

#include <iostream>
#include <fstream>
#include <filesystem>
#include <regex>
#include <cstdint>
#include <vector>
#include <string>

#define NOMINMAX
#include <windows.h>

namespace fs = std::filesystem;

namespace {

// ------------------------------------------------------------------
// tail helpers
// ------------------------------------------------------------------

enum class CountMode { Lines, Bytes };

struct TailOptions {
    CountMode mode = CountMode::Lines;
    std::uintmax_t count = 10;
    bool fromStart = false;
    bool quiet = false;
    bool verbose = false;
    bool help = false;
    bool conflict = false;
    std::vector<std::string> files;
};

TailOptions parseTailArgs(const commands::Args& args, std::ostream& err) {
    TailOptions opts;
    for (std::size_t i = 1; i < args.size(); ++i) {
        const std::string& token = args[i];
        if (token == "--help") {
            opts.help = true;
            return opts;
        }
        if (token == "-q") {
            opts.quiet = true;
            continue;
        }
        if (token == "-v") {
            opts.verbose = true;
            continue;
        }
        if (token == "-n" || token == "--lines") {
            if (i + 1 >= args.size()) {
                err << "tail: option '" << token << "' requires an argument\n";
                return opts;
            }
            const std::string& val = args[++i];
            if (!val.empty() && val[0] == '+') {
                try {
                    opts.count = std::stoull(val.substr(1));
                } catch (...) {
                    err << "tail: invalid number: '" << val << "'\n";
                    return opts;
                }
                opts.fromStart = true;
            } else {
                try {
                    opts.count = std::stoull(val);
                } catch (...) {
                    err << "tail: invalid number: '" << val << "'\n";
                    return opts;
                }
                opts.fromStart = false;
            }
            if (opts.mode == CountMode::Bytes) {
                opts.conflict = true;
            }
            opts.mode = CountMode::Lines;
            continue;
        }
        if (token.rfind("--lines=", 0) == 0) {
            std::string val = token.substr(8);
            if (val.empty()) {
                err << "tail: option '--lines=' requires a value\n";
                return opts;
            }
            if (!val.empty() && val[0] == '+') {
                try {
                    opts.count = std::stoull(val.substr(1));
                } catch (...) {
                    err << "tail: invalid number: '" << val << "'\n";
                    return opts;
                }
                opts.fromStart = true;
            } else {
                try {
                    opts.count = std::stoull(val);
                } catch (...) {
                    err << "tail: invalid number: '" << val << "'\n";
                    return opts;
                }
                opts.fromStart = false;
            }
            if (opts.mode == CountMode::Bytes) {
                opts.conflict = true;
            }
            opts.mode = CountMode::Lines;
            continue;
        }
        if (token == "-c" || token == "--bytes") {
            if (i + 1 >= args.size()) {
                err << "tail: option '" << token << "' requires an argument\n";
                return opts;
            }
            const std::string& val = args[++i];
            try {
                opts.count = std::stoull(val);
            } catch (...) {
                err << "tail: invalid number: '" << val << "'\n";
                return opts;
            }
            if (opts.mode == CountMode::Lines) {
                opts.conflict = true;
            }
            opts.mode = CountMode::Bytes;
            continue;
        }
        if (token.rfind("--bytes=", 0) == 0) {
            std::string val = token.substr(8);
            if (val.empty()) {
                err << "tail: option '--bytes=' requires a value\n";
                return opts;
            }
            try {
                opts.count = std::stoull(val);
            } catch (...) {
                err << "tail: invalid number: '" << val << "'\n";
                return opts;
            }
            if (opts.mode == CountMode::Lines) {
                opts.conflict = true;
            }
            opts.mode = CountMode::Bytes;
            continue;
        }
        if (!token.empty() && token[0] == '-') {
            err << "tail: invalid option: '" << token << "'\n";
            continue;
        }
        opts.files.push_back(token);
    }
    return opts;
}

void printTailHelp(std::ostream& out) {
    out << "usage: tail [OPTION]... [FILE]...\n"
        << "Print the last 10 lines of each FILE to standard output.\n"
        << "With more than one FILE, precede each with a header giving the file name.\n\n"
        << "  -n, --lines=NUM    output the last NUM lines (or +NUM lines from start)\n"
        << "  -c, --bytes=NUM    output the last NUM bytes\n"
        << "  -q                 never output headers giving file names\n"
        << "  -v                 always output headers giving file names\n"
        << "      --help         display this help and exit\n\n"
        << "Examples:\n"
        << "  tail file.txt            last 10 lines of file.txt\n"
        << "  tail -n 5 file.txt       last 5 lines of file.txt\n"
        << "  tail -n +15 file.txt     lines 15 through end of file.txt\n"
        << "  tail -c 25 file.txt      last 25 bytes of file.txt\n"
        << "  tail -q file1 file2      no headers, just concatenated tails\n";
}

bool shouldPrintHeader(std::size_t totalFiles, const TailOptions& opts) {
    if (opts.quiet) return false;
    if (opts.verbose) return true;
    return totalFiles > 1;
}

void printTailHeader(const std::string& filename, std::ostream& out) {
    out << "==> " << filename << " <==\n";
}

void printTailBytes(const std::string& file, std::uintmax_t count, std::ostream& out) {
    if (count == 0) {
        return;
    }
    std::ifstream in(file, std::ios::binary);
    if (!in.is_open()) {
        return;
    }
    in.seekg(0, std::ios::end);
    std::streamoff size = in.tellg();
    if (size <= 0) {
        return;
    }
    std::streamoff startPos = (size > static_cast<std::streamoff>(count)) ? size - static_cast<std::streamoff>(count) : 0;
    in.seekg(startPos);
    out << in.rdbuf();
}

void printTailLinesBackward(const std::string& file, std::uintmax_t count, std::ostream& out) {
    if (count == 0) {
        return;
    }
    std::ifstream in(file, std::ios::binary);
    if (!in.is_open()) {
        return;
    }
    in.seekg(0, std::ios::end);
    std::streamoff size = in.tellg();
    if (size <= 0) {
        return;
    }

    // If file size is small enough, just read the entire file from the beginning
    if (static_cast<std::uintmax_t>(size) <= count) {
        in.seekg(0);
        in.clear();
        std::string line;
        while (std::getline(in, line)) {
            out << line << '\n';
        }
        return;
    }

    std::streamoff pos = size - 1;
    in.seekg(pos);
    char lastChar = 0;
    in.read(&lastChar, 1);
    if (lastChar == '\n') {
        if (pos > 0) {
            --pos;
        }
    }

    std::uintmax_t foundCount = 0;
    while (pos > 0) {
        in.seekg(pos);
        char ch = 0;
        in.read(&ch, 1);
        if (ch == '\n') {
            ++foundCount;
            if (foundCount == count) {
                break;
            }
        }
        --pos;
    }

    // Clamp: if we hit the beginning, check the first character too
    if (pos == 0 && foundCount < count) {
        in.seekg(0);
        char ch = 0;
        in.read(&ch, 1);
        if (ch == '\n') {
            ++foundCount;
        }
    }

    std::streamoff startPos = (foundCount == count) ? pos + 1 : 0;
    in.seekg(startPos);
    in.clear();

    std::string line;
    while (std::getline(in, line)) {
        out << line << '\n';
    }
}

void printTailLinesForward(const std::string& file, std::uintmax_t startLine, std::ostream& out) {
    if (startLine == 0) {
        return;
    }
    std::ifstream in(file);
    if (!in.is_open()) {
        return;
    }
    std::string line;
    std::uintmax_t currentLine = 1;
    while (std::getline(in, line)) {
        if (currentLine >= startLine) {
            out << line << '\n';
        }
        ++currentLine;
    }
}

} // anonymous namespace

void commands::cat(const Args& args, std::ostream& out, std::istream& /*in*/, std::ostream& err)
{
    // --help
    for (std::size_t i = 1; i < args.size(); ++i) {
        if (args[i] == "--help") {
            out << "usage: cat [file...]\n"
                << "  Concatenate files to standard output.\n"
                << "  Limits: regular files only, max 10 MiB streamed, Ctrl+C to abort.\n";
            return;
        }
    }

    if (args.size() < 2) {
        // Unix `cat` with no args is silent — keep that behavior
        return;
    }

    HANDLE hIn = GetStdHandle(STD_INPUT_HANDLE);
    std::size_t totalWritten = 0;

    auto pollCtrlC = [&]() -> bool {
        if (hIn == INVALID_HANDLE_VALUE) return false;
        DWORD pending = 0;
        if (!GetNumberOfConsoleInputEvents(hIn, &pending) || pending == 0) return false;
        for (DWORD k = 0; k < pending; ++k) {
            INPUT_RECORD rec;
            DWORD got = 0;
            if (!ReadConsoleInput(hIn, &rec, 1, &got) || got == 0) break;
            if (rec.EventType == KEY_EVENT && rec.Event.KeyEvent.bKeyDown) {
                WORD vk = rec.Event.KeyEvent.wVirtualKeyCode;
                bool ctrl = (rec.Event.KeyEvent.dwControlKeyState &
                             (LEFT_CTRL_PRESSED | RIGHT_CTRL_PRESSED)) != 0;
                if (vk == 'C' && ctrl) return true;
            }
        }
        return false;
    };

    for (std::size_t i = 1; i < args.size(); ++i) {
        const std::string& path = args[i];
        std::error_code ec;
        if (!fs::is_regular_file(path, ec)) {
            err << "cat: '" << path << "': "
                      << (ec ? ec.message().c_str() : "not a regular file") << '\n';
            continue;
        }

        std::ifstream file(path, std::ios::binary);
        if (!file.is_open()) {
            err << "cat: cannot open '" << path << "'\n";
            continue;
        }

        std::vector<char> buf(config::IO_CHUNK_SIZE);
        bool aborted = false;
        while (file) {
            file.read(buf.data(), static_cast<std::streamsize>(buf.size()));
            std::streamsize got = file.gcount();
            if (got > 0) {
                out.write(buf.data(), got);
                totalWritten += static_cast<std::size_t>(got);
                if (totalWritten > config::MAX_CAT_FILE_SIZE) {
                    err << "cat: output truncated at 10 MiB; use a pager\n";
                    aborted = true;
                    break;
                }
            }
            if (file.bad()) {
                err << "cat: read error on '" << path << "'\n";
                break;
            }
            if (pollCtrlC()) {
                out << "^C\n";
                aborted = true;
                break;
            }
            if (file.eof()) break;
        }
        if (aborted) return;
    }
}

void commands::tail(const Args& args, std::ostream& out, std::istream& /*in*/, std::ostream& err)
{
    TailOptions opts = parseTailArgs(args, err);
    if (opts.help) {
        printTailHelp(out);
        return;
    }
    if (opts.files.empty()) {
        err << "usage: tail [OPTION]... [FILE]...\n";
        return;
    }
    if (opts.conflict) {
        err << "tail: lines and bytes options are mutually exclusive\n";
        return;
    }

    std::size_t totalFiles = opts.files.size();
    for (const std::string& filename : opts.files) {
        if (shouldPrintHeader(totalFiles, opts)) {
            printTailHeader(filename, out);
        }
        std::ifstream test(filename, std::ios::binary);
        if (!test.is_open()) {
            err << "tail: cannot open '" << filename << "'\n";
            continue;
        }
        test.close();

        if (opts.mode == CountMode::Bytes) {
            printTailBytes(filename, opts.count, out);
        } else if (opts.fromStart) {
            printTailLinesForward(filename, opts.count, out);
        } else {
            printTailLinesBackward(filename, opts.count, out);
        }
    }
}

void commands::grep(const Args& args, std::ostream& out, std::istream& in, std::ostream& err)
{
    if (args.size() < 2) {
        err << "usage: grep <pattern> [file]\n";
        return;
    }

    std::regex re;
    try {
        re = std::regex(args[1]);
    } catch (const std::regex_error& e) {
        err << "grep: invalid regex: " << e.what() << '\n';
        return;
    }

    std::ifstream file;
    std::istream* input = &in;
    if (args.size() >= 3) {
        file.open(args[2]);
        if (!file.is_open()) {
            err << "grep: cannot open '" << args[2] << "'\n";
            return;
        }
        input = &file;
    }

    std::string line;
    while (std::getline(*input, line)) {
        std::string output;
        std::string::const_iterator searchStart(line.cbegin());
        std::smatch match;
        bool found = false;

        while (std::regex_search(searchStart, line.cend(), match, re)) {
            found = true;
            output.append(searchStart, match[0].first);
            output += commands::RED;
            output += match[0].str();
            output += commands::RESET;
            searchStart = match[0].second;
        }

        if (found) {
            output.append(searchStart, line.cend());
            out << output << '\n';
        }
    }
}
