cmake_minimum_required(VERSION 3.20)
project(ranking_dsl_engine VERSION 0.1.0 LANGUAGES CXX)

# C++20 required
set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)

# Export compile_commands.json for IDE support
set(CMAKE_EXPORT_COMPILE_COMMANDS ON)

# Options
option(RANKING_DSL_BUILD_TESTS "Build tests" ON)

# FetchContent for dependencies
include(FetchContent)

# fmt library
FetchContent_Declare(
  fmt
  GIT_REPOSITORY https://github.com/fmtlib/fmt.git
  GIT_TAG 10.2.1
)
FetchContent_MakeAvailable(fmt)

# nlohmann/json
FetchContent_Declare(
  json
  GIT_REPOSITORY https://github.com/nlohmann/json.git
  GIT_TAG v3.11.3
)
FetchContent_MakeAvailable(json)

# Catch2 for testing
if(RANKING_DSL_BUILD_TESTS)
  FetchContent_Declare(
    Catch2
    GIT_REPOSITORY https://github.com/catchorg/Catch2.git
    GIT_TAG v3.5.2
  )
  FetchContent_MakeAvailable(Catch2)
  list(APPEND CMAKE_MODULE_PATH ${catch2_SOURCE_DIR}/extras)
endif()

# Generated keys header location
set(GENERATED_KEYS_DIR "${CMAKE_SOURCE_DIR}/../keys/generated")

# Engine library
add_library(ranking_dsl_engine
  src/keys/registry.cpp
  src/object/value.cpp
  src/object/obj.cpp
  src/expr/expr.cpp
  src/plan/plan.cpp
  src/plan/compiler.cpp
  src/nodes/registry.cpp
  src/nodes/core/sourcer.cpp
  src/nodes/core/merge.cpp
  src/nodes/core/features.cpp
  src/nodes/core/model.cpp
  src/nodes/core/score_formula.cpp
  src/executor/executor.cpp
  src/logging/trace.cpp
)

target_include_directories(ranking_dsl_engine
  PUBLIC
    ${CMAKE_CURRENT_SOURCE_DIR}/src
    ${GENERATED_KEYS_DIR}
)

target_link_libraries(ranking_dsl_engine
  PUBLIC
    fmt::fmt
    nlohmann_json::nlohmann_json
)

# Main executable
add_executable(rankdsl_engine src/main.cpp)
target_link_libraries(rankdsl_engine PRIVATE ranking_dsl_engine)

# Tests
if(RANKING_DSL_BUILD_TESTS)
  enable_testing()
  include(CTest)
  include(Catch)

  add_executable(ranking_dsl_tests
    tests/test_main.cpp
    tests/obj_test.cpp
    tests/expr_eval_test.cpp
    tests/plan_compiler_test.cpp
    tests/key_enforcement_test.cpp
  )

  target_link_libraries(ranking_dsl_tests
    PRIVATE
      ranking_dsl_engine
      Catch2::Catch2WithMain
  )

  catch_discover_tests(ranking_dsl_tests)
endif()
