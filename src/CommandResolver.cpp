#include "CommandResolver.hpp"

#include <windows.h>
#include <algorithm>
#include <cassert>

// ---------------------------------------------------------------------------
// Internal helper: read a semicolon-delimited environment variable via the
// wide-character Windows API.
// ---------------------------------------------------------------------------
static std::vector<std::wstring> getEnvVarList(const wchar_t* varName)
{
    std::vector<std::wstring> result;

    DWORD len = GetEnvironmentVariableW(varName, nullptr, 0);
    if (len == 0) {
        return result;
    }

    std::wstring buffer(len, L'\0');
    DWORD actual = GetEnvironmentVariableW(varName, buffer.data(), len);
    if (actual == 0 || actual >= len) {
        return result;
    }
    buffer.resize(actual);

    std::size_t start = 0;
    while (start <= buffer.size()) {
        std::size_t pos = buffer.find(L';', start);
        std::wstring entry;
        if (pos == std::wstring::npos) {
            entry = buffer.substr(start);
            start = buffer.size() + 1;
        } else {
            entry = buffer.substr(start, pos - start);
            start = pos + 1;
        }

        if (!entry.empty()) {
            result.push_back(std::move(entry));
        }
    }

    return result;
}

// ---------------------------------------------------------------------------
// Convert a UTF-8 std::string to a wide string (UTF-16).
// ---------------------------------------------------------------------------
static std::wstring widen(const std::string& utf8)
{
    if (utf8.empty()) {
        return {};
    }

    int len = MultiByteToWideChar(CP_UTF8, 0, utf8.data(),
                                  static_cast<int>(utf8.size()),
                                  nullptr, 0);
    if (len <= 0) {
        return {};
    }

    std::wstring result(len, L'\0');
    MultiByteToWideChar(CP_UTF8, 0, utf8.data(),
                        static_cast<int>(utf8.size()),
                        result.data(), len);
    return result;
}

// ---------------------------------------------------------------------------
// Public: qualified-path predicate
// ---------------------------------------------------------------------------
bool CommandResolver::isQualifiedPath(const std::string& name)
{
    return name.find('\\') != std::string::npos ||
           name.find('/')  != std::string::npos ||
           name.find(':')  != std::string::npos;
}

// ---------------------------------------------------------------------------
// Public: PATH directory list
// ---------------------------------------------------------------------------
std::vector<std::wstring> CommandResolver::getPathDirs()
{
    return getEnvVarList(L"PATH");
}

// ---------------------------------------------------------------------------
// Public: PATHEXT extension list.
//
// Returns the user's PATHEXT if it can be read and is non-empty; otherwise
// returns a hardcoded list matching the Windows default.  This guarantees
// that commands like `npm` (which resolves to `npm.cmd` via PATHEXT) always
// work, even when the terminal is launched from an environment that does
// not inherit PATHEXT (e.g. some IDEs).
// ---------------------------------------------------------------------------
std::vector<std::wstring> CommandResolver::getPathext()
{
    std::vector<std::wstring> result = getEnvVarList(L"PATHEXT");

    // Normalize extensions: trim whitespace and ensure leading dot.
    for (auto& ext : result) {
        const auto first = ext.find_first_not_of(L" \t\r\n");
        const auto last  = ext.find_last_not_of(L" \t\r\n");
        if (first == std::wstring::npos) {
            ext.clear();
            continue;
        }
        ext = ext.substr(first, last - first + 1);
        if (!ext.empty() && ext[0] != L'.') {
            ext = L'.' + ext;
        }
    }

    // Remove any entries that became empty after normalization.
    result.erase(
        std::remove_if(result.begin(), result.end(),
                       [](const std::wstring& s) { return s.empty(); }),
        result.end());

    // Fallback to the Windows default PATHEXT if the env var was missing
    // or empty.  This list matches the system default on every modern
    // Windows installation.
    if (result.empty()) {
        result = {
            L".COM", L".EXE", L".BAT", L".CMD",
            L".VBS", L".VBE", L".JS",  L".JSE",
            L".WSF", L".WSH", L".MSC"
        };
    }

    return result;
}

