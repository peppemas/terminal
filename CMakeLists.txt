cmake_minimum_required(VERSION 3.20)
project(terminal)

set(CMAKE_CXX_STANDARD 20)

# --- Static library target (everything except main.cpp) for test linking ---
add_library(terminal_lib STATIC
    src/Terminal.cpp
    src/CommandParser.cpp
    src/commands/FileCommands.cpp
    src/commands/TextCommands.cpp
    src/commands/DirCommands.cpp
    src/commands/ShellCommands.cpp
    src/commands/BcCommand.cpp
    src/ForegroundJob.cpp
    src/ShellTokenizer.cpp
    src/GlobExpander.cpp
    src/CommandResolver.cpp
    src/ExternalExecutor.cpp
    src/TuiScreen.cpp
)

target_include_directories(terminal_lib PUBLIC include)

# --- Strict compiler warnings: treat all warnings as errors ---
if(MSVC)
    target_compile_options(terminal_lib PRIVATE /W4 /WX)
else()
    target_compile_options(terminal_lib PRIVATE -Wall -Wextra -Wpedantic -Werror)
endif()

if(WIN32)
    target_link_libraries(terminal_lib PUBLIC shell32 ftxui::screen ftxui::dom ftxui::component)
else()
    target_link_libraries(terminal_lib PUBLIC ftxui::screen ftxui::dom ftxui::component)
endif()

# --- Main executable ---
add_executable(terminal src/main.cpp)
target_link_libraries(terminal PRIVATE terminal_lib ftxui::screen ftxui::dom ftxui::component)

# --- GoogleTest integration ---
include(FetchContent)
FetchContent_Declare(
    googletest
    GIT_REPOSITORY https://github.com/google/googletest.git
    GIT_TAG v1.14.0
)
FetchContent_MakeAvailable(googletest)

# FTXUI's component_options.hpp contains a default member initializer that
# triggers an internal compiler error with MSVC 14.38 when included by our
# source files. Patch it to a simple default immediately after population.
FetchContent_Declare(ftxui
        GIT_REPOSITORY https://github.com/arthursonzogni/ftxui.git
        GIT_TAG        v6.1.9
        GIT_PROGRESS   TRUE
        GIT_SHALLOW    TRUE
        EXCLUDE_FROM_ALL
        PATCH_COMMAND
            "${CMAKE_COMMAND}" -E chdir "${CMAKE_BINARY_DIR}"
            python "${CMAKE_SOURCE_DIR}/patch_ftxui.py"
            "${CMAKE_BINARY_DIR}/_deps/ftxui-src/include/ftxui/component/component_options.hpp"
)
FetchContent_MakeAvailable(ftxui)
# ------------------------------------------------------------------------------

# Treat FTXUI headers as system includes so our /W4 /WX settings do not flag
# warnings inside third-party code.
target_include_directories(terminal_lib SYSTEM PUBLIC ${ftxui_SOURCE_DIR}/include)

enable_testing()

add_executable(terminal_tests
    tests/TokenizerTest.cpp
    tests/GlobExpanderTest.cpp
    tests/CommandResolverTest.cpp
    tests/ArgumentEscapingTest.cpp
    tests/TailEdgeCaseTest.cpp
)
target_link_libraries(terminal_tests PRIVATE gtest gtest_main terminal_lib shell32)
add_test(NAME unit_tests COMMAND terminal_tests)
