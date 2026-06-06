#pragma once

#ifndef TERMINAL_COMMANDRESOLVER_HPP
#define TERMINAL_COMMANDRESOLVER_HPP

#include <filesystem>
#include <optional>
#include <string>
#include <vector>

struct CommandResolver {
    struct ResolutionResult {
        std::filesystem::path executable;   // Absolute path to the resolved file
        std::wstring          commandLine;  // Full command line suitable for CreateProcessW
    };

    // Attempt to resolve an external command name to an executable file.
    // name: the command token (first token from the user).
    // args: the full argv vector (including name at index 0).
    // Returns std::nullopt if no executable is found after PATH/PATHEXT search.
    static std::optional<ResolutionResult> resolve(const std::string& name,
                                                   const std::vector<std::string>& args);

private:
    // Read PATH environment variable and split into directory entries.
    static std::vector<std::wstring> getPathDirs();

    // Read PATHEXT environment variable and split into extension entries.
    static std::vector<std::wstring> getPathext();

    // Check whether a command name appears to be a qualified file path
    // (i.e. contains a backslash, forward slash, or drive colon).
    static bool isQualifiedPath(const std::string& name);

    // Build a quoted wide-character command line from an executable path
    // and the original argument vector (args[0] is the command name).
    static std::wstring buildCommandLine(const std::wstring& exePath,
                                         const std::vector<std::string>& args);
};

#endif // TERMINAL_COMMANDRESOLVER_HPP
