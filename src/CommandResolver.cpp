#include "CommandResolver.hpp"

#include <windows.h>
#include <algorithm>
#include <cassert>

// TEMPORARY DEBUG — remove after diagnostics
#include <iostream>
#define DBG_RESOLVER 1
#ifdef DBG_RESOLVER
#define DR_LOG(msg) do { std::wcout << L"[DR] " << msg << std::endl; } while (0)
#define DR_LOG_NATIVE(msg) do { std::cout << "[DR] " << msg << std::endl; } while (0)
#else
#define DR_LOG(msg) do { } while (0)
#define DR_LOG_NATIVE(msg) do { } while (0)
#endif

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

    // --- Unqualified name: explicit PATH+PATHEXT loop (INSTRUMENTED) ----
    const std::wstring wname   = widen(name);
    const auto         pathext = getPathext();

    DR_LOG(L"resolve() called with name='" << name.c_str() << L"' wname='" << wname << L"'");
    DR_LOG(L"PATHEXT entries: " << pathext.size());
    for (std::size_t i = 0; i < pathext.size(); ++i) {
        DR_LOG(L"  pathext[" << i << L"] = '" << pathext[i] << L"' (len=" << pathext[i].size() << L")");
    }

    std::wstring curDir(32768, L'\0');
    {
        DWORD n = ::GetCurrentDirectoryW(32768, curDir.data());
        if (n > 0 && n < 32768) curDir.resize(n);
        else                     curDir.clear();
    }
    DR_LOG(L"current dir: '" << curDir << L"' (len=" << curDir.size() << L")");

    auto toAbsolute = [](const std::wstring& p) -> std::wstring {
        std::wstring buf(32768, L'\0');
        DWORD n = ::GetFullPathNameW(p.c_str(), 32768, buf.data(), nullptr);
        if (n > 0 && n < 32768) { buf.resize(n); return buf; }
        return p;
    };

    auto inspectDir = [](const std::wstring& d) {
        DR_LOG(L"  PATH entry: '" << d << L"' (len=" << d.size() << L")");
        if (!d.empty()) {
            DR_LOG(L"    first char: 0x" << std::hex << (int)d.front() << std::dec
                     << L" last char: 0x" << std::hex << (int)d.back() << std::dec);
            if (d.front() == L'"' || d.back() == L'"')
                DR_LOG(L"    *** HAS SURROUNDING QUOTES ***");
            if (d.back() == L' ' || d.back() == L'\t' || d.back() == L'\\')
                DR_LOG(L"    *** TRAILING SPACE/TAB/BACKSLASH ***");
        }
    };

    auto tryDir = [&](const std::wstring& dir, const std::wstring& dirLabel) -> std::optional<ResolutionResult> {
        if (dir.empty()) {
            DR_LOG(L"tryDir(" << dirLabel << L"): empty, skip");
            return std::nullopt;
        }
        const std::wstring base = dir + L'\\' + wname;
        DR_LOG(L"tryDir(" << dirLabel << L"): base = '" << base << L"'");

        // a) PATHEXT extensions
        for (const auto& ext : pathext) {
            std::wstring candidate = base + ext;
            DWORD a = ::GetFileAttributesW(candidate.c_str());
            bool exists = (a != INVALID_FILE_ATTRIBUTES);
            bool isDir  = exists && (a & FILE_ATTRIBUTE_DIRECTORY);
            DR_LOG(L"  candidate='" << candidate << L"' GetFileAttributesW=" << a
                     << L" exists=" << (exists ? 1 : 0) << L" isDir=" << (isDir ? 1 : 0));
            if (exists && !isDir) {
                ResolutionResult r;
                r.executable  = std::filesystem::path(toAbsolute(candidate));
                r.commandLine = buildCommandLine(r.executable.wstring(), args);
                DR_LOG(L"  *** RESOLVED via PATHEXT ***");
                return r;
            }
        }

        // b) Bare name
        {
            DWORD a = ::GetFileAttributesW(base.c_str());
            bool exists = (a != INVALID_FILE_ATTRIBUTES);
            bool isDir  = exists && (a & FILE_ATTRIBUTE_DIRECTORY);
            DR_LOG(L"  bare='" << base << L"' GetFileAttributesW=" << a
                     << L" exists=" << (exists ? 1 : 0) << L" isDir=" << (isDir ? 1 : 0));
            if (exists && !isDir) {
                ResolutionResult r;
                r.executable  = std::filesystem::path(toAbsolute(base));
                r.commandLine = buildCommandLine(r.executable.wstring(), args);
                DR_LOG(L"  *** RESOLVED via BARE ***");
                return r;
            }
        }

        return std::nullopt;
    };

    DR_LOG(L"--- searching current dir ---");
    if (auto r = tryDir(curDir, L"cwd")) return r;

    auto dirs = getPathDirs();
    DR_LOG(L"--- searching PATH: " << dirs.size() << L" entries ---");
    for (std::size_t i = 0; i < dirs.size(); ++i) {
        inspectDir(dirs[i]);
        if (auto r = tryDir(dirs[i], std::to_wstring(i))) return r;
    }

    DR_LOG(L"*** NOT FOUND ***");
    return std::nullopt;
}
