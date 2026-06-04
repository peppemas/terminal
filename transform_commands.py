import re

with open('src/Commands.cpp', 'r') as f:
    content = f.read()

# 1. printTailHelp -> add out parameter
content = content.replace(
    'void printTailHelp() {\n    std::cout << "usage: tail [OPTION]... [FILE]...\\n"',
    'void printTailHelp(std::ostream& out) {\n    out << "usage: tail [OPTION]... [FILE]...\\n"'
)

# 2. printTailHeader -> add out parameter
content = content.replace(
    'void printTailHeader(const std::string& filename) {\n    std::cout << "==> " << filename << " <==\\n";',
    'void printTailHeader(const std::string& filename, std::ostream& out) {\n    out << "==> " << filename << " <==\\n";'
)

# 3. printTailBytes -> add out parameter
content = content.replace(
    'void printTailBytes(const std::string& file, std::uintmax_t count) {',
    'void printTailBytes(const std::string& file, std::uintmax_t count, std::ostream& out) {'
)
content = content.replace(
    '    std::cout << in.rdbuf();',
    '    out << in.rdbuf();'
)

# 4. printTailLinesBackward -> add out parameter
content = content.replace(
    'void printTailLinesBackward(const std::string& file, std::uintmax_t count) {',
    'void printTailLinesBackward(const std::string& file, std::uintmax_t count, std::ostream& out) {'
)
# Replace the single std::cout inside it
content = content.replace(
    '        std::cout << line << \'\\n\';\n    }\n}\n\nvoid printTailLinesForward',
    '        out << line << \'\\n\';\n    }\n}\n\nvoid printTailLinesForward'
)

# 5. printTailLinesForward -> add out parameter
content = content.replace(
    'void printTailLinesForward(const std::string& file, std::uintmax_t startLine) {',
    'void printTailLinesForward(const std::string& file, std::uintmax_t startLine, std::ostream& out) {'
)
content = content.replace(
    '            std::cout << line << \'\\n\';\n        }\n        ++currentLine;\n    }\n}\n\nbool isHidden',
    '            out << line << \'\\n\';\n        }\n        ++currentLine;\n    }\n}\n\nbool isHidden'
)

# 6. printEntries -> add out parameter
content = content.replace(
    'void printEntries(const std::vector<DirEntry>& entries, const LsOptions& opts,\n                  std::size_t linkWidth, std::size_t ownerWidth, std::size_t sizeWidth) {',
    'void printEntries(const std::vector<DirEntry>& entries, const LsOptions& opts,\n                  std::size_t linkWidth, std::size_t ownerWidth, std::size_t sizeWidth,\n                  std::ostream& out) {'
)
# Replace std::cout lines inside printEntries
content = content.replace(
    '            std::cout << std::left << std::setw(10) << perm << \' \'',
    '            out << std::left << std::setw(10) << perm << \' \''
)
content = content.replace(
    '                std::cout << std::put_time(tm, "%b %d %H:%M") << \' \';',
    '                out << std::put_time(tm, "%b %d %H:%M") << \' \';'
)
content = content.replace(
    '                std::cout << "??? ?? ??:?? ";',
    '                out << "??? ?? ??:?? ";'
)
content = content.replace(
    '            std::cout << colorFor(e) << e.path.filename().string() << commands::RESET << \'\\n\';',
    '            out << colorFor(e) << e.path.filename().string() << commands::RESET << \'\\n\';'
)
content = content.replace(
    '        for (const auto& e : entries) {\n            std::cout << colorFor(e) << e.path.filename().string() << commands::RESET << \'\\n\';\n        }',
    '        for (const auto& e : entries) {\n            out << colorFor(e) << e.path.filename().string() << commands::RESET << \'\\n\';\n        }'
)

# 7. listDirectory -> add out parameter
content = content.replace(
    'std::vector<DirEntry> listDirectory(const fs::path& dir, const LsOptions& opts, bool printHeader) {',
    'std::vector<DirEntry> listDirectory(const fs::path& dir, const LsOptions& opts, bool printHeader, std::ostream& out) {'
)
content = content.replace(
    '    if (printHeader) {\n        std::cout << dir.string() << ":\\n";\n    }',
    '    if (printHeader) {\n        out << dir.string() << ":\\n";\n    }'
)

