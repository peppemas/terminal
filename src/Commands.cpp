#include "Commands.hpp"

#include <iostream>
#include <fstream>
#include <filesystem>
#include <regex>
#include <cstdint>
#include <utility>
#include <array>
#include <optional>
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

static std::array<std::optional<std::filesystem::path>, 10> g_dirSlots;

bool parseSlotIndex(const std::string& token, int& outIndex) {
    if (token.size() == 1 && token[0] >= '0' && token[0] <= '9') {
        outIndex = token[0] - '0';
        return true;
    }
    return false;
}

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

struct RmOptions {
    bool recursive = false;
    bool help      = false;
    bool error     = false;
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

TailOptions parseTailArgs(const commands::Args& args) {
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
                std::cerr << "tail: option '" << token << "' requires an argument\n";
                return opts;
            }
            const std::string& val = args[++i];
            if (!val.empty() && val[0] == '+') {
                try {
                    opts.count = std::stoull(val.substr(1));
                } catch (...) {
                    std::cerr << "tail: invalid number: '" << val << "'\n";
                    return opts;
                }
                opts.fromStart = true;
            } else {
                try {
                    opts.count = std::stoull(val);
                } catch (...) {
                    std::cerr << "tail: invalid number: '" << val << "'\n";
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
                std::cerr << "tail: option '--lines=' requires a value\n";
                return opts;
            }
            if (!val.empty() && val[0] == '+') {
                try {
                    opts.count = std::stoull(val.substr(1));
                } catch (...) {
                    std::cerr << "tail: invalid number: '" << val << "'\n";
                    return opts;
                }
                opts.fromStart = true;
            } else {
                try {
                    opts.count = std::stoull(val);
                } catch (...) {
                    std::cerr << "tail: invalid number: '" << val << "'\n";
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
                std::cerr << "tail: option '" << token << "' requires an argument\n";
                return opts;
            }
            const std::string& val = args[++i];
            try {
                opts.count = std::stoull(val);
            } catch (...) {
                std::cerr << "tail: invalid number: '" << val << "'\n";
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
                std::cerr << "tail: option '--bytes=' requires a value\n";
                return opts;
            }
            try {
                opts.count = std::stoull(val);
            } catch (...) {
                std::cerr << "tail: invalid number: '" << val << "'\n";
                return opts;
            }
            if (opts.mode == CountMode::Lines) {
                opts.conflict = true;
            }
            opts.mode = CountMode::Bytes;
            continue;
        }
        if (!token.empty() && token[0] == '-') {
            std::cerr << "tail: invalid option: '" << token << "'\n";
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

    std::streamoff pos = size - 1;
    in.seekg(pos);
    char lastChar = 0;
    in.read(&lastChar, 1);
    if (lastChar == '\n') {
        --pos;
    }

    std::uintmax_t foundCount = 0;
    while (pos >= 0) {
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
    std::error_code ec;
    fs::directory_iterator it(dir, ec);
    if (ec) {
        std::cerr << "ls: cannot open directory '" << dir.string() << "': " << ec.message() << '\n';
        return;
    }

    fs::directory_iterator end;
    while (it != end) {
        const auto& entry = *it;

        // Filter hidden files first -- if we can't even read the name, skip.
        std::error_code pathEc;
        fs::path entryPath = entry.path();
        if (!opts.all && isHidden(entryPath)) {
            it.increment(ec);
            if (ec) {
                std::cerr << "ls: cannot advance iterator in '" << dir.string() << "': " << ec.message() << '\n';
                return;
            }
            continue;
        }

        // status() -- non-throwing
        std::error_code statusEc;
        fs::file_status st = entry.status(statusEc);

        // symlink_status() -- non-throwing
        std::error_code symlinkEc;
        fs::file_status symst = entry.symlink_status(symlinkEc);

        // Skip the entry outright if the most basic stat failed (file doesn't exist anymore).
        if (statusEc && symlinkEc) {
            std::cerr << "ls: cannot stat '" << entryPath.string() << "': " << statusEc.message() << '\n';
            it.increment(ec);
            if (ec) {
                std::cerr << "ls: cannot advance iterator in '" << dir.string() << "': " << ec.message() << '\n';
                return;
            }
            continue;
        }

        DirEntry de;
        de.path = entryPath;
        de.status = st;
        de.is_symlink = !symlinkEc && fs::is_symlink(symst);
        de.is_hidden = isHidden(entryPath);

        // file_size() -- non-throwing
        if (!statusEc && fs::is_regular_file(st)) {
            std::error_code sizeEc;
            de.size = entry.file_size(sizeEc);
            if (sizeEc) {
                de.size = 0;
            }
        } else {
            de.size = 0;
        }

        // last_write_time() -- non-throwing
        std::error_code mtimeEc;
        de.mtime = entry.last_write_time(mtimeEc);
        if (mtimeEc) {
            std::cerr << "ls: cannot stat '" << entryPath.string() << "' (mtime): " << mtimeEc.message() << '\n';
            de.mtime = fs::file_time_type::clock::now();  // fallback
        }

        // hard_link_count() -- non-throwing
        std::error_code nlinkEc;
        de.nlink = entry.hard_link_count(nlinkEc);
        if (nlinkEc) {
            de.nlink = 1;
        }

        de.owner = "";
        out.push_back(std::move(de));

        it.increment(ec);
        if (ec) {
            std::cerr << "ls: cannot advance iterator in '" << dir.string() << "': " << ec.message() << '\n';
            return;
        }
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
                  std::size_t linkWidth, std::size_t ownerWidth, std::size_t sizeWidth,
                  std::ostream& out) {
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

            out << std::left << std::setw(10) << perm << ' '
                      << std::right << std::setw(static_cast<int>(linkWidth)) << e.nlink << ' '
                      << std::left << std::setw(static_cast<int>(ownerWidth)) << e.owner << ' '
                      << std::right << std::setw(static_cast<int>(sizeWidth)) << sizeStr << ' ';

            if (tm) {
                out << std::put_time(tm, "%b %d %H:%M") << ' ';
            } else {
                out << "??? ?? ??:?? ";
            }

            out << colorFor(e) << e.path.filename().string() << commands::RESET << '\n';
        }
    } else {
        for (const auto& e : entries) {
            out << colorFor(e) << e.path.filename().string() << commands::RESET << '\n';
        }
    }
}

std::vector<DirEntry> listDirectory(const fs::path& dir, const LsOptions& opts, bool printHeader, std::ostream& out) {
    if (printHeader) {
        out << dir.string() << ":\n";
    }

    std::vector<DirEntry> entries;

    if (!fs::is_directory(dir)) {
        std::error_code statusEc;
        fs::file_status st = fs::status(dir, statusEc);
        if (statusEc) {
            std::cerr << "ls: cannot stat '" << dir.string() << "': " << statusEc.message() << '\n';
            return entries;
        }

        std::error_code symlinkEc;
        fs::file_status symst = fs::symlink_status(dir, symlinkEc);

        DirEntry de;
        de.path = dir;
        de.status = st;
        de.is_symlink = !symlinkEc && fs::is_symlink(symst);
        de.is_hidden = isHidden(dir);

        if (fs::is_regular_file(st)) {
            std::error_code sizeEc;
            de.size = fs::file_size(dir, sizeEc);
            if (sizeEc) de.size = 0;
        } else {
            de.size = 0;
        }

        std::error_code mtimeEc;
        de.mtime = fs::last_write_time(dir, mtimeEc);
        if (mtimeEc) {
            std::cerr << "ls: cannot stat '" << dir.string() << "' (mtime): " << mtimeEc.message() << '\n';
            de.mtime = fs::file_time_type::clock::now();
        }

        de.nlink = 1;
        de.owner = "";
        entries.push_back(std::move(de));
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

    printEntries(entries, opts, linkWidth, ownerWidth, sizeWidth, out);
    return entries;
}

void lsRecursive(const fs::path& dir, const LsOptions& opts, std::ostream& out) {
    auto entries = listDirectory(dir, opts, false, out);
    for (const auto& e : entries) {
        if (fs::is_directory(e.status)) {
            try {
                lsRecursive(e.path, opts, out);
            } catch (const fs::filesystem_error& err) {
                std::cerr << "ls: cannot access '" << e.path.string() << "': " << err.what() << '\n';
            }
        }
    }
}

// ------------------------------------------------------------------
// rm helpers: argument parsing and Recycle Bin integration
// ------------------------------------------------------------------

RmOptions parseRmArgs(const commands::Args& args) {
    RmOptions opts;
    bool operandsCollected = false;
    for (std::size_t i = 1; i < args.size(); ++i) {
        const std::string& token = args[i];
        if (token == "-r" && !operandsCollected) {
            opts.recursive = true;
        } else if (token.size() >= 1 && token[0] == '-') {
            std::cerr << "rm: invalid option -- '" << token << "'\n";
            opts.error = true;
        } else {
            opts.paths.push_back(token);
            operandsCollected = true;
        }
    }
    if (opts.paths.empty() && !opts.error) {
        std::cerr << "usage: rm [-r] <path>...\n";
        opts.error = true;
    }
    return opts;
}

bool moveToRecycleBin(const std::filesystem::path& p, bool recursive, std::ostream& err) {
    std::error_code ec;
    if (!fs::exists(p, ec) || ec) {
        err << "rm: cannot remove '" << p.string() << "': No such file or directory\n";
        return false;
    }

    if (fs::is_directory(p, ec) && !recursive) {
        // Check emptiness
        auto it = fs::directory_iterator(p, ec);
        if (!ec && it != fs::directory_iterator()) {
            err << "rm: cannot remove '" << p.string() << "': Is a directory\n";
            return false;
        }
    }

    // Convert to absolute wide path with double null termination
    fs::path abs = fs::absolute(p);
    std::wstring wsrc = abs.wstring();
    wsrc.push_back(L'\0');  // second terminator required by SHFileOperationW

    SHFILEOPSTRUCTW op = {};
    op.wFunc = FO_DELETE;
    op.pFrom = wsrc.c_str();
    op.fFlags = FOF_ALLOWUNDO | FOF_NOCONFIRMATION | FOF_SILENT | FOF_NOERRORUI;

    int ret = SHFileOperationW(&op);
    if (ret != 0 || op.fAnyOperationsAborted) {
        err << "rm: failed to move '" << p.string() << "' to Recycle Bin\n";
        return false;
    }
    return true;
}

void printHelp(std::ostream& out) {
    out << "usage: ls [options] [path...]\n"
              << "  -a    include hidden entries\n"
              << "  -l    long listing format\n"
              << "  -h    human-readable sizes (with -l)\n"
              << "  -r    reverse sort order\n"
              << "  -t    sort by modification time\n"
              << "  -S    sort by file size\n"
              << "  -R    list subdirectories recursively\n";
}

} // anonymous namespace

void commands::ls(const Args& args, std::ostream& out, std::istream& /*in*/)
{
    auto opts = parseArgs(args);
    if (opts.help) {
        printHelp(out);
        return;
    }

    for (const auto& target : opts.paths) {
        try {
            if (opts.recursive && fs::is_directory(target)) {
                lsRecursive(target, opts, out);
            } else {
                listDirectory(target, opts, false, out);
            }
        } catch (const fs::filesystem_error& e) {
            std::cerr << "ls: cannot access '" << target.string() << "': " << e.what() << '\n';
        }
    }
}

void commands::rm(const Args& args, std::ostream& out, std::istream& /*in*/)
{
    RmOptions opts = parseRmArgs(args);
    if (opts.error) {
        return;
    }

    for (const auto& path : opts.paths) {
        std::error_code ec;
        fs::path target = fs::weakly_canonical(path, ec);

        bool exists = fs::exists(target, ec);
        if (!exists || ec) {
            std::cerr << "rm: cannot remove '" << path.string()
                      << "': No such file or directory\n";
            continue;
        }

        bool isDir = fs::is_directory(target, ec);
        if (isDir && !opts.recursive) {
            auto it = fs::directory_iterator(target, ec);
            if (!ec && it != fs::directory_iterator()) {
                std::cerr << "rm: cannot remove '" << path.string()
                          << "': Is a directory\n";
                continue;
            }
        }

        if (moveToRecycleBin(target, opts.recursive, std::cerr)) {
        }
    }
}

void commands::slots(const Args& args, std::ostream& out, std::istream& /*in*/) {
    bool any = false;
    for (int i = 0; i < 10; ++i) {
        if (g_dirSlots[i].has_value()) {
            out << "slot " << i << ": " << g_dirSlots[i].value().string() << "\n";
            any = true;
        }
    }
    if (!any) {
        out << "(no slots stored)\n";
    }
}

void commands::cp(const Args& args, std::ostream& /*out*/, std::istream& /*in*/)
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

void commands::mv(const Args& args, std::ostream& /*out*/, std::istream& /*in*/)
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

void commands::cat(const Args& args, std::ostream& out, std::istream& /*in*/)
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
    constexpr std::size_t CHUNK = 64 * 1024;
    constexpr std::size_t MAX_TOTAL = 10 * 1024 * 1024;
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
            std::cerr << "cat: '" << path << "': "
                      << (ec ? ec.message().c_str() : "not a regular file") << '\n';
            continue;
        }

