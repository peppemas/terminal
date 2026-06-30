#include "Commands.hpp"
#include "Config.hpp"

#include <iostream>
#include <fstream>
#include <filesystem>
#include <deque>
#include <string>
#include <vector>
#include <sstream>
#include <cctype>

#define NOMINMAX
#include <windows.h>
#include <shellapi.h>

namespace fs = std::filesystem;

namespace {

std::wstring utf8ToWide(const std::string& utf8) {
    if (utf8.empty()) return {};
    int size = MultiByteToWideChar(CP_UTF8, MB_ERR_INVALID_CHARS,
                                   utf8.data(), static_cast<int>(utf8.size()),
                                   nullptr, 0);
    if (size == 0) return {};
    std::wstring wide(size, L'\0');
    MultiByteToWideChar(CP_UTF8, MB_ERR_INVALID_CHARS,
                        utf8.data(), static_cast<int>(utf8.size()),
                        wide.data(), size);
    return wide;
}

// ------------------------------------------------------------------
// more command helpers
// ------------------------------------------------------------------

struct MoreOptions {
    bool help = false;
    bool promptHelp = false;
    bool squeezeBlank = false;
    std::size_t startLine = 1;
    std::string startPattern;
    std::vector<std::string> files;
};

struct PagerState {
    std::istream& input;
    std::ostream& output;
    std::deque<std::string> buffer;
    std::size_t currentPos = 0;
    std::size_t totalLinesRead = 0;
    std::size_t pageHeight = 0;
    std::size_t maxBufferSize = 0;
    bool squeeze = false;
    bool lastWasBlank = false;
    bool showPromptHelp = false;
    bool done = false;
};

struct ConsoleInputGuard {
    HANDLE hInput{INVALID_HANDLE_VALUE};
    DWORD oldMode{0};
    bool valid{false};

    explicit ConsoleInputGuard(HANDLE h) : hInput(h) {
        if (hInput != INVALID_HANDLE_VALUE && GetConsoleMode(hInput, &oldMode)) {
            DWORD newMode = oldMode & ~(ENABLE_LINE_INPUT | ENABLE_ECHO_INPUT);
            if (SetConsoleMode(hInput, newMode)) {
                valid = true;
            }
        }
    }

