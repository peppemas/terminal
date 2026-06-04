cmake_minimum_required(VERSION 3.20)
project(terminal)

set(CMAKE_CXX_STANDARD 20)

add_executable(terminal
    main.cpp
    src/Terminal.cpp
    src/CommandParser.cpp
    src/Commands.cpp
    src/ForegroundJob.cpp
)

target_include_directories(terminal PUBLIC include)

if(WIN32)
    target_link_libraries(terminal PRIVATE shell32)
endif()