// ---------------------------------------------------------------------------
// Private: build a properly quoted wide command line.
// ---------------------------------------------------------------------------
std::wstring CommandResolver::buildCommandLine(const std::wstring& exePath,
                                               const std::vector<std::string>& args)
{
    std::wstring cmdLine;
    cmdLine.reserve(exePath.size() + args.size() * 16);

    cmdLine += L'"';
    cmdLine += exePath;
    cmdLine += L'"';

    for (std::size_t i = 1; i < args.size(); ++i) {
        cmdLine += L' ';
        const std::wstring warg = widen(args[i]);

        bool needQuotes = warg.empty() ||
                          warg.find(L' ')  != std::wstring::npos ||
                          warg.find(L'\t') != std::wstring::npos;

        if (needQuotes) {
            cmdLine += L'"';
            cmdLine += warg;
            cmdLine += L'"';
        } else {
            cmdLine += warg;
        }
    }

    return cmdLine;
}

// ---------------------------------------------------------------------------
// Core: resolve a command name to an executable file.
// ---------------------------------------------------------------------------
std::optional<CommandResolver::ResolutionResult>
CommandResolver::resolve(const std::string& name, const std::vector<std::string>& args)
{
    assert(!args.empty() && args[0] == name);

    // --- Qualified path --------------------------------------------------
    if (isQualifiedPath(name)) {
        std::error_code ec;
        if (std::filesystem::exists(name, ec) && !ec) {
            ResolutionResult result;
            result.executable  = std::filesystem::absolute(name, ec);
            if (ec) {
                result.executable = name;
            }
            result.commandLine = buildCommandLine(result.executable.wstring(), args);
            return result;
        }
        return std::nullopt;
    }

    // --- Unqualified name: explicit PATH+PATHEXT loop --------------------
    // We use our own getPathext() (which has a hardcoded Windows default
    // fallback including .CMD) so the search works even when the
    // process environment has a stripped PATHEXT (e.g. when the
    // terminal is launched from an IDE).
    //
    // Search order: current directory first, then each PATH directory.
    // Within each directory:
    //   a) Try the name + each PATHEXT extension (e.g. npm.CMD, npm.EXE)
    //      so that npm.cmd is preferred over a bare Unix-style shim.
    //   b) Then try the bare name as a fallback for native executables
    //      that have no file extension.

    const std::wstring wname   = widen(name);
    const auto         pathext = getPathext();

    std::wstring curDir(32768, L'\0');
    {
        DWORD n = ::GetCurrentDirectoryW(32768, curDir.data());
        if (n > 0 && n < 32768) curDir.resize(n);
        else                     curDir.clear();
    }

    auto isRegularFile = [](const std::wstring& p) -> bool {
        DWORD a = ::GetFileAttributesW(p.c_str());
        return a != INVALID_FILE_ATTRIBUTES && !(a & FILE_ATTRIBUTE_DIRECTORY);
    };

    auto toAbsolute = [](const std::wstring& p) -> std::wstring {
        std::wstring buf(32768, L'\0');
        DWORD n = ::GetFullPathNameW(p.c_str(), 32768, buf.data(), nullptr);
        if (n > 0 && n < 32768) { buf.resize(n); return buf; }
        return p;
    };

    auto tryDir = [&](const std::wstring& dir) -> std::optional<ResolutionResult> {
        if (dir.empty()) return std::nullopt;
        const std::wstring base = dir + L'\\' + wname;

        // a) PATHEXT extensions -- cmd.exe-compatible, finds npm.cmd etc.
        for (const auto& ext : pathext) {
            std::wstring candidate = base + ext;
            if (isRegularFile(candidate)) {
                ResolutionResult r;
                r.executable  = std::filesystem::path(toAbsolute(candidate));
                r.commandLine = buildCommandLine(r.executable.wstring(), args);
                return r;
            }
        }

        // b) Bare name -- fallback for native Windows binaries with no extension.
        if (isRegularFile(base)) {
            ResolutionResult r;
            r.executable  = std::filesystem::path(toAbsolute(base));
            r.commandLine = buildCommandLine(r.executable.wstring(), args);
            return r;
        }

        return std::nullopt;
    };

    if (auto r = tryDir(curDir)) return r;
    for (const auto& dir : getPathDirs()) {
        if (auto r = tryDir(dir)) return r;
    }

    return std::nullopt;
}