    ~ConsoleInputGuard() {
        if (valid && hInput != INVALID_HANDLE_VALUE) {
            SetConsoleMode(hInput, oldMode);
        }
    }
};

bool isStdoutTerminal() {
    HANDLE hOut = GetStdHandle(STD_OUTPUT_HANDLE);
    if (hOut == INVALID_HANDLE_VALUE) return false;
    DWORD mode = 0;
    return GetConsoleMode(hOut, &mode) != 0;
}

std::size_t getConsoleHeight() {
    HANDLE hOut = GetStdHandle(STD_OUTPUT_HANDLE);
    if (hOut == INVALID_HANDLE_VALUE) return 25;
    CONSOLE_SCREEN_BUFFER_INFO info{};
    if (!GetConsoleScreenBufferInfo(hOut, &info)) return 25;
    LONG height = info.srWindow.Bottom - info.srWindow.Top + 1;
    if (height < 1) return 1;
    return static_cast<std::size_t>(height);
}

MoreOptions parseMoreArgs(const commands::Args& args, std::ostream& err) {
    MoreOptions opts;
    for (std::size_t i = 1; i < args.size(); ++i) {
        const std::string& token = args[i];
        if (token == "--help") {
            opts.help = true;
            return opts;
        }
        if (token == "-d") {
            opts.promptHelp = true;
            continue;
        }
        if (token == "-s") {
            opts.squeezeBlank = true;
            continue;
        }
        if (!token.empty() && token[0] == '+' && token.size() > 1) {
            if (token[1] == '/') {
                if (token.size() > 2) {
                    opts.startPattern = token.substr(2);
                }
                continue;
            }
            bool allDigits = true;
            for (std::size_t j = 1; j < token.size(); ++j) {
                if (!std::isdigit(static_cast<unsigned char>(token[j]))) {
                    allDigits = false;
                    break;
                }
            }
            if (allDigits && token.size() > 1) {
                try {
                    opts.startLine = std::stoull(token.substr(1));
                    if (opts.startLine == 0) opts.startLine = 1;
                } catch (...) {
                    opts.startLine = 1;
                }
                continue;
            }
        }
        if (!token.empty() && token[0] == '-') {
            err << "more: invalid option: '" << token << "'\n";
            continue;
        }
        opts.files.push_back(token);
    }
    return opts;
}

void printMoreHelp(std::ostream& out) {
    out << "usage: more [OPTION]... [FILE]...\n"
        << "Display FILE content page by page.\n"
        << "With no FILE, read standard input.\n\n"
        << "  -d           display a help prompt at the bottom of each page\n"
        << "  -s           squeeze multiple blank lines into one\n"
        << "  +N           start at line N\n"
        << "  +/pattern    start at first line matching pattern\n"
        << "      --help   display this help and exit\n\n"
        << "Navigation:\n"
        << "  Space        next page\n"
        << "  Enter        next line\n"
        << "  b            back one page\n"
        << "  =            show current line number\n"
        << "  q            quit\n";
}

void streamThrough(std::istream& in, std::ostream& out) {
    std::string line;
    while (std::getline(in, line)) {
        out << line << '\n';
    }
}

bool readLine(std::istream& in, std::string& out) {
    return static_cast<bool>(std::getline(in, out));
}

bool isBlankLine(const std::string& line) {
    for (char c : line) {
        if (!std::isspace(static_cast<unsigned char>(c))) return false;
    }
    return true;
}

void refillBuffer(PagerState& state) {
    std::string line;
    while (state.buffer.size() < state.currentPos + state.pageHeight && !state.done) {
        if (!readLine(state.input, line)) {
            state.done = true;
            break;
        }
        ++state.totalLinesRead;

        bool blank = isBlankLine(line);
        if (state.squeeze && blank && state.lastWasBlank) {
            continue;
        }
        state.lastWasBlank = blank;

        state.buffer.push_back(line);
        if (state.buffer.size() > state.maxBufferSize) {
            state.buffer.pop_front();
            if (state.currentPos > 0) {
                --state.currentPos;
            }
        }
    }
}

void displayPage(PagerState& state) {
    std::size_t end = state.currentPos + state.pageHeight;
    if (end > state.buffer.size()) end = state.buffer.size();
    for (std::size_t i = state.currentPos; i < end; ++i) {
        state.output << state.buffer[i] << '\n';
    }
}

void erasePromptRow(std::ostream& out) {
    out << '\r';
    HANDLE hOut = GetStdHandle(STD_OUTPUT_HANDLE);
    if (hOut != INVALID_HANDLE_VALUE) {
        CONSOLE_SCREEN_BUFFER_INFO info{};
        if (GetConsoleScreenBufferInfo(hOut, &info)) {
            SHORT width = info.dwSize.X;
            for (SHORT i = 0; i < width; ++i) {
                out << ' ';
            }
            out << '\r';
        }
    }
    out.flush();
}

void showPrompt(const PagerState& state, std::ostream& out) {
    out << "--More--";
    if (state.showPromptHelp) {
        out << " [Press space to continue, q to quit]";
    }
    out.flush();
}

std::size_t currentDisplayedLineNumber(const PagerState& state) {
    if (state.buffer.empty()) return 0;
    std::size_t lineNum = state.totalLinesRead - state.buffer.size() + state.currentPos + 1;
    return lineNum;
}

void waitForKeyAndHandle(PagerState& state) {
    HANDLE hIn = GetStdHandle(STD_INPUT_HANDLE);
    if (hIn == INVALID_HANDLE_VALUE) {
        state.done = true;
        return;
    }

    ConsoleInputGuard guard(hIn);
    if (!guard.valid) {
        state.done = true;
        return;
    }

    while (true) {
        showPrompt(state, state.output);

        INPUT_RECORD record{};
        DWORD readCount = 0;
        if (!ReadConsoleInput(hIn, &record, 1, &readCount)) {
            break;
        }
        if (record.EventType != KEY_EVENT || !record.Event.KeyEvent.bKeyDown) {
            erasePromptRow(state.output);   // wipe the just-printed --More-- before retrying
            continue;
        }

        WORD vk = record.Event.KeyEvent.wVirtualKeyCode;
        char ch = record.Event.KeyEvent.uChar.AsciiChar;

        erasePromptRow(state.output);

        if (vk == VK_SPACE) {
            state.currentPos += state.pageHeight;
            break;
        }
        if (vk == VK_RETURN) {
            state.currentPos += 1;
            break;
        }
        if (ch == 'q' || ch == 'Q') {
            state.done = true;
            break;
        }
        if (ch == 'b' || ch == 'B') {
            if (state.currentPos > state.pageHeight) {
                state.currentPos -= state.pageHeight;
            } else {
                state.currentPos = 0;
            }
            break;
        }
        if (ch == '=') {
            std::size_t lineNum = currentDisplayedLineNumber(state);
            state.output << "Line " << lineNum << '\n';
            state.output.flush();
            continue;
        }
        // Unrecognized key: redraw prompt
    }
}

void prePosition(PagerState& state, const MoreOptions& opts) {
    std::string line;
    if (opts.startLine > 1) {
        std::size_t skip = opts.startLine - 1;
        while (skip > 0) {
            if (!readLine(state.input, line)) {
                state.done = true;
                return;
            }
            ++state.totalLinesRead;
            bool blank = isBlankLine(line);
            if (state.squeeze && blank && state.lastWasBlank) {
                continue;
            }
            state.lastWasBlank = blank;
            --skip;
        }
    }
    if (!opts.startPattern.empty()) {
        while (true) {
            if (!readLine(state.input, line)) {
                state.done = true;
                return;
            }
            ++state.totalLinesRead;
            bool blank = isBlankLine(line);
            if (state.squeeze && blank && state.lastWasBlank) {
                continue;
            }
            state.lastWasBlank = blank;
            if (line.find(opts.startPattern) != std::string::npos) {
                state.buffer.push_back(line);
                break;
            }
        }
    }
}

void runPager(std::istream& in, std::ostream& out, const MoreOptions& opts) {
    PagerState state{in, out};
    state.squeeze = opts.squeezeBlank;
    state.showPromptHelp = opts.promptHelp;
    std::size_t consoleHeight = getConsoleHeight();
    state.pageHeight = (consoleHeight > 1) ? consoleHeight - 1 : 1;
    state.maxBufferSize = state.pageHeight * config::PAGE_BUFFER_MULT;

    prePosition(state, opts);

    while (!state.done) {
        refillBuffer(state);
        if (state.currentPos >= state.buffer.size()) {
            if (state.done) break;
            state.currentPos = state.buffer.size();
        }
        displayPage(state);
        if (state.done && state.currentPos + state.pageHeight >= state.buffer.size()) {
            break;
        }
        waitForKeyAndHandle(state);
    }
}

} // anonymous namespace