# 8. lsRecursive -> add out parameter
content = content.replace(
    'void lsRecursive(const fs::path& dir, const LsOptions& opts) {',
    'void lsRecursive(const fs::path& dir, const LsOptions& opts, std::ostream& out) {'
)
content = content.replace(
    '    auto entries = listDirectory(dir, opts, true);',
    '    auto entries = listDirectory(dir, opts, true, out);'
)
content = content.replace(
    '                lsRecursive(e.path, opts);',
    '                lsRecursive(e.path, opts, out);'
)

# 9. printHelp -> add out parameter
content = content.replace(
    'void printHelp() {',
    'void printHelp(std::ostream& out) {'
)
content = content.replace(
    '    std::cout << "usage: ls [options] [path...]\\n"',
    '    out << "usage: ls [options] [path...]\\n"'
)

# 10. ls command
content = content.replace(
    'void commands::ls(const Args& args)\n{\n    auto opts = parseArgs(args);\n    if (opts.help) {\n        printHelp();\n        return;\n    }\n\n    for (const auto& target : opts.paths) {\n        try {\n            if (opts.recursive && fs::is_directory(target)) {\n                lsRecursive(target, opts);\n            } else {\n                listDirectory(target, opts, opts.paths.size() > 1);\n            }\n        } catch (const fs::filesystem_error& e) {\n            std::cerr << "ls: cannot access \'" << target.string() << "\': " << e.what() << \'\\n\';\n        }\n    }\n}',
    'void commands::ls(const Args& args, std::ostream& out, std::istream& /*in*/)\n{\n    auto opts = parseArgs(args);\n    if (opts.help) {\n        printHelp(out);\n        return;\n    }\n\n    for (const auto& target : opts.paths) {\n        try {\n            if (opts.recursive && fs::is_directory(target)) {\n                lsRecursive(target, opts, out);\n            } else {\n                listDirectory(target, opts, opts.paths.size() > 1, out);\n            }\n        } catch (const fs::filesystem_error& e) {\n            std::cerr << "ls: cannot access \'" << target.string() << "\': " << e.what() << \'\\n\';\n        }\n    }\n}'
)

# 11. rm command
content = content.replace(
    'void commands::rm(const Args& args)\n{\n    if (args.size() < 2) {\n        std::cerr << "usage: rm [-r] <path>\\n";\n        return;\n    }\n\n    bool recursive = false;\n    std::size_t pathIndex = 1;\n    if (args[1] == "-r") {\n        if (args.size() < 3) {\n            std::cerr << "usage: rm [-r] <path>\\n";\n            return;\n        }\n        recursive = true;\n        pathIndex = 2;\n    }\n\n    try {\n        if (recursive) {\n            std::uintmax_t n = fs::remove_all(args[pathIndex]);\n            std::cout << "removed " << n << " items\\n";\n        } else {\n            if (!fs::remove(args[pathIndex])) {\n                std::cerr << "rm: failed to remove \'" << args[pathIndex] << "\'\\n";\n            }\n        }\n    } catch (const fs::filesystem_error& e) {\n        std::cerr << "error: " << e.what() << \'\\n\';\n    }\n}',
    'void commands::rm(const Args& args, std::ostream& out, std::istream& /*in*/)\n{\n    if (args.size() < 2) {\n        std::cerr << "usage: rm [-r] <path>\\n";\n        return;\n    }\n\n    bool recursive = false;\n    std::size_t pathIndex = 1;\n    if (args[1] == "-r") {\n        if (args.size() < 3) {\n            std::cerr << "usage: rm [-r] <path>\\n";\n            return;\n        }\n        recursive = true;\n        pathIndex = 2;\n    }\n\n    try {\n        if (recursive) {\n            std::uintmax_t n = fs::remove_all(args[pathIndex]);\n            out << "removed " << n << " items\\n";\n        } else {\n            if (!fs::remove(args[pathIndex])) {\n                std::cerr << "rm: failed to remove \'" << args[pathIndex] << "\'\\n";\n            }\n        }\n    } catch (const fs::filesystem_error& e) {\n        std::cerr << "error: " << e.what() << \'\\n\';\n    }\n}'
)

