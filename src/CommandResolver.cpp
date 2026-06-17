#include "CommandResolver.hpp"

#include <windows.h>
#include <algorithm>
#include <cassert>
#include <iostream>
#include <string_view>

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
// Internal helper: check if an executable path ends with .cmd or .bat
// (case-insensitive).
// ---------------------------------------------------------------------------
static bool isBatchFile(const std::wstring& exePath)
{
    if (exePath.size() < 4) return false;
    std::wstring ext = exePath.substr(exePath.size() - 4);
    // Lowercase the extension for comparison
    for (auto& ch : ext) {
        if (ch >= L'A' && ch <= L'Z') ch += 32;
    }
    return ext == L".cmd" || ext == L".bat";
}

// ---------------------------------------------------------------------------
// Internal helper: escape a single argument using the Windows CreateProcess
// escaping algorithm.
//
// Rules (https://learn.microsoft.com/en-us/cpp/c-runtime-library/
//        parsing-c-command-line-arguments):
//  - If the argument is empty or contains spaces, tabs, or double-quotes,
//    it must be wrapped in double-quotes.
//  - Inside quotes, backslashes are literal UNLESS they precede a `"`:
//    - N backslashes followed by `"`: emit 2N+1 backslashes then the `"`.
//    - N trailing backslashes (precede closing quote): emit 2N backslashes.
//  - For .cmd/.bat targets, additionally escape cmd.exe metacharacters
//    (&|^<>()) with a `^` prefix.
// ---------------------------------------------------------------------------
static std::wstring escapeArgument(const std::wstring& arg, bool isBatch)
{
    using namespace std::literals;

    std::wstring result;
    bool needQuotes = arg.empty() || arg.find_first_of(L" \t\"") != std::wstring::npos;

    if (needQuotes) result += L'"';

    for (size_t i = 0; i < arg.size(); ++i) {
        size_t backslashes = 0;
        while (i < arg.size() && arg[i] == L'\\') {
            ++backslashes;
            ++i;
        }

        if (i == arg.size()) {
            // Trailing backslashes: double them only if they precede the closing quote
            result.append(needQuotes ? backslashes * 2 : backslashes, L'\\');
        } else if (arg[i] == L'"') {
            // Backslashes before quote: double + escape the quote
            result.append(backslashes * 2 + 1, L'\\');
            result += L'"';
        } else {
            result.append(backslashes, L'\\');
            result += arg[i];
        }
    }

    if (needQuotes) result += L'"';

    if (isBatch) {
        // Escape cmd.exe metacharacters with ^
        std::wstring escaped;
        escaped.reserve(result.size() + 8);
        for (wchar_t c : result) {
            if (L"&|^<>()"sv.find(c) != std::wstring_view::npos) {
                escaped += L'^';
            }
            escaped += c;
        }
        return escaped;
    }

    return result;
}

// ---------------------------------------------------------------------------
// Private: build a properly escaped wide command line using Windows
// CreateProcess escaping rules.
// ---------------------------------------------------------------------------
std::wstring CommandResolver::buildCommandLine(const std::wstring& exePath,
                                               const std::vector<std::string>& args)
{
    const bool isBatch = isBatchFile(exePath);

    std::wstring cmdLine;
    cmdLine.reserve(exePath.size() + args.size() * 16);

    // argv[0] is the executable path — always quote it, but no special
    // escaping needed (paths don't embed double-quotes on Windows).
    cmdLine += L'"';
    cmdLine += exePath;
    cmdLine += L'"';

    // Escape each subsequent argument properly.
    for (std::size_t i = 1; i < args.size(); ++i) {
        cmdLine += L' ';
        cmdLine += escapeArgument(widen(args[i]), isBatch);
    }

    return cmdLine;
}

// ---------------------------------------------------------------------------
// Internal helper: check if CWD execution is allowed via the
// TERMINAL_ALLOW_CWD_EXEC environment variable.
// Returns true (allow) by default for backward compatibility.
// Returns false only when the env var is explicitly "0" or "false".
// ---------------------------------------------------------------------------
static bool isCwdExecutionAllowed()
{
    wchar_t buf[64]{};
    DWORD n = ::GetEnvironmentVariableW(L"TERMINAL_ALLOW_CWD_EXEC", buf, 64);
    if (n == 0 || n >= 64) {
        return true; // not set or too long — default to allowed
    }

    std::wstring val(buf, n);
    // Lowercase for comparison
    for (auto& ch : val) {
        if (ch >= L'A' && ch <= L'Z') ch += 32;
    }
    return val != L"0" && val != L"false";
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
    // Search order: system PATH directories first, CWD last.
    // This prevents CWD shadowing attacks where a malicious binary in the
    // current directory overrides a legitimate system command.
    //
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

    // 1) Search system PATH directories first.
    for (const auto& dir : getPathDirs()) {
        if (auto r = tryDir(dir)) return r;
    }

    // 2) Search CWD last — with security checks for unqualified commands.
    if (auto r = tryDir(curDir)) {
        // The binary was found in CWD but NOT on PATH. Emit a warning.
        std::cerr << "warning: '" << name
                  << "' resolved from current directory ("
                  << r->executable.string()
                  << "). Use './" << name
                  << "' to suppress this warning.\n";

        // Check if CWD execution is allowed by the environment variable.
        if (!isCwdExecutionAllowed()) {
            std::cerr << "error: execution of unqualified CWD binary refused "
                         "(TERMINAL_ALLOW_CWD_EXEC=0). Use './" << name
                      << "' to explicitly run from CWD.\n";
            return std::nullopt;
        }

        return r;
    }

    return std::nullopt;
}
