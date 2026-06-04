#include "Commands.hpp"

#include <iostream>
#include <fstream>
#include <filesystem>
#include <regex>

namespace fs = std::filesystem;

void commands::ls(const Args& args)
{
    fs::path target = ".";
    if (args.size() >= 2) {
        target = args[1];
    }

    try {
        for (const auto& entry : fs::directory_iterator(target)) {
            if (fs::is_directory(entry.status())) {
                std::cout << BOLD_BLUE << entry.path().filename().string() << RESET << '\n';
            } else {
                std::cout << entry.path().filename().string() << '\n';
            }
        }
    } catch (const fs::filesystem_error& e) {
        std::cerr << "error: " << e.what() << '\n';
    }
}

void commands::rm(const Args& args)
{
    if (args.size() < 2) {
        std::cerr << "usage: rm [-r] <path>\n";
        return;
    }

    bool recursive = false;
    std::size_t pathIndex = 1;
    if (args[1] == "-r") {
        if (args.size() < 3) {
            std::cerr << "usage: rm [-r] <path>\n";
            return;
        }
        recursive = true;
        pathIndex = 2;
    }

    try {
        if (recursive) {
            std::uintmax_t n = fs::remove_all(args[pathIndex]);
            std::cout << "removed " << n << " items\n";
        } else {
            if (!fs::remove(args[pathIndex])) {
                std::cerr << "rm: failed to remove '" << args[pathIndex] << "'\n";
            }
        }
    } catch (const fs::filesystem_error& e) {
        std::cerr << "error: " << e.what() << '\n';
    }
}

void commands::cp(const Args& args)
{
    if (args.size() < 3) {
        std::cerr << "usage: cp <source> <destination>\n";
        return;
    }

    try {
        fs::copy(args[1], args[2], fs::copy_options::overwrite_existing);
    } catch (const fs::filesystem_error& e) {
        std::cerr << "error: " << e.what() << '\n';
    }
}

void commands::mv(const Args& args)
{
    if (args.size() < 3) {
        std::cerr << "usage: mv <source> <destination>\n";
        return;
    }

    try {
        fs::rename(args[1], args[2]);
    } catch (const fs::filesystem_error& e) {
        std::cerr << "error: " << e.what() << '\n';
    }
}

void commands::cat(const Args& args)
{
    if (args.size() < 2) {
        std::cerr << "usage: cat <file>\n";
        return;
    }

    std::ifstream file(args[1]);
    if (!file.is_open()) {
        std::cerr << "cat: cannot open '" << args[1] << "'\n";
        return;
    }

    std::cout << file.rdbuf();
}

void commands::tail(const Args& args)
{
    if (args.size() < 2) {
        std::cerr << "usage: tail <file>\n";
        return;
    }

    std::ifstream file(args[1], std::ios::binary);
    if (!file.is_open()) {
        std::cerr << "tail: cannot open '" << args[1] << "'\n";
        return;
    }

    file.seekg(0, std::ios::end);
    std::streamoff size = file.tellg();
    if (size <= 0) {
        return;
    }

    const int targetLines = 10;
    std::streamoff pos = size - 1;

    // If the file ends with a newline, ignore it for counting purposes
    // because it terminates the last line rather than creating an empty one.
    file.seekg(pos);
    char lastChar = 0;
    file.read(&lastChar, 1);
    if (lastChar == '\n') {
        --pos;
    }

    int newlineCount = 0;
    while (pos >= 0) {
        file.seekg(pos);
        char ch = 0;
        file.read(&ch, 1);
        if (ch == '\n') {
            ++newlineCount;
            if (newlineCount == targetLines) {
                break;
            }
        }
        --pos;
    }

    std::streamoff startPos = 0;
    if (newlineCount == targetLines) {
        startPos = pos + 1;
    } else {
        startPos = 0;
    }

    file.seekg(startPos);
    file.clear();

    std::string line;
    while (std::getline(file, line)) {
        std::cout << line << '\n';
    }
}

void commands::grep(const Args& args)
{
    if (args.size() < 3) {
        std::cerr << "usage: grep <pattern> <file>\n";
        return;
    }

    std::regex re;
    try {
        re = std::regex(args[1]);
    } catch (const std::regex_error& e) {
        std::cerr << "grep: invalid regex: " << e.what() << '\n';
        return;
    }

    std::ifstream file(args[2]);
    if (!file.is_open()) {
        std::cerr << "grep: cannot open '" << args[2] << "'\n";
        return;
    }

    std::string line;
    while (std::getline(file, line)) {
        std::string output;
        std::string::const_iterator searchStart(line.cbegin());
        std::smatch match;
        bool found = false;

        while (std::regex_search(searchStart, line.cend(), match, re)) {
            found = true;
            output.append(searchStart, match[0].first);
            output += RED;
            output += match[0].str();
            output += RESET;
            searchStart = match[0].second;
        }

        if (found) {
            output.append(searchStart, line.cend());
            std::cout << output << '\n';
        }
    }
}

void commands::cd(const Args& args)
{
    if (args.size() < 2) {
        std::cerr << "usage: cd <path>\n";
        return;
    }

    std::string target = args[1];

    if (target.size() == 2 && target[1] == ':') {
        target += '\\';
    }

    try {
        fs::current_path(target);
    } catch (const fs::filesystem_error& e) {
        std::cerr << "error: " << e.what() << '\n';
    }
}

void commands::clear(const Args& /*args*/)
{
    std::cout << "\x1b[2J\x1b[H";
}
