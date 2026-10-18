# The SX52 core without any front end: no SDL, wxWidgets or curses.
if(CMAKE_SOURCE_DIR STREQUAL CMAKE_CURRENT_SOURCE_DIR)
    # built on its own, not as part of another project
    cmake_minimum_required(VERSION 3.10)
    project(sxcpu CXX)
    set(CMAKE_CXX_STANDARD 11)
    set(CMAKE_CXX_STANDARD_REQUIRED ON)
    set(SXCPU_STANDALONE ON)
    enable_testing()
endif()

add_library(sxcpu STATIC cpu.cpp cpu.h cpu_ports.h disasm.cpp disasm.h asm.cpp asm.h)
target_include_directories(sxcpu PUBLIC ${CMAKE_CURRENT_SOURCE_DIR})

# The tests of the CPU, the assembler and the disassembler. They are on when sxcpu is
# built on its own; a project that uses sxcpu sets SXCPU_BUILD_TESTS to switch them on.
if(SXCPU_STANDALONE)
    option(SXCPU_BUILD_TESTS "Build the tests of sxcpu" ON)
else()
    option(SXCPU_BUILD_TESTS "Build the tests of sxcpu" OFF)
endif()

if(SXCPU_BUILD_TESTS)
    if(NOT TARGET gtest)
        include(FetchContent)
        FetchContent_Declare(
            googletest
            GIT_REPOSITORY https://github.com/google/googletest.git
            GIT_TAG release-1.12.1
        )
        FetchContent_MakeAvailable(googletest)
    endif()

    file(GLOB SXCPU_TEST_SOURCES ${CMAKE_CURRENT_SOURCE_DIR}/tests/*.cpp)
    add_executable(sxcpu_tests ${SXCPU_TEST_SOURCES})
    target_link_libraries(sxcpu_tests sxcpu gtest gtest_main)

    include(GoogleTest)
    gtest_discover_tests(sxcpu_tests)
endif()
