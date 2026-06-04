#include "Commands.hpp"

#include <iostream>
#include <fstream>
#include <filesystem>
#include <regex>
#include <cstdint>
#include <utility>
#include <chrono>
#include <iomanip>
#include <algorithm>
#include <cstdio>
#include <ctime>
#include <system_error>

#define NOMINMAX

#include <windows.h>
#include <shellapi.h>

namespace fs = std::filesystem;

namespace {

struct LsOptions {
    bool all        = false;
    bool long_fmt   = false;
    bool human      = false;
    bool reverse    = false;
    bool time_sort  = false;
    bool size_sort  = false;
    bool recursive  = false;
    bool help       = false;
    std::vector<fs::path> paths;
};

struct DirEntry {
    fs::path path;
    fs::file_status status;
    std::uintmax_t size;
    fs::file_time_type mtime;
    std::uintmax_t nlink;
    std::string owner;
    bool is_symlink;
    bool is_hidden;
};

LsOptions parseArgs(const commands::Args& args) {
    LsOptions opts;
    for (std::size_t i = 1; i < args.size(); ++i) {
        const std::string& token = args[i];
        if (token == "--help") {
            opts.help = true;
            return opts;
        }
        if (token.size() > 1 && token[0] == '-') {
            for (std::size_t j = 1; j < token.size(); ++j) {
                char c = token[j];
                switch (c) {
                    case 'a': opts.all = true; break;
                    case 'l': opts.long_fmt = true; break;
                    case 'h': opts.human = true; break;
                    case 'r': opts.reverse = true; break;
                    case 't': opts.time_sort = true; break;
                    case 'S': opts.size_sort = true; break;
                    case 'R': opts.recursive = true; break;
                    default:
                        std::cerr << "ls: invalid option -- '" << c << "'\n";
                        break;
                }
            }
        } else {
            opts.paths.push_back(token);
        }
    }
    if (opts.paths.empty()) {
        opts.paths.push_back(".");
    }
    return opts;
}

bool isHidden(const fs::path& p) {
    std::string name = p.filename().string();
    return !name.empty() && name[0] == '.';
}

std::string permissionString(const fs::file_status& s) {
    std::string perm(10, '-');

    if (fs::is_directory(s)) {
        perm[0] = 'd';
    } else if (fs::is_symlink(s)) {
        perm[0] = 'l';
    } else if (fs::is_character_file(s)) {
        perm[0] = 'c';
    } else if (fs::is_block_file(s)) {
        perm[0] = 'b';
    } else if (fs::is_fifo(s)) {
        perm[0] = 'p';
    } else if (fs::is_socket(s)) {
        perm[0] = 's';
    }

    auto p = s.permissions();
    auto has = [&](fs::perms bit) { return (p & bit) != fs::perms::none; };

    perm[1] = has(fs::perms::owner_read)  ? 'r' : '-';
    perm[2] = has(fs::perms::owner_write) ? 'w' : '-';
    perm[3] = has(fs::perms::owner_exec)  ? 'x' : '-';
    perm[4] = has(fs::perms::group_read)  ? 'r' : '-';
    perm[5] = has(fs::perms::group_write) ? 'w' : '-';
    perm[6] = has(fs::perms::group_exec)  ? 'x' : '-';
    perm[7] = has(fs::perms::others_read)  ? 'r' : '-';
    perm[8] = has(fs::perms::others_write) ? 'w' : '-';
    perm[9] = has(fs::perms::others_exec)  ? 'x' : '-';

    return perm;
}

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

std::string humanReadableSize(std::uintmax_t bytes) {
    if (bytes < 1024) {
        return std::to_string(bytes);
    }

    const char* units[] = {"K", "M", "G"};
    double size = static_cast<double>(bytes);
    int unitIndex = -1;

    while (size >= 1024.0 && unitIndex < 2) {
        size /= 1024.0;
        ++unitIndex;
    }

    char buf[32];
    std::snprintf(buf, sizeof(buf), "%.1f%s", size, units[unitIndex]);
    return std::string(buf);
}

std::string colorFor(const DirEntry& e) {
    if (e.is_symlink) {
        return commands::CYAN;
    }
    if (fs::is_directory(e.status)) {
        return commands::BOLD_BLUE;
    }
    if (fs::is_regular_file(e.status)) {
        auto p = e.status.permissions();
        if ((p & fs::perms::owner_exec) != fs::perms::none) {
            return commands::BOLD_GREEN;
        }
    }
    if (e.is_hidden) {
        return commands::MAGENTA;
    }
    return "";
}

void collectEntries(const fs::path& dir, const LsOptions& opts, std::vector<DirEntry>& out) {
    try {
        for (const auto& entry : fs::directory_iterator(dir)) {
            if (!opts.all && isHidden(entry.path())) {
                continue;
            }

            DirEntry de;
            de.path = entry.path();
            de.status = entry.status();
            de.is_symlink = fs::is_symlink(entry.symlink_status());
            de.is_hidden = isHidden(entry.path());

            if (fs::is_regular_file(de.status)) {
                de.size = entry.file_size();
            } else {
                de.size = 0;
            }

            de.mtime = entry.last_write_time();

            try {
                de.nlink = entry.hard_link_count();
            } catch (...) {
                de.nlink = 1;
            }

            de.owner = "";
            out.push_back(std::move(de));
        }
    } catch (const fs::filesystem_error& e) {
        std::cerr << "ls: cannot access '" << dir.string() << "': " << e.what() << '\n';
    }
}

void sortEntries(std::vector<DirEntry>& entries, const LsOptions& opts) {
    auto cmp = [](const DirEntry& a, const DirEntry& b, const LsOptions& o) {
        if (o.time_sort) {
            return a.mtime > b.mtime;
        }
        if (o.size_sort) {
            return a.size > b.size;
        }
        return a.path.filename().string() < b.path.filename().string();
    };

    if (opts.reverse) {
        std::sort(entries.begin(), entries.end(),
                  [&opts, &cmp](const DirEntry& a, const DirEntry& b) {
                      return cmp(b, a, opts);
                  });
    } else {
        std::sort(entries.begin(), entries.end(),
                  [&opts, &cmp](const DirEntry& a, const DirEntry& b) {
                      return cmp(a, b, opts);
                  });
    }
}

void printEntries(const std::vector<DirEntry>& entries, const LsOptions& opts,
                  std::size_t linkWidth, std::size_t ownerWidth, std::size_t sizeWidth) {
    if (entries.empty()) {
        return;
    }

    if (opts.long_fmt) {
        for (const auto& e : entries) {
            std::string perm = permissionString(e.status);
            std::string sizeStr = opts.human ? humanReadableSize(e.size)
                                              : std::to_string(e.size);

            auto sctp = std::chrono::time_point_cast<std::chrono::system_clock::duration>(
                e.mtime - std::filesystem::file_time_type::clock::now() + std::chrono::system_clock::now()
            );
            std::time_t t = std::chrono::system_clock::to_time_t(sctp);
            std::tm* tm = std::localtime(&t);

            std::cout << std::left << std::setw(10) << perm << ' '
                      << std::right << std::setw(static_cast<int>(linkWidth)) << e.nlink << ' '
                      << std::left << std::setw(static_cast<int>(ownerWidth)) << e.owner << ' '
                      << std::right << std::setw(static_cast<int>(sizeWidth)) << sizeStr << ' ';

            if (tm) {
                std::cout << std::put_time(tm, "%b %d %H:%M") << ' ';
            } else {
                std::cout << "??? ?? ??:?? ";
            }

            std::cout << colorFor(e) << e.path.filename().string() << commands::RESET << '\n';
        }
    } else {
        for (const auto& e : entries) {
            std::cout << colorFor(e) << e.path.filename().string() << commands::RESET << '\n';
        }
    }
}

std::vector<DirEntry> listDirectory(const fs::path& dir, const LsOptions& opts, bool printHeader) {
    if (printHeader) {
        std::cout << dir.string() << ":\n";
    }

    std::vector<DirEntry> entries;

    if (!fs::is_directory(dir)) {
        try {
            DirEntry de;
            de.path = dir;
            de.status = fs::status(dir);
            de.is_symlink = fs::is_symlink(fs::symlink_status(dir));
            de.is_hidden = isHidden(dir);

            if (fs::is_regular_file(de.status)) {
                de.size = fs::file_size(dir);
            } else {
                de.size = 0;
            }

            de.mtime = fs::last_write_time(dir);
            de.nlink = 1;
            de.owner = "";
            entries.push_back(std::move(de));
        } catch (const fs::filesystem_error& e) {
            std::cerr << "ls: cannot access '" << dir.string() << "': " << e.what() << '\n';
            return entries;
        }
    } else {
        collectEntries(dir, opts, entries);
    }

    sortEntries(entries, opts);

    std::size_t linkWidth = 0, ownerWidth = 0, sizeWidth = 0;
    if (opts.long_fmt) {
        for (const auto& e : entries) {
            linkWidth = std::max(linkWidth, std::to_string(e.nlink).length());
            ownerWidth = std::max(ownerWidth, e.owner.length());
            std::string sizeStr = opts.human ? humanReadableSize(e.size)
                                              : std::to_string(e.size);
            sizeWidth = std::max(sizeWidth, sizeStr.length());
        }
    }

    printEntries(entries, opts, linkWidth, ownerWidth, sizeWidth);
    return entries;
}

void lsRecursive(const fs::path& dir, const LsOptions& opts) {
    auto entries = listDirectory(dir, opts, true);
    for (const auto& e : entries) {
        if (fs::is_directory(e.status)) {
            try {
                lsRecursive(e.path, opts);
            } catch (const fs::filesystem_error& err) {
                std::cerr << "ls: cannot access '" << e.path.string() << "': " << err.what() << '\n';
            }
        }
    }
}

void printHelp() {
    std::cout << "usage: ls [options] [path...]\n"
              << "  -a    include hidden entries\n"
              << "  -l    long listing format\n"
              << "  -h    human-readable sizes (with -l)\n"
              << "  -r    reverse sort order\n"
              << "  -t    sort by modification time\n"
              << "  -S    sort by file size\n"
              << "  -R    list subdirectories recursively\n";
}

} // anonymous namespace

