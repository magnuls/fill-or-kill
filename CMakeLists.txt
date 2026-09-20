cmake_minimum_required(VERSION 3.20)
project(lob CXX)

set(CMAKE_CXX_STANDARD 23)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_EXPORT_COMPILE_COMMANDS ON)

# types, order books, and strategies
add_library(
    lob
    STATIC
    src/Order.cpp
    src/Trade.cpp
    src/books/ProRataOrderBook.cpp
)
target_include_directories(lob PUBLIC include)
target_compile_features(lob PUBLIC cxx_std_23)
target_compile_options(lob PRIVATE -Wall -Wextra)

# normal entry point  ./build/lob
add_executable(lob_main main.cpp)
target_link_libraries(lob_main PRIVATE lob)
target_compile_options(lob_main PRIVATE -Wall -Wextra)
set_target_properties(lob_main PROPERTIES OUTPUT_NAME lob)

# testing entry ./build/lob_tests
add_executable(lob_tests tests/ProRataOrderBookTest.cpp)
target_link_libraries(lob_tests PRIVATE lob)
target_compile_options(lob_tests PRIVATE -Wall -Wextra)

enable_testing()
add_test(NAME prorata COMMAND lob_tests)
