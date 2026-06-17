#include "Commands.hpp"
#include "Config.hpp"

#include <iostream>
#include <filesystem>
#include <cstdint>
#include <utility>
#include <array>
#include <optional>
#include <chrono>
#include <iomanip>
#include <algorithm>
#include <cstdio>
#include <ctime>
#include <string>
#include <vector>

#define NOMINMAX
#include <windows.h>

namespace fs = std::filesystem;

namespace {

static std::array<std::optional<std::filesystem::path>, config::DIR_SLOT_COUNT> g_dirSlots;

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

struct MkdirOptions {
    bool parents = false;
    bool verbose = false;
    bool help    = false;
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

LsOptions parseArgs(const commands::Args& args, std::ostream& err) {
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
                        err << "ls: invalid option -- '" << c << "'\n";
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

MkdirOptions parseMkdirArgs(const commands::Args& args, std::ostream& err) {
    MkdirOptions opts;
    for (std::size_t i = 1; i < args.size(); ++i) {
        const std::string& token = args[i];
        if (token == "--help") {
            opts.help = true;
            return opts;
        }
        if (token == "-p" || token == "--parents") {
            opts.parents = true;
            continue;
        }
        if (token == "-v" || token == "--verbose") {
            opts.verbose = true;
            continue;
        }
        if (!token.empty() && token[0] == '-') {
            err << "mkdir: invalid option -- '" << token << "'\n";
            continue;
        }
        opts.paths.push_back(token);
    }
    return opts;
}

void printMkdirHelp(std::ostream& out) {
    out << "usage: mkdir [OPTION]... DIRECTORY...\n"
        << "Create the DIRECTORY(ies), if they do not already exist.\n\n"
        << "  -p, --parents   make parent directories as needed\n"
        << "  -v, --verbose   print a message for each created directory\n"
        << "      --help      display this help and exit\n";
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

void collectEntries(const fs::path& dir, const LsOptions& opts, std::vector<DirEntry>& out, std::ostream& err) {
    std::error_code ec;
    fs::directory_iterator it(dir, ec);
    if (ec) {
        err << "ls: cannot open directory '" << dir.string() << "': " << ec.message() << '\n';
        return;
    }

    fs::directory_iterator end;
    while (it != end) {
        const auto& entry = *it;

        std::error_code pathEc;
        fs::path entryPath = entry.path();
        if (!opts.all && isHidden(entryPath)) {
            it.increment(ec);
            if (ec) {
                err << "ls: cannot advance iterator in '" << dir.string() << "': " << ec.message() << '\n';
                return;
            }
            continue;
        }

        std::error_code statusEc;
        fs::file_status st = entry.status(statusEc);

        std::error_code symlinkEc;
        fs::file_status symst = entry.symlink_status(symlinkEc);

        if (statusEc && symlinkEc) {
            err << "ls: cannot stat '" << entryPath.string() << "': " << statusEc.message() << '\n';
            it.increment(ec);
            if (ec) {
                err << "ls: cannot advance iterator in '" << dir.string() << "': " << ec.message() << '\n';
                return;
            }
            continue;
        }

        DirEntry de;
        de.path = entryPath;
        de.status = st;
        de.is_symlink = !symlinkEc && fs::is_symlink(symst);
        de.is_hidden = isHidden(entryPath);

        if (!statusEc && fs::is_regular_file(st)) {
            std::error_code sizeEc;
            de.size = entry.file_size(sizeEc);
            if (sizeEc) {
                de.size = 0;
            }
        } else {
            de.size = 0;
        }

        std::error_code mtimeEc;
        de.mtime = entry.last_write_time(mtimeEc);
        if (mtimeEc) {
            err << "ls: cannot stat '" << entryPath.string() << "' (mtime): " << mtimeEc.message() << '\n';
            de.mtime = fs::file_time_type::clock::now();
        }

        std::error_code nlinkEc;
        de.nlink = entry.hard_link_count(nlinkEc);
        if (nlinkEc) {
            de.nlink = 1;
        }

        de.owner = "";
        out.push_back(std::move(de));

        it.increment(ec);
        if (ec) {
            err << "ls: cannot advance iterator in '" << dir.string() << "': " << ec.message() << '\n';
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
            std::tm tmBuf{};
            localtime_s(&tmBuf, &t);  // MSVC-safe alternative to localtime (C4996)
            std::tm* tm = &tmBuf;

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

std::vector<DirEntry> listDirectory(const fs::path& dir, const LsOptions& opts, bool printHeader, std::ostream& out, std::ostream& err) {
    if (printHeader) {
        out << dir.string() << ":\n";
    }

    std::vector<DirEntry> entries;

    if (!fs::is_directory(dir)) {
        std::error_code statusEc;
        fs::file_status st = fs::status(dir, statusEc);
        if (statusEc) {
            err << "ls: cannot stat '" << dir.string() << "': " << statusEc.message() << '\n';
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
            err << "ls: cannot stat '" << dir.string() << "' (mtime): " << mtimeEc.message() << '\n';
            de.mtime = fs::file_time_type::clock::now();
        }

        de.nlink = 1;
        de.owner = "";
        entries.push_back(std::move(de));
    } else {
        collectEntries(dir, opts, entries, err);
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

void lsRecursive(const fs::path& dir, const LsOptions& opts, std::ostream& out, std::ostream& err) {
    auto entries = listDirectory(dir, opts, false, out, err);
    for (const auto& e : entries) {
        if (fs::is_directory(e.status)) {
            try {
                lsRecursive(e.path, opts, out, err);
            } catch (const fs::filesystem_error& ex) {
                err << "ls: cannot access '" << e.path.string() << "': " << ex.what() << '\n';
            }
        }
    }
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

void commands::ls(const Args& args, std::ostream& out, std::istream& /*in*/, std::ostream& err)
{
    auto opts = parseArgs(args, err);
    if (opts.help) {
        printHelp(out);
        return;
    }

    for (const auto& target : opts.paths) {
        try {
            if (opts.recursive && fs::is_directory(target)) {
                lsRecursive(target, opts, out, err);
            } else {
                listDirectory(target, opts, false, out, err);
            }
        } catch (const fs::filesystem_error& e) {
            err << "ls: cannot access '" << target.string() << "': " << e.what() << '\n';
        }
    }
}

void commands::cd(const Args& args, std::ostream& /*out*/, std::istream& /*in*/, std::ostream& err)
{
    if (args.size() < 2) {
        err << "usage: cd <path>\n";
        return;
    }

    std::string target = args[1];
    std::replace(target.begin(), target.end(), '/', '\\');

    if (target.size() == 2 && target[1] == ':') {
        target += '\\';
    }

    std::wstring wtarget = utf8ToWide(target);
    if (wtarget.empty() && !target.empty()) {
        err << "cd: invalid UTF-8 path: " << target << '\n';
        return;
    }

    if (!SetCurrentDirectoryW(wtarget.c_str())) {
        DWORD errCode = GetLastError();
        std::string msg = std::system_category().message(errCode);
        err << "cd: " << msg << ": " << target << '\n';
        return;
    }

    // Re-sync std::filesystem with the updated OS working directory
    // so that push/pop (which use fs::current_path()) stay consistent.
    fs::current_path(fs::current_path());
}

void commands::pwd(const Args& /*args*/, std::ostream& out, std::istream& /*in*/, std::ostream& /*err*/)
{
    out << std::filesystem::current_path().string() << '\n';
}

void commands::push(const Args& args, std::ostream& out, std::istream& /*in*/, std::ostream& err)
{
    if (args.size() < 2) {
        err << "usage: push <num> [directory]\n";
        return;
    }
    int idx = 0;
    if (!parseSlotIndex(args[1], idx)) {
        err << "push: '" << args[1] << "' is not a valid slot (0-9)\n";
        return;
    }
    fs::path path = (args.size() >= 3) ? fs::path(args[2]) : fs::current_path();
    g_dirSlots[idx] = path;
    out << "pushed slot " << idx << " -> " << path.string() << '\n';
}

void commands::pop(const Args& args, std::ostream& out, std::istream& /*in*/, std::ostream& err)
{
    if (args.size() < 2) {
        err << "usage: pop <num>\n";
        return;
    }
    int idx = 0;
    if (!parseSlotIndex(args[1], idx)) {
        err << "pop: '" << args[1] << "' is not a valid slot (0-9)\n";
        return;
    }
    if (!g_dirSlots[idx].has_value()) {
        err << "pop: slot " << idx << " is empty\n";
        return;
    }
    fs::path target = g_dirSlots[idx].value();
    try {
        fs::current_path(target);
        out << "popped slot " << idx << " -> " << target.string() << '\n';
    } catch (const fs::filesystem_error& e) {
        err << "error: " << e.what() << '\n';
    }
}

void commands::slots(const Args& /*args*/, std::ostream& out, std::istream& /*in*/, std::ostream& /*err*/) {
    bool any = false;
    for (int i = 0; i < config::DIR_SLOT_COUNT; ++i) {
        if (g_dirSlots[i].has_value()) {
            out << "slot " << i << ": " << g_dirSlots[i].value().string() << "\n";
            any = true;
        }
    }
    if (!any) {
        out << "(no slots stored)\n";
    }
}

void commands::mkdir(const Args& args, std::ostream& out, std::istream& /*in*/, std::ostream& err)
{
    MkdirOptions opts = parseMkdirArgs(args, err);
    if (opts.help) {
        printMkdirHelp(out);
        return;
    }
    if (opts.paths.empty()) {
        err << "mkdir: missing operand\n";
        return;
    }

    for (const auto& p : opts.paths) {
        std::error_code ec;
        if (opts.parents) {
            bool created = fs::create_directories(p, ec);
            if (ec && ec != std::errc::file_exists) {
                err << "mkdir: cannot create directory '" << p.string()
                          << "': " << ec.message() << "\n";
                continue;
            }
            if (opts.verbose && created) {
                out << p.string() << "\n";
            }
        } else {
            bool created = fs::create_directory(p, ec);
            if (ec) {
                if (ec == std::errc::file_exists) {
                    err << "mkdir: cannot create directory '" << p.string()
                              << "': File exists\n";
                } else {
                    err << "mkdir: cannot create directory '" << p.string()
                              << "': " << ec.message() << "\n";
                }
                continue;
            }
            if (opts.verbose && created) {
                out << p.string() << "\n";
            }
        }
    }
}
