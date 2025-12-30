cmake_minimum_required(VERSION 3.20)
project(ranking_dsl VERSION 0.1.0 LANGUAGES CXX)

# Enable testing at root level so ctest can find tests
enable_testing()

# Build the engine (which includes all C++ code and tests)
add_subdirectory(engine)