void commands::echo(const Args& args, std::ostream& out, std::istream& /*in*/, std::ostream& /*err*/)
{
    for (std::size_t i = 1; i < args.size(); ++i) {
        if (i > 1) out << ' ';
        out << args[i];
    }
    out << '\n';
}

void commands::clear(const Args& /*args*/, std::ostream& out, std::istream& /*in*/, std::ostream& /*err*/)
{
    // Clear screen, clear scrollback, and move cursor to home (cross-platform ANSI)
    out << "\x1b[2J\x1b[3J\x1b[H" << std::flush;
}

void commands::open(const Args& args, std::ostream& out, std::istream& /*in*/, std::ostream& err)
{
    if (args.size() < 2) {
        out << "usage: open [--url] <path|URL>\n"
            << "  Opens a file or directory with the default system handler.\n"
            << "  --url    allow opening URLs (http://, https://, ftp://)\n";
        return;
    }

    // Parse --url flag
    bool allowUrl = false;
    std::string target_arg;
    for (std::size_t i = 1; i < args.size(); ++i) {
        if (args[i] == "--url") {
            allowUrl = true;
        } else if (target_arg.empty()) {
            target_arg = args[i];
        } else {
            err << commands::RED << "open: too many arguments\n" << commands::RESET;
            return;
        }
    }

    if (target_arg.empty()) {
        out << "usage: open [--url] <path|URL>\n"
            << "  Opens a file or directory with the default system handler.\n"
            << "  --url    allow opening URLs (http://, https://, ftp://)\n";
        return;
    }

    // Reject URL schemes unless --url flag is provided
    if (!allowUrl) {
        if (target_arg.rfind("http://", 0) == 0 ||
            target_arg.rfind("https://", 0) == 0 ||
            target_arg.rfind("ftp://", 0) == 0) {
            err << commands::RED << "open: refusing to open URL '" << target_arg
                      << "' without --url flag\n" << commands::RESET;
            return;
        }
    }

    // If it's a URL with --url flag, open directly without filesystem check
    if (allowUrl &&
        (target_arg.rfind("http://", 0) == 0 ||
         target_arg.rfind("https://", 0) == 0 ||
         target_arg.rfind("ftp://", 0) == 0)) {
        std::wstring wUrl = utf8ToWide(target_arg);
        HINSTANCE result = ShellExecuteW(nullptr, L"open", wUrl.c_str(),
                                         nullptr, nullptr, SW_SHOWNORMAL);
        if (reinterpret_cast<intptr_t>(result) <= 32) {
            err << commands::RED << "open: failed to open URL\n" << commands::RESET;
        }
        return;
    }

    // Validate that target exists on the local filesystem
    fs::path target = fs::weakly_canonical(target_arg);

    std::error_code ec;
    bool exists = fs::exists(target, ec);
    if (ec || !exists) {
        err << commands::RED << "open: '" << target_arg << "' does not exist\n" << commands::RESET;
        return;
    }

    std::wstring wPath = utf8ToWide(target.string());
    HINSTANCE result = ShellExecuteW(nullptr, L"open", wPath.c_str(),
                                     nullptr, nullptr, SW_SHOWNORMAL);
    if (reinterpret_cast<intptr_t>(result) <= 32) {
        err << commands::RED << "open: failed to open '" << target_arg << "'\n" << commands::RESET;
    }
}

void commands::more(const Args& args, std::ostream& out, std::istream& in, std::ostream& err) {
    MoreOptions opts = parseMoreArgs(args, err);
    if (opts.help) {
        printMoreHelp(out);
        return;
    }

    // Non-terminal output: passthrough like cat
    if (!isStdoutTerminal()) {
        if (!opts.files.empty()) {
            for (const std::string& path : opts.files) {
                std::ifstream file(path);
                if (!file.is_open()) {
                    err << "more: " << path << ": No such file or directory\n";
                    continue;
                }
                streamThrough(file, out);
            }
        } else {
            streamThrough(in, out);
        }
        return;
    }

    if (!opts.files.empty()) {
        for (const std::string& path : opts.files) {
            std::ifstream file(path);
            if (!file.is_open()) {
                err << "more: " << path << ": No such file or directory\n";
                continue;
            }
            // Reset options per file so each starts fresh
            MoreOptions fileOpts = opts;
            fileOpts.files.clear();
            runPager(file, out, fileOpts);
        }
    } else {
        runPager(in, out, opts);
    }
}
