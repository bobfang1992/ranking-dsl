#include <iostream>
#include <string>

#include <fmt/format.h>

#include "executor/executor.h"
#include "keys/registry.h"
#include "plan/compiler.h"
#include "plan/plan.h"
#include "logging/trace.h"
#include "keys.h"

using namespace ranking_dsl;

void PrintUsage(const char* prog) {
  fmt::print("Usage: {} <plan.json> [--keys <keys.json>] [--dump-top N] [--quiet]\n", prog);
}

int main(int argc, char* argv[]) {
  if (argc < 2) {
    PrintUsage(argv[0]);
    return 1;
  }

  std::string plan_path;
  std::string keys_path;
  int dump_top = 0;
  bool quiet = false;

  for (int i = 1; i < argc; ++i) {
    std::string arg = argv[i];
    if (arg == "--keys" && i + 1 < argc) {
      keys_path = argv[++i];
    } else if (arg == "--dump-top" && i + 1 < argc) {
      dump_top = std::stoi(argv[++i]);
    } else if (arg == "--quiet") {
      quiet = true;
    } else if (arg[0] != '-') {
      plan_path = arg;
    } else {
      fmt::print(stderr, "Unknown option: {}\n", arg);
      PrintUsage(argv[0]);
      return 1;
    }
  }

  if (plan_path.empty()) {
    fmt::print(stderr, "Error: plan path required\n");
    PrintUsage(argv[0]);
    return 1;
  }

  // Set tracing based on quiet flag
  Tracer::SetEnabled(!quiet);

  // Load key registry
  KeyRegistry registry;
  if (!keys_path.empty()) {
    std::string error;
    if (!registry.LoadFromFile(keys_path, &error)) {
      fmt::print(stderr, "Error loading keys: {}\n", error);
      return 1;
    }
  } else {
    // Use compiled-in keys
    registry.LoadFromCompiled();
  }

  // Load plan
  Plan plan;
  std::string error;
  if (!ParsePlanFile(plan_path, plan, &error)) {
    fmt::print(stderr, "Error loading plan: {}\n", error);
    return 1;
  }

  // Compile plan
  PlanCompiler compiler(registry);
  CompiledPlan compiled;
  if (!compiler.Compile(plan, compiled, &error)) {
    fmt::print(stderr, "Error compiling plan: {}\n", error);
    return 1;
  }

  // Execute plan
  Executor executor(registry);
  CandidateBatch result = executor.Execute(compiled, &error);
  if (!error.empty()) {
    fmt::print(stderr, "Error executing plan: {}\n", error);
    return 1;
  }

  // Output results (using columnar API)
  if (!quiet) {
    size_t row_count = result.RowCount();
    fmt::print("\n=== Results ({} candidates) ===\n", row_count);

    size_t count = dump_top > 0 ? std::min(static_cast<size_t>(dump_top), row_count)
                                : row_count;

    // Get typed columns for faster access
    auto* id_col = result.GetI64Column(keys::id::CAND_CANDIDATE_ID);
    auto* score_col = result.GetF32Column(keys::id::SCORE_FINAL);

    for (size_t i = 0; i < count; ++i) {
      int64_t id = 0;
      float score = 0.0f;

      if (id_col && !id_col->IsNull(i)) {
        id = id_col->Get(i);
      }
      if (score_col && !score_col->IsNull(i)) {
        score = score_col->Get(i);
      }

      fmt::print("  [{}] candidate_id={}, score.final={:.4f}\n", i, id, score);
    }
  }

  return 0;
}
