cmake_minimum_required(VERSION 3.20)
project(ranking_dsl_engine VERSION 0.1.0 LANGUAGES C CXX)

# C++20 required
set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)

# Options for sanitizers
option(RANKING_DSL_ENABLE_ASAN "Enable AddressSanitizer" OFF)
option(RANKING_DSL_ENABLE_UBSAN "Enable UndefinedBehaviorSanitizer" OFF)

if(RANKING_DSL_ENABLE_ASAN)
  add_compile_options(-fsanitize=address -fno-omit-frame-pointer)
  add_link_options(-fsanitize=address)
endif()

if(RANKING_DSL_ENABLE_UBSAN)
  add_compile_options(-fsanitize=undefined)
  add_link_options(-fsanitize=undefined)
endif()

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

# CLI11 for argument parsing
FetchContent_Declare(
  cli11
  GIT_REPOSITORY https://github.com/CLIUtils/CLI11.git
  GIT_TAG v2.4.2
)
FetchContent_MakeAvailable(cli11)

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

# QuickJS for njs execution (using bellard's official repo)
FetchContent_Declare(
  quickjs
  GIT_REPOSITORY https://github.com/bellard/quickjs.git
  GIT_TAG master
)
FetchContent_Populate(quickjs)

# Build QuickJS as a static library (minimal - just what we need)
add_library(quickjs_lib STATIC
  ${quickjs_SOURCE_DIR}/quickjs.c
  ${quickjs_SOURCE_DIR}/libregexp.c
  ${quickjs_SOURCE_DIR}/libunicode.c
  ${quickjs_SOURCE_DIR}/cutils.c
  ${quickjs_SOURCE_DIR}/dtoa.c
)

# Use PRIVATE to avoid polluting include paths (quickjs has a 'version' file that conflicts)
target_include_directories(quickjs_lib PRIVATE ${quickjs_SOURCE_DIR})
target_compile_definitions(quickjs_lib PRIVATE
  CONFIG_VERSION="2024-12-30"
)

# Copy quickjs.h to a safe include directory (quickjs directory has a 'version' file that conflicts with C++ std::version)
set(QUICKJS_INCLUDE_DIR "${CMAKE_CURRENT_BINARY_DIR}/quickjs-include")
file(MAKE_DIRECTORY ${QUICKJS_INCLUDE_DIR})
file(COPY ${quickjs_SOURCE_DIR}/quickjs.h DESTINATION ${QUICKJS_INCLUDE_DIR})

# Silence warnings in QuickJS (it's a C library with different style)
if(CMAKE_C_COMPILER_ID MATCHES "Clang|GNU")
  target_compile_options(quickjs_lib PRIVATE
    -Wno-sign-compare
    -Wno-unused-parameter
    -Wno-implicit-fallthrough
    -Wno-missing-field-initializers
    -Wno-implicit-const-int-float-conversion
  )
endif()

# Generated keys header location (use CMAKE_CURRENT_SOURCE_DIR for correct path when built from root)
set(GENERATED_KEYS_DIR "${CMAKE_CURRENT_SOURCE_DIR}/../keys/generated")

# Engine library
add_library(ranking_dsl_engine
  src/keys/registry.cpp
  src/object/value.cpp
  src/object/obj.cpp
  src/object/column.cpp
  src/object/typed_column.cpp
  src/object/column_batch.cpp
  src/object/batch_builder.cpp
  src/object/row_view.cpp
  src/expr/expr.cpp
  src/plan/plan.cpp
  src/plan/compiler.cpp
  src/nodes/registry.cpp
  src/nodes/core/sourcer.cpp
  src/nodes/core/merge.cpp
  src/nodes/core/features.cpp
  src/nodes/core/model.cpp
  src/nodes/core/score_formula.cpp
  src/nodes/js/batch_context.cpp
  src/nodes/js/njs_runner.cpp
  src/executor/executor.cpp
  src/logging/trace.cpp
)

target_include_directories(ranking_dsl_engine
  PUBLIC
    ${CMAKE_CURRENT_SOURCE_DIR}/src
    ${GENERATED_KEYS_DIR}
  PRIVATE
    ${QUICKJS_INCLUDE_DIR}
)

target_link_libraries(ranking_dsl_engine
  PUBLIC
    fmt::fmt
    nlohmann_json::nlohmann_json
    quickjs_lib
)

# Main executable
add_executable(rankdsl_engine src/main.cpp)
target_link_libraries(rankdsl_engine PRIVATE ranking_dsl_engine CLI11::CLI11)

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
    tests/column_batch_test.cpp
    tests/row_view_test.cpp
    tests/columnar_eval_test.cpp
    tests/njs_runner_test.cpp
  )

  target_link_libraries(ranking_dsl_tests
    PRIVATE
      ranking_dsl_engine
      Catch2::Catch2WithMain
  )

  # Copy test data files to build directory so tests can find them
  file(COPY ${CMAKE_CURRENT_SOURCE_DIR}/tests/testdata DESTINATION ${CMAKE_BINARY_DIR}/engine/tests)

  catch_discover_tests(ranking_dsl_tests
    WORKING_DIRECTORY ${CMAKE_BINARY_DIR}
  )
endif()