# 12. cp command
content = content.replace(
    'void commands::cp(const Args& args)\n{\n    if (args.size() < 3) {\n        std::cerr << "usage: cp <source> <destination>\\n";\n        return;\n    }\n\n    try {\n        fs::copy(args[1], args[2], fs::copy_options::overwrite_existing);\n    } catch (const fs::filesystem_error& e) {\n        std::cerr << "error: " << e.what() << \'\\n\';\n    }\n}',
    'void commands::cp(const Args& args, std::ostream& /*out*/, std::istream& /*in*/)\n{\n    if (args.size() < 3) {\n        std::cerr << "usage: cp <source> <destination>\\n";\n        return;\n    }\n\n    try {\n        fs::copy(args[1], args[2], fs::copy_options::overwrite_existing);\n    } catch (const fs::filesystem_error& e) {\n        std::cerr << "error: " << e.what() << \'\\n\';\n    }\n}'
)

# 13. mv command
content = content.replace(
    'void commands::mv(const Args& args)\n{\n    if (args.size() < 3) {\n        std::cerr << "usage: mv <source> <destination>\\n";\n        return;\n    }\n\n    try {\n        fs::rename(args[1], args[2]);\n    } catch (const fs::filesystem_error& e) {\n        std::cerr << "error: " << e.what() << \'\\n\';\n    }\n}',
    'void commands::mv(const Args& args, std::ostream& /*out*/, std::istream& /*in*/)\n{\n    if (args.size() < 3) {\n        std::cerr << "usage: mv <source> <destination>\\n";\n        return;\n    }\n\n    try {\n        fs::rename(args[1], args[2]);\n    } catch (const fs::filesystem_error& e) {\n        std::cerr << "error: " << e.what() << \'\\n\';\n    }\n}'
)

# 14. cat command
content = content.replace(
    'void commands::cat(const Args& args)\n{\n    // --help\n    for (std::size_t i = 1; i < args.size(); ++i) {\n        if (args[i] == "--help") {\n            std::cout << "usage: cat [file...]\\n"\n                      << "  Concatenate files to standard output.\\n"\n                      << "  Limits: regular files only, max 10 MiB streamed, Ctrl+C to abort.\\n";\n            return;\n        }\n    }\n\n    if (args.size() < 2) {\n        // Unix `cat` with no args is silent — keep that behavior\n        return;\n    }',
    'void commands::cat(const Args& args, std::ostream& out, std::istream& in)\n{\n    // --help\n    for (std::size_t i = 1; i < args.size(); ++i) {\n        if (args[i] == "--help") {\n            out << "usage: cat [file...]\\n"\n                << "  Concatenate files to standard output.\\n"\n                << "  Limits: regular files only, max 10 MiB streamed, Ctrl+C to abort.\\n";\n            return;\n        }\n    }\n\n    if (args.size() < 2) {\n        // When no file arguments are given, read from the provided input stream\n        out << in.rdbuf();\n        return;\n    }'
)
content = content.replace(
    '        std::cout.write(buf.data(), got);',
    '        out.write(buf.data(), got);'
)
content = content.replace(
    '                std::cout << "^C\\n";',
    '                out << "^C\\n";'
)

# 15. tail command
content = content.replace(
    'void commands::tail(const Args& args)\n{\n    TailOptions opts = parseTailArgs(args);\n    if (opts.help) {\n        printTailHelp();\n        return;\n    }\n    if (opts.files.empty()) {\n        std::cerr << "usage: tail [OPTION]… [FILE]…\\n";\n        return;\n    }\n    if (opts.conflict) {\n        std::cerr << "tail: lines and bytes options are mutually exclusive\\n";\n        return;\n    }\n\n    std::size_t totalFiles = opts.files.size();\n    for (const std::string& filename : opts.files) {\n        if (shouldPrintHeader(totalFiles, opts)) {\n            printTailHeader(filename);\n        }\n        std::ifstream test(filename, std::ios::binary);\n        if (!test.is_open()) {\n            std::cerr << "tail: cannot open \'" << filename << "\'\\n";\n            continue;\n        }\n        test.close();\n\n        if (opts.mode == CountMode::Bytes) {\n            printTailBytes(filename, opts.count);\n        } else if (opts.fromStart) {\n            printTailLinesForward(filename, opts.count);\n        } else {\n            printTailLinesBackward(filename, opts.count);\n        }\n    }\n}',
    'void commands::tail(const Args& args, std::ostream& out, std::istream& /*in*/)\n{\n    TailOptions opts = parseTailArgs(args);\n    if (opts.help) {\n        printTailHelp(out);\n        return;\n    }\n    if (opts.files.empty()) {\n        std::cerr << "usage: tail [OPTION]… [FILE]…\\n";\n        return;\n    }\n    if (opts.conflict) {\n        std::cerr << "tail: lines and bytes options are mutually exclusive\\n";\n        return;\n    }\n\n    std::size_t totalFiles = opts.files.size();\n    for (const std::string& filename : opts.files) {\n        if (shouldPrintHeader(totalFiles, opts)) {\n            printTailHeader(filename, out);\n        }\n        std::ifstream test(filename, std::ios::binary);\n        if (!test.is_open()) {\n            std::cerr << "tail: cannot open \'" << filename << "\'\\n";\n            continue;\n        }\n        test.close();\n\n        if (opts.mode == CountMode::Bytes) {\n            printTailBytes(filename, opts.count, out);\n        } else if (opts.fromStart) {\n            printTailLinesForward(filename, opts.count, out);\n        } else {\n            printTailLinesBackward(filename, opts.count, out);\n        }\n    }\n}'
)