void commands::ls(const Args& args)
{
    auto opts = parseArgs(args);
    if (opts.help) {
        printHelp();
        return;
    }

    for (const auto& target : opts.paths) {
        try {
            if (opts.recursive && fs::is_directory(target)) {
                lsRecursive(target, opts);
            } else {
                listDirectory(target, opts, opts.paths.size() > 1);
            }
        } catch (const fs::filesystem_error& e) {
            std::cerr << "ls: cannot access '" << target.string() << "': " << e.what() << '\n';
        }
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

void commands::pwd(const Args& /*args*/)
{
    std::cout << std::filesystem::current_path().string() << '\n';
}

void commands::open(const Args& args)
{
    if (args.size() < 2) {
        std::cerr << commands::RED << "open: missing directory operand\n" << commands::RESET;
        return;
    }
    if (args.size() > 2) {
        std::cerr << commands::RED << "open: too many arguments\n" << commands::RESET;
        return;
    }

    fs::path target = fs::weakly_canonical(args[1]);

    std::error_code ec;
    bool exists = fs::exists(target, ec);
    if (ec || !exists) {
        std::cerr << commands::RED << "open: '" << args[1] << "' does not exist\n" << commands::RESET;
        return;
    }
    bool isDir = fs::is_directory(target, ec);
    if (ec || !isDir) {
        std::cerr << commands::RED << "open: '" << args[1] << "' is not a directory\n" << commands::RESET;
        return;
    }

    std::wstring wPath = utf8ToWide(target.string());
    HINSTANCE result = ShellExecuteW(nullptr, L"open", wPath.c_str(),
                                     nullptr, nullptr, SW_SHOWNORMAL);
    if (reinterpret_cast<intptr_t>(result) <= 32) {
        std::cerr << commands::RED << "open: failed to launch file explorer\n" << commands::RESET;
    }
}
