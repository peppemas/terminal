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
    src/ForegroundJob.cpp
    src/ShellTokenizer.cpp
    src/GlobExpander.cpp
    src/CommandResolver.cpp
    src/ExternalExecutor.cpp
)

target_include_directories(terminal_lib PUBLIC include)

# --- Strict compiler warnings: treat all warnings as errors ---
if(MSVC)
    target_compile_options(terminal_lib PRIVATE /W4 /WX)
else()
    target_compile_options(terminal_lib PRIVATE -Wall -Wextra -Wpedantic -Werror)
endif()

if(WIN32)
    target_link_libraries(terminal_lib PUBLIC shell32)
endif()

# --- Main executable ---
add_executable(terminal src/main.cpp)
target_link_libraries(terminal PRIVATE terminal_lib)

# --- GoogleTest integration ---
include(FetchContent)
FetchContent_Declare(
    googletest
    GIT_REPOSITORY https://github.com/google/googletest.git
    GIT_TAG v1.14.0
)
FetchContent_MakeAvailable(googletest)

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