# 16. grep command
content = content.replace(
    'void commands::grep(const Args& args)\n{\n    if (args.size() < 3) {\n        std::cerr << "usage: grep <pattern> <file>\\n";\n        return;\n    }\n\n    std::regex re;\n    try {\n        re = std::regex(args[1]);\n    } catch (const std::regex_error& e) {\n        std::cerr << "grep: invalid regex: " << e.what() << \'\\n\';\n        return;\n    }\n\n    std::ifstream file(args[2]);\n    if (!file.is_open()) {\n        std::cerr << "grep: cannot open \'" << args[2] << "\'\\n";\n        return;\n    }\n\n    std::string line;\n    while (std::getline(file, line)) {\n        std::string output;\n        std::string::const_iterator searchStart(line.cbegin());\n        std::smatch match;\n        bool found = false;\n\n        while (std::regex_search(searchStart, line.cend(), match, re)) {\n            found = true;\n            output.append(searchStart, match[0].first);\n            output += RED;\n            output += match[0].str();\n            output += RESET;\n            searchStart = match[0].second;\n        }\n\n        if (found) {\n            output.append(searchStart, line.cend());\n            std::cout << output << \'\\n\';\n        }\n    }\n}',
    'void commands::grep(const Args& args, std::ostream& out, std::istream& in)\n{\n    if (args.size() < 2) {\n        std::cerr << "usage: grep <pattern> [file]\\n";\n        return;\n    }\n\n    std::regex re;\n    try {\n        re = std::regex(args[1]);\n    } catch (const std::regex_error& e) {\n        std::cerr << "grep: invalid regex: " << e.what() << \'\\n\';\n        return;\n    }\n\n    std::ifstream file;\n    std::istream* input = &in;\n    if (args.size() >= 3) {\n        file.open(args[2]);\n        if (!file.is_open()) {\n            std::cerr << "grep: cannot open \'" << args[2] << "\'\\n";\n            return;\n        }\n        input = &file;\n    }\n\n    std::string line;\n    while (std::getline(*input, line)) {\n        std::string output;\n        std::string::const_iterator searchStart(line.cbegin());\n        std::smatch match;\n        bool found = false;\n\n        while (std::regex_search(searchStart, line.cend(), match, re)) {\n            found = true;\n            output.append(searchStart, match[0].first);\n            output += RED;\n            output += match[0].str();\n            output += RESET;\n            searchStart = match[0].second;\n        }\n\n        if (found) {\n            output.append(searchStart, line.cend());\n            out << output << \'\\n\';\n        }\n    }\n}'
)

# 17. cd command
content = content.replace(
    'void commands::cd(const Args& args)\n{\n    if (args.size() < 2) {\n        std::cerr << "usage: cd <path>\\n";\n        return;\n    }\n\n    std::string target = args[1];\n\n    if (target.size() == 2 && target[1] == \':\') {\n        target += \'\\\\\';\n    }\n\n    try {\n        fs::current_path(target);\n    } catch (const fs::filesystem_error& e) {\n        std::cerr << "error: " << e.what() << \'\\n\';\n    }\n}',
    'void commands::cd(const Args& args, std::ostream& /*out*/, std::istream& /*in*/)\n{\n    if (args.size() < 2) {\n        std::cerr << "usage: cd <path>\\n";\n        return;\n    }\n\n    std::string target = args[1];\n\n    if (target.size() == 2 && target[1] == \':\') {\n        target += \'\\\\\';\n    }\n\n    try {\n        fs::current_path(target);\n    } catch (const fs::filesystem_error& e) {\n        std::cerr << "error: " << e.what() << \'\\n\';\n    }\n}'
)

