#include "Commands.hpp"

#include <iostream>
#include <filesystem>
#include <vector>
#include <string>

#define NOMINMAX
#include <windows.h>
#include <shellapi.h>

namespace fs = std::filesystem;

namespace {

struct RmOptions {
    bool recursive = false;
    bool permanent = false;
    bool help      = false;
    bool error     = false;
    std::vector<fs::path> paths;
};

RmOptions parseRmArgs(const commands::Args& args, std::ostream& err) {
    RmOptions opts;
    bool operandsCollected = false;
    for (std::size_t i = 1; i < args.size(); ++i) {
        const std::string& token = args[i];
        if (!operandsCollected && token == "-r") {
            opts.recursive = true;
        } else if (!operandsCollected && token == "--permanent") {
            opts.permanent = true;
        } else if (!operandsCollected && token.size() >= 1 && token[0] == '-') {
            err << "rm: invalid option -- '" << token << "'\n";
            opts.error = true;
        } else {
            opts.paths.push_back(token);
            operandsCollected = true;
        }
    }
    if (opts.paths.empty() && !opts.error) {
        err << "usage: rm [-r] [--permanent] <path>...\n";
        opts.error = true;
    }
    return opts;
}

bool isUNCPath(const fs::path& p) {
    std::string pathStr = p.string();
    return (pathStr.size() >= 2 &&
            ((pathStr[0] == '\\' && pathStr[1] == '\\') ||
             (pathStr[0] == '/' && pathStr[1] == '/')));
}

bool permanentDelete(const fs::path& target, bool recursive, std::ostream& err) {
    std::error_code ec;
    if (recursive || fs::is_directory(target, ec)) {
        (void)fs::remove_all(target, ec); // Result intentionally unused — we check ec
        if (ec) {
            err << "rm: failed to permanently delete '" << target.string()
                << "': " << ec.message() << "\n";
            return false;
        }
    } else {
        if (!fs::remove(target, ec) || ec) {
            err << "rm: failed to permanently delete '" << target.string()
                << "': " << ec.message() << "\n";
            return false;
        }
    }
    return true;
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
        // Fallback: attempt permanent deletion
        err << "rm: Recycle Bin operation failed for '" << p.string()
            << "', attempting permanent deletion...\n";
        return permanentDelete(p, recursive, err);
    }
    return true;
}

} // anonymous namespace

void commands::rm(const Args& args, std::ostream& /*out*/, std::istream& /*in*/, std::ostream& err)
{
    RmOptions opts = parseRmArgs(args, err);
    if (opts.error) {
        return;
    }

    for (const auto& path : opts.paths) {
        std::error_code ec;
        fs::path target = fs::weakly_canonical(path, ec);

        bool exists = fs::exists(target, ec);
        if (!exists || ec) {
            err << "rm: cannot remove '" << path.string()
                      << "': No such file or directory\n";
            continue;
        }

        bool isDir = fs::is_directory(target, ec);
        if (isDir && !opts.recursive) {
            auto it = fs::directory_iterator(target, ec);
            if (!ec && it != fs::directory_iterator()) {
                err << "rm: cannot remove '" << path.string()
                          << "': Is a directory\n";
                continue;
            }
        }

        // --permanent flag: bypass Recycle Bin entirely
        if (opts.permanent) {
            permanentDelete(target, opts.recursive, err);
            continue;
        }

        // UNC paths: SHFileOperationW does not support network paths reliably
        if (isUNCPath(target)) {
            err << "rm: warning: '" << target.string()
                      << "' is a network path; file will be permanently deleted (not sent to Recycle Bin)\n";
            permanentDelete(target, opts.recursive, err);
            continue;
        }

        // Local paths: attempt Recycle Bin with fallback
        moveToRecycleBin(target, opts.recursive, err);
    }
}

void commands::cp(const Args& args, std::ostream& /*out*/, std::istream& /*in*/, std::ostream& err)
{
    if (args.size() < 3) {
        err << "usage: cp <source> <destination>\n";
        return;
    }

    try {
        fs::copy(args[1], args[2], fs::copy_options::overwrite_existing);
    } catch (const fs::filesystem_error& e) {
        err << "error: " << e.what() << '\n';
    }
}

void commands::mv(const Args& args, std::ostream& /*out*/, std::istream& /*in*/, std::ostream& err)
{
    if (args.size() < 3) {
        err << "usage: mv <source> <destination>\n";
        return;
    }

    try {
        fs::rename(args[1], args[2]);
    } catch (const fs::filesystem_error& e) {
        err << "error: " << e.what() << '\n';
    }
}