        std::ifstream file(path, std::ios::binary);
        if (!file.is_open()) {
            std::cerr << "cat: cannot open '" << path << "'\n";
            continue;
        }

        std::vector<char> buf(CHUNK);
        bool aborted = false;
        while (file) {
            file.read(buf.data(), static_cast<std::streamsize>(buf.size()));
            std::streamsize got = file.gcount();
            if (got > 0) {
                out.write(buf.data(), got);
                totalWritten += static_cast<std::size_t>(got);
                if (totalWritten > MAX_TOTAL) {
                    std::cerr << "cat: output truncated at 10 MiB; use a pager\n";
                    aborted = true;
                    break;
                }
            }
            if (file.bad()) {
                std::cerr << "cat: read error on '" << path << "'\n";
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

void commands::tail(const Args& args, std::ostream& out, std::istream& /*in*/)
{
    TailOptions opts = parseTailArgs(args);
    if (opts.help) {
        printTailHelp(out);
        return;
    }
    if (opts.files.empty()) {
        std::cerr << "usage: tail [OPTION]… [FILE]…\n";
        return;
    }
    if (opts.conflict) {
        std::cerr << "tail: lines and bytes options are mutually exclusive\n";
        return;
    }

    std::size_t totalFiles = opts.files.size();
    for (const std::string& filename : opts.files) {
        if (shouldPrintHeader(totalFiles, opts)) {
            printTailHeader(filename, out);
        }
        std::ifstream test(filename, std::ios::binary);
        if (!test.is_open()) {
            std::cerr << "tail: cannot open '" << filename << "'\n";
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

void commands::grep(const Args& args, std::ostream& out, std::istream& in)
{
    if (args.size() < 2) {
        std::cerr << "usage: grep <pattern> [file]\n";
        return;
    }

    std::regex re;
    try {
        re = std::regex(args[1]);
    } catch (const std::regex_error& e) {
        std::cerr << "grep: invalid regex: " << e.what() << '\n';
        return;
    }

    std::ifstream file;
    std::istream* input = &in;
    if (args.size() >= 3) {
        file.open(args[2]);
        if (!file.is_open()) {
            std::cerr << "grep: cannot open '" << args[2] << "'\n";
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
            output += RED;
            output += match[0].str();
            output += RESET;
            searchStart = match[0].second;
        }

        if (found) {
            output.append(searchStart, line.cend());
            out << output << '\n';
        }
    }
}

void commands::cd(const Args& args, std::ostream& /*out*/, std::istream& /*in*/)
{
    if (args.size() < 2) {
        std::cerr << "usage: cd <path>\n";
        return;
    }

    std::string target = args[1];
    std::replace(target.begin(), target.end(), '/', '\\');

    if (target.size() == 2 && target[1] == ':') {
        target += '\\';
    }

    std::wstring wtarget = utf8ToWide(target);
    if (wtarget.empty() && !target.empty()) {
        std::cerr << "cd: invalid UTF-8 path: " << target << '\n';
        return;
    }

    if (!SetCurrentDirectoryW(wtarget.c_str())) {
        DWORD err = GetLastError();
        std::string msg = std::system_category().message(err);
        std::cerr << "cd: " << msg << ": " << target << '\n';
        return;
    }

    // Re-sync std::filesystem with the updated OS working directory
    // so that push/pop (which use fs::current_path()) stay consistent.
    fs::current_path(fs::current_path());
}

void commands::clear(const Args& /*args*/, std::ostream& out, std::istream& /*in*/)
{
    // Clear screen, clear scrollback, and move cursor to home (cross-platform ANSI)
    out << "\x1b[2J\x1b[3J\x1b[H" << std::flush;
}

void commands::pwd(const Args& /*args*/, std::ostream& out, std::istream& /*in*/)
{
    out << std::filesystem::current_path().string() << '\n';
}

void commands::open(const Args& args, std::ostream& /*out*/, std::istream& /*in*/)
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

void commands::echo(const Args& args, std::ostream& out, std::istream& /*in*/)
{
    for (std::size_t i = 1; i < args.size(); ++i) {
        if (i > 1) out << ' ';
        out << args[i];
    }
    out << '\n';
}

void commands::push(const Args& args, std::ostream& out, std::istream& /*in*/)
{
    if (args.size() < 2) {
        std::cerr << "usage: push <num> [directory]\n";
        return;
    }
    int idx = 0;
    if (!parseSlotIndex(args[1], idx)) {
        std::cerr << "push: '" << args[1] << "' is not a valid slot (0-9)\n";
        return;
    }
    fs::path path = (args.size() >= 3) ? fs::path(args[2]) : fs::current_path();
    g_dirSlots[idx] = path;
    out << "pushed slot " << idx << " -> " << path.string() << '\n';
}

void commands::pop(const Args& args, std::ostream& out, std::istream& /*in*/)
{
    if (args.size() < 2) {
        std::cerr << "usage: pop <num>\n";
        return;
    }
    int idx = 0;
    if (!parseSlotIndex(args[1], idx)) {
        std::cerr << "pop: '" << args[1] << "' is not a valid slot (0-9)\n";
        return;
    }
    if (!g_dirSlots[idx].has_value()) {
        std::cerr << "pop: slot " << idx << " is empty\n";
        return;
    }
    fs::path target = g_dirSlots[idx].value();
    try {
        fs::current_path(target);
        std::cout << "popped slot " << idx << " -> " << target.string() << '\n';
    } catch (const fs::filesystem_error& e) {
        std::cerr << "error: " << e.what() << '\n';
    }
}