# 18. clear command
content = content.replace(
    'void commands::clear(const Args& /*args*/)\n{\n    // Clear screen, clear scrollback, and move cursor to home (cross-platform ANSI)\n    std::cout << "\\x1b[2J\\x1b[3J\\x1b[H" << std::flush;\n}',
    'void commands::clear(const Args& /*args*/, std::ostream& out, std::istream& /*in*/)\n{\n    // Clear screen, clear scrollback, and move cursor to home (cross-platform ANSI)\n    out << "\\x1b[2J\\x1b[3J\\x1b[H" << std::flush;\n}'
)

# 19. pwd command
content = content.replace(
    'void commands::pwd(const Args& /*args*/)\n{\n    std::cout << std::filesystem::current_path().string() << \'\\n\';\n}',
    'void commands::pwd(const Args& /*args*/, std::ostream& out, std::istream& /*in*/)\n{\n    out << std::filesystem::current_path().string() << \'\\n\';\n}'
)

# 20. open command
content = content.replace(
    'void commands::open(const Args& args)\n{\n    if (args.size() < 2) {\n        std::cerr << commands::RED << "open: missing directory operand\\n" << commands::RESET;\n        return;\n    }\n    if (args.size() > 2) {\n        std::cerr << commands::RED << "open: too many arguments\\n" << commands::RESET;\n        return;\n    }\n\n    fs::path target = fs::weakly_canonical(args[1]);\n\n    std::error_code ec;\n    bool exists = fs::exists(target, ec);\n    if (ec || !exists) {\n        std::cerr << commands::RED << "open: \'" << args[1] << "\' does not exist\\n" << commands::RESET;\n        return;\n    }\n    bool isDir = fs::is_directory(target, ec);\n    if (ec || !isDir) {\n        std::cerr << commands::RED << "open: \'" << args[1] << "\' is not a directory\\n" << commands::RESET;\n        return;\n    }\n\n    std::wstring wPath = utf8ToWide(target.string());\n    HINSTANCE result = ShellExecuteW(nullptr, L"open", wPath.c_str(),\n                                     nullptr, nullptr, SW_SHOWNORMAL);\n    if (reinterpret_cast<intptr_t>(result) <= 32) {\n        std::cerr << commands::RED << "open: failed to launch file explorer\\n" << commands::RESET;\n    }\n}',
    'void commands::open(const Args& args, std::ostream& /*out*/, std::istream& /*in*/)\n{\n    if (args.size() < 2) {\n        std::cerr << commands::RED << "open: missing directory operand\\n" << commands::RESET;\n        return;\n    }\n    if (args.size() > 2) {\n        std::cerr << commands::RED << "open: too many arguments\\n" << commands::RESET;\n        return;\n    }\n\n    fs::path target = fs::weakly_canonical(args[1]);\n\n    std::error_code ec;\n    bool exists = fs::exists(target, ec);\n    if (ec || !exists) {\n        std::cerr << commands::RED << "open: \'" << args[1] << "\' does not exist\\n" << commands::RESET;\n        return;\n    }\n    bool isDir = fs::is_directory(target, ec);\n    if (ec || !isDir) {\n        std::cerr << commands::RED << "open: \'" << args[1] << "\' is not a directory\\n" << commands::RESET;\n        return;\n    }\n\n    std::wstring wPath = utf8ToWide(target.string());\n    HINSTANCE result = ShellExecuteW(nullptr, L"open", wPath.c_str(),\n                                     nullptr, nullptr, SW_SHOWNORMAL);\n    if (reinterpret_cast<intptr_t>(result) <= 32) {\n        std::cerr << commands::RED << "open: failed to launch file explorer\\n" << commands::RESET;\n    }\n}'
)

# 21. Add echo command at the end
if 'void commands::echo' not in content:
    content = content.rstrip() + '\n\nvoid commands::echo(const Args& args, std::ostream& out, std::istream& /*in*/)\n{\n    for (std::size_t i = 1; i < args.size(); ++i) {\n        if (i > 1) out << \' \';\n        out << args[i];\n    }\n    out << \'\\n\';\n}\n'

with open('src/Commands.cpp', 'w') as f:
    f.write(content)

print("Done")
