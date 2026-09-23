cmake_minimum_required(VERSION 3.20)
project(lob CXX)

set(CMAKE_CXX_STANDARD 23)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_EXPORT_COMPILE_COMMANDS ON)

include(CheckCXXCompilerFlag)

if(NOT CMAKE_BUILD_TYPE AND NOT CMAKE_CONFIGURATION_TYPES)
    set(CMAKE_BUILD_TYPE Release CACHE STRING "build type" FORCE)
endif()

check_cxx_compiler_flag(-march=native OB_HAS_MARCH_NATIVE)
if(OB_HAS_MARCH_NATIVE)
    set(OB_ARCH_FLAG -march=native)
else()
    check_cxx_compiler_flag(-mcpu=native OB_HAS_MCPU_NATIVE)
    if(OB_HAS_MCPU_NATIVE)
        set(OB_ARCH_FLAG -mcpu=native)
    else()
        set(OB_ARCH_FLAG "")
    endif()
endif()

set(CMAKE_CXX_FLAGS_RELEASE
    "-O2 -fno-omit-frame-pointer ${OB_ARCH_FLAG} -DNDEBUG"
)
set(CMAKE_CXX_FLAGS_DEBUG "-O0 -g")

option(OB_ASAN "build with address and undefined behaviour sanitizers" OFF)
if(OB_ASAN)
    add_compile_options(-fsanitize=address,undefined -fno-omit-frame-pointer)
    add_link_options(-fsanitize=address,undefined)
endif()

execute_process(
    COMMAND git rev-parse --short HEAD
    WORKING_DIRECTORY ${CMAKE_SOURCE_DIR}
    OUTPUT_VARIABLE OB_GIT_COMMIT
    OUTPUT_STRIP_TRAILING_WHITESPACE
    ERROR_QUIET
)
if(NOT OB_GIT_COMMIT)
    set(OB_GIT_COMMIT unknown)
endif()

string(TOUPPER "${CMAKE_BUILD_TYPE}" OB_BUILD_TYPE_UPPER)
string(
    STRIP "${CMAKE_CXX_FLAGS} ${CMAKE_CXX_FLAGS_${OB_BUILD_TYPE_UPPER}}"
    OB_CXX_FLAGS
)
if(OB_ASAN)
    string(STRIP "${OB_CXX_FLAGS} -fsanitize=address,undefined" OB_CXX_FLAGS)
endif()

enable_testing()

add_subdirectory(matching)

add_executable(ob_genfeed ob/GenFeed.cpp ob/Feed.cpp)
target_include_directories(ob_genfeed PRIVATE ${CMAKE_SOURCE_DIR})
target_compile_options(ob_genfeed PRIVATE -Wall -Wextra)

add_executable(ob_bench ob/RunBench.cpp ob/Feed.cpp ob/AllocCounter.cpp)
target_include_directories(ob_bench PRIVATE ${CMAKE_SOURCE_DIR})
target_compile_options(ob_bench PRIVATE -Wall -Wextra)
target_compile_definitions(
    ob_bench
    PRIVATE OB_GIT_COMMIT="${OB_GIT_COMMIT}" OB_CXX_FLAGS="${OB_CXX_FLAGS}"
)

add_executable(ob_tests tests/ObTests.cpp ob/Feed.cpp ob/AllocCounter.cpp)
target_include_directories(ob_tests PRIVATE ${CMAKE_SOURCE_DIR})
target_compile_options(ob_tests PRIVATE -Wall -Wextra -UNDEBUG)

add_test(NAME ob COMMAND ob_tests)
