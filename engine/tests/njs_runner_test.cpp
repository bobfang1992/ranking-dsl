#include <catch2/catch_test_macros.hpp>
#include <catch2/catch_approx.hpp>
#include <catch2/matchers/catch_matchers_string.hpp>

#include "nodes/js/njs_runner.h"
#include "nodes/js/batch_context.h"
#include "object/column_batch.h"
#include "object/batch_builder.h"
#include "object/typed_column.h"
#include "keys/registry.h"
#include "keys.h"

using namespace ranking_dsl;

TEST_CASE("BatchContext read APIs", "[njs][batch_context]") {
  // Create a batch with test data
  auto score_col = std::make_shared<F32Column>(3);
  score_col->Set(0, 0.5f);
  score_col->Set(1, 0.6f);
  score_col->Set(2, 0.7f);

  auto id_col = std::make_shared<I64Column>(3);
  id_col->Set(0, int64_t{100});
  id_col->Set(1, int64_t{200});
  id_col->Set(2, int64_t{300});

  auto vec_col = std::make_shared<F32VecColumn>(3, 3);  // 3 rows, dim 3
  vec_col->Set(0, std::vector<float>{1.0f, 2.0f, 3.0f});
  vec_col->Set(1, std::vector<float>{4.0f, 5.0f, 6.0f});
  vec_col->Set(2, std::vector<float>{7.0f, 8.0f, 9.0f});

  ColumnBatch batch(3);
  batch.SetColumn(keys::id::SCORE_BASE, score_col);
  batch.SetColumn(keys::id::CAND_CANDIDATE_ID, id_col);
  batch.SetColumn(keys::id::FEAT_EMBEDDING, vec_col);

  BatchBuilder builder(batch);
  NjsBudget budget;
  std::set<int32_t> allowed_writes;

  BatchContext ctx(batch, builder, nullptr, allowed_writes, budget);

  SECTION("rowCount") {
    REQUIRE(ctx.RowCount() == 3);
  }

  SECTION("GetF32 returns column values") {
    auto values = ctx.GetF32(keys::id::SCORE_BASE);
    REQUIRE(values.size() == 3);
    REQUIRE(values[0] == 0.5f);
    REQUIRE(values[1] == 0.6f);
    REQUIRE(values[2] == 0.7f);
  }

  SECTION("GetF32Raw returns zero-copy pointer") {
    auto [data, size] = ctx.GetF32Raw(keys::id::SCORE_BASE);
    REQUIRE(data != nullptr);
    REQUIRE(size == 3);
    REQUIRE(data[0] == 0.5f);
    REQUIRE(data[1] == 0.6f);
    REQUIRE(data[2] == 0.7f);
  }

  SECTION("GetF32 returns zeros for missing column") {
    auto values = ctx.GetF32(keys::id::SCORE_FINAL);
    REQUIRE(values.size() == 3);
    REQUIRE(values[0] == 0.0f);
    REQUIRE(values[1] == 0.0f);
    REQUIRE(values[2] == 0.0f);
  }

  SECTION("GetI64 returns column values") {
    auto values = ctx.GetI64(keys::id::CAND_CANDIDATE_ID);
    REQUIRE(values.size() == 3);
    REQUIRE(values[0] == 100);
    REQUIRE(values[1] == 200);
    REQUIRE(values[2] == 300);
  }

  SECTION("GetF32VecRaw returns contiguous N*D storage") {
    auto view = ctx.GetF32VecRaw(keys::id::FEAT_EMBEDDING);
    REQUIRE(view.data != nullptr);
    REQUIRE(view.dim == 3);
    REQUIRE(view.row_count == 3);
    REQUIRE(view.data_size == 9);  // 3 rows * 3 dims

    // Check contiguous layout
    REQUIRE(view.data[0] == 1.0f);   // row 0, dim 0
    REQUIRE(view.data[3] == 4.0f);   // row 1, dim 0
    REQUIRE(view.data[6] == 7.0f);   // row 2, dim 0

    // Check GetRow helper
    REQUIRE(view.GetRow(1)[0] == 4.0f);
    REQUIRE(view.GetRow(1)[2] == 6.0f);
  }

  SECTION("GetF32Vec returns vector of vectors (legacy)") {
    auto values = ctx.GetF32Vec(keys::id::FEAT_EMBEDDING);
    REQUIRE(values.size() == 3);
    REQUIRE(values[0] == std::vector<float>{1.0f, 2.0f, 3.0f});
    REQUIRE(values[1] == std::vector<float>{4.0f, 5.0f, 6.0f});
    REQUIRE(values[2] == std::vector<float>{7.0f, 8.0f, 9.0f});
  }
}

TEST_CASE("BatchContext write APIs", "[njs][batch_context]") {
  auto score_col = std::make_shared<F32Column>(3);
  score_col->Set(0, 0.5f);
  score_col->Set(1, 0.6f);
  score_col->Set(2, 0.7f);

  ColumnBatch batch(3);
  batch.SetColumn(keys::id::SCORE_BASE, score_col);

  KeyRegistry registry;
  registry.LoadFromCompiled();

  SECTION("AllocateF32 returns pointer for direct writes") {
    BatchBuilder builder(batch);
    NjsBudget budget;
    std::set<int32_t> allowed_writes = {keys::id::SCORE_FINAL};

    BatchContext ctx(batch, builder, &registry, allowed_writes, budget);

    float* data = ctx.AllocateF32(keys::id::SCORE_FINAL);
    REQUIRE(data != nullptr);

    // Direct write to contiguous storage
    data[0] = 1.0f;
    data[1] = 2.0f;
    data[2] = 3.0f;

    ctx.Commit();
    ColumnBatch result = builder.Build();

    REQUIRE(result.HasColumn(keys::id::SCORE_FINAL));
    auto* final_col = result.GetF32Column(keys::id::SCORE_FINAL);
    REQUIRE(final_col->Get(0) == 1.0f);
    REQUIRE(final_col->Get(1) == 2.0f);
    REQUIRE(final_col->Get(2) == 3.0f);
  }

  SECTION("AllocateF32Vec returns contiguous N*D storage") {
    BatchBuilder builder(batch);
    NjsBudget budget;
    std::set<int32_t> allowed_writes = {keys::id::FEAT_EMBEDDING};

    BatchContext ctx(batch, builder, &registry, allowed_writes, budget);

    float* data = ctx.AllocateF32Vec(keys::id::FEAT_EMBEDDING, 3);
    REQUIRE(data != nullptr);

    // Write contiguous N*D data
    for (size_t i = 0; i < 9; ++i) {
      data[i] = static_cast<float>(i);
    }

    ctx.Commit();
    ColumnBatch result = builder.Build();

    auto* vec_col = result.GetF32VecColumn(keys::id::FEAT_EMBEDDING);
    REQUIRE(vec_col != nullptr);
    REQUIRE(vec_col->Dim() == 3);
    REQUIRE(vec_col->Data()[0] == 0.0f);
    REQUIRE(vec_col->Data()[4] == 4.0f);
  }

  SECTION("AllocateI64 returns pointer for direct writes") {
    BatchBuilder builder(batch);
    NjsBudget budget;
    std::set<int32_t> allowed_writes = {keys::id::CAND_CANDIDATE_ID};

    BatchContext ctx(batch, builder, &registry, allowed_writes, budget);

    int64_t* data = ctx.AllocateI64(keys::id::CAND_CANDIDATE_ID);
    REQUIRE(data != nullptr);

    data[0] = 1000;
    data[1] = 2000;
    data[2] = 3000;

    ctx.Commit();
    ColumnBatch result = builder.Build();

    auto* id_col = result.GetI64Column(keys::id::CAND_CANDIDATE_ID);
    REQUIRE(id_col->Get(0) == 1000);
  }
}

TEST_CASE("BatchContext meta.writes enforcement", "[njs][enforcement]") {
  ColumnBatch batch(3);

  KeyRegistry registry;
  registry.LoadFromCompiled();

  BatchBuilder builder(batch);
  NjsBudget budget;
  std::set<int32_t> allowed_writes = {keys::id::SCORE_FINAL};  // Only SCORE_FINAL allowed

  BatchContext ctx(batch, builder, &registry, allowed_writes, budget);

  SECTION("Write to allowed key succeeds") {
    REQUIRE_NOTHROW(ctx.AllocateF32(keys::id::SCORE_FINAL));
  }

  SECTION("Write to non-allowed key throws") {
    REQUIRE_THROWS_WITH(
        ctx.AllocateF32(keys::id::SCORE_BASE),
        Catch::Matchers::ContainsSubstring("not in meta.writes"));
  }

  SECTION("Write wrong type throws") {
    // SCORE_FINAL is F32, trying to allocate I64 should fail
    std::set<int32_t> all_writes = {keys::id::SCORE_FINAL};
    BatchBuilder builder2(batch);
    BatchContext ctx2(batch, builder2, &registry, all_writes, budget);

    REQUIRE_THROWS_WITH(
        ctx2.AllocateI64(keys::id::SCORE_FINAL),
        Catch::Matchers::ContainsSubstring("Type mismatch"));
  }
}

TEST_CASE("BatchContext budget enforcement", "[njs][budget]") {
  ColumnBatch batch(100);  // 100 rows

  KeyRegistry registry;
  registry.LoadFromCompiled();

  SECTION("max_write_bytes limit") {
    BatchBuilder builder(batch);
    NjsBudget budget;
    budget.max_write_bytes = 100;  // Very small limit
    budget.max_write_cells = 1000000;
    std::set<int32_t> allowed_writes = {keys::id::SCORE_FINAL};

    BatchContext ctx(batch, builder, &registry, allowed_writes, budget);

    // Allocating 100 floats (400 bytes) should exceed 100 byte limit
    REQUIRE_THROWS_WITH(
        ctx.AllocateF32(keys::id::SCORE_FINAL),
        Catch::Matchers::ContainsSubstring("max_write_bytes"));
  }

  SECTION("max_write_cells limit") {
    BatchBuilder builder(batch);
    NjsBudget budget;
    budget.max_write_bytes = 1000000;
    budget.max_write_cells = 50;  // Less than 100 rows
    std::set<int32_t> allowed_writes = {keys::id::SCORE_FINAL};

    BatchContext ctx(batch, builder, &registry, allowed_writes, budget);

    // Allocating 100 cells should exceed 50 cell limit
    REQUIRE_THROWS_WITH(
        ctx.AllocateF32(keys::id::SCORE_FINAL),
        Catch::Matchers::ContainsSubstring("max_write_cells"));
  }

  SECTION("Budget accumulates across allocations") {
    BatchBuilder builder(batch);
    NjsBudget budget;
    budget.max_write_bytes = 1000000;
    budget.max_write_cells = 150;  // Enough for one allocation, not two
    std::set<int32_t> allowed_writes = {keys::id::SCORE_FINAL, keys::id::SCORE_ADJUSTED};

    BatchContext ctx(batch, builder, &registry, allowed_writes, budget);

    // First allocation uses 100 cells
    REQUIRE_NOTHROW(ctx.AllocateF32(keys::id::SCORE_FINAL));

    // Second allocation would use another 100 cells, exceeding 150 limit
    REQUIRE_THROWS_WITH(
        ctx.AllocateF32(keys::id::SCORE_ADJUSTED),
        Catch::Matchers::ContainsSubstring("max_write_cells"));
  }
}

TEST_CASE("NjsRunner with column function", "[njs][runner]") {
  // Create input batch
  auto score_col = std::make_shared<F32Column>(3);
  score_col->Set(0, 0.5f);
  score_col->Set(1, 0.6f);
  score_col->Set(2, 0.7f);

  ColumnBatch batch(3);
  batch.SetColumn(keys::id::SCORE_BASE, score_col);

  KeyRegistry registry;
  registry.LoadFromCompiled();

  ExecContext exec_ctx;
  exec_ctx.registry = &registry;

  NjsRunner runner;

  SECTION("Column-level function writes new column via zero-copy API") {
    NjsMeta meta;
    meta.writes = {keys::id::SCORE_FINAL};
    meta.budget.max_write_bytes = 1000000;
    meta.budget.max_write_cells = 1000;

    nlohmann::json params = {{"alpha", 2.0}};

    auto result = runner.RunWithMeta(
        exec_ctx, batch, params, meta,
        [](BatchContext& ctx, const nlohmann::json& params) {
          auto [input, size] = ctx.GetF32Raw(keys::id::SCORE_BASE);
          float alpha = params["alpha"].get<float>();

          float* output = ctx.AllocateF32(keys::id::SCORE_FINAL);
          for (size_t i = 0; i < ctx.RowCount(); ++i) {
            output[i] = (input ? input[i] : 0.0f) * alpha;
          }
        });

    REQUIRE(result.RowCount() == 3);
    REQUIRE(result.HasColumn(keys::id::SCORE_FINAL));

    auto* final_col = result.GetF32Column(keys::id::SCORE_FINAL);
    REQUIRE(final_col->Get(0) == Catch::Approx(1.0f));
    REQUIRE(final_col->Get(1) == Catch::Approx(1.2f));
    REQUIRE(final_col->Get(2) == Catch::Approx(1.4f));

    // Original column should be shared
    REQUIRE(result.GetColumn(keys::id::SCORE_BASE) == score_col);
  }

  SECTION("HasColumnWrites tracks usage") {
    NjsMeta meta;
    meta.writes = {keys::id::SCORE_FINAL};

    nlohmann::json params;

    // Function that doesn't use column writers
    auto result_no_writes = runner.RunWithMeta(
        exec_ctx, batch, params, meta,
        [](BatchContext& ctx, const nlohmann::json&) {
          // Just read, no writes
          auto _ = ctx.GetF32(keys::id::SCORE_BASE);
        });

    // Should return unchanged batch
    REQUIRE(result_no_writes.GetColumn(keys::id::SCORE_BASE) == score_col);
    REQUIRE_FALSE(result_no_writes.HasColumn(keys::id::SCORE_FINAL));
  }
}

TEST_CASE("NjsMeta parsing", "[njs][meta]") {
  SECTION("Parse complete meta") {
    auto j = nlohmann::json::parse(R"({
      "name": "test_module",
      "version": "1.0.0",
      "reads": [3001, 3002],
      "writes": [3999],
      "params": {"alpha": {"type": "number"}},
      "budget": {
        "max_write_bytes": 2000000,
        "max_write_cells": 50000,
        "max_set_per_obj": 5
      }
    })");

    NjsMeta meta = NjsMeta::Parse(j);

    REQUIRE(meta.name == "test_module");
    REQUIRE(meta.version == "1.0.0");
    REQUIRE(meta.reads.count(3001) == 1);
    REQUIRE(meta.reads.count(3002) == 1);
    REQUIRE(meta.writes.count(3999) == 1);
    REQUIRE(meta.budget.max_write_bytes == 2000000);
    REQUIRE(meta.budget.max_write_cells == 50000);
    REQUIRE(meta.budget.max_set_per_obj == 5);
  }

  SECTION("Parse minimal meta uses defaults") {
    auto j = nlohmann::json::parse(R"({"name": "minimal"})");

    NjsMeta meta = NjsMeta::Parse(j);

    REQUIRE(meta.name == "minimal");
    REQUIRE(meta.reads.empty());
    REQUIRE(meta.writes.empty());
    REQUIRE(meta.budget.max_write_bytes == 1048576);  // 1MB default
    REQUIRE(meta.budget.max_write_cells == 100000);   // 100k default
  }
}

// ============================================================================
// QuickJS Execution Tests - These actually run JavaScript via QuickJS
// ============================================================================

// Helper to get the test data directory (relative to where tests are run from)
static std::string GetTestDataDir() {
  // Tests are run from build directory, testdata is in engine/tests/testdata
  return "engine/tests/testdata/";
}

TEST_CASE("QuickJS execution - valid module", "[njs][quickjs]") {
  // Create input batch with score.base column (key 3001)
  auto score_col = std::make_shared<F32Column>(3);
  score_col->Set(0, 1.0f);
  score_col->Set(1, 2.0f);
  score_col->Set(2, 3.0f);

  ColumnBatch batch(3);
  batch.SetColumn(keys::id::SCORE_BASE, score_col);

  KeyRegistry registry;
  registry.LoadFromCompiled();

  ExecContext exec_ctx;
  exec_ctx.registry = &registry;

  NjsRunner runner;

  nlohmann::json params;
  params["module"] = GetTestDataDir() + "valid_module.njs";

  // This should execute the JS code and write to score.ml (key 3002)
  CandidateBatch result = runner.Run(exec_ctx, batch, params);

  REQUIRE(result.RowCount() == 3);
  REQUIRE(result.HasColumn(keys::id::SCORE_ML));

  auto* ml_col = result.GetF32Column(keys::id::SCORE_ML);
  REQUIRE(ml_col != nullptr);
  // The valid_module.njs writes 42.0 to all rows
  REQUIRE(ml_col->Get(0) == Catch::Approx(42.0f));
  REQUIRE(ml_col->Get(1) == Catch::Approx(42.0f));
  REQUIRE(ml_col->Get(2) == Catch::Approx(42.0f));
}

TEST_CASE("QuickJS execution - unauthorized write fails", "[njs][quickjs][enforcement]") {
  // Create input batch
  auto score_col = std::make_shared<F32Column>(3);
  score_col->Set(0, 1.0f);
  score_col->Set(1, 2.0f);
  score_col->Set(2, 3.0f);

  ColumnBatch batch(3);
  batch.SetColumn(keys::id::SCORE_BASE, score_col);

  KeyRegistry registry;
  registry.LoadFromCompiled();

  ExecContext exec_ctx;
  exec_ctx.registry = &registry;

  NjsRunner runner;

  nlohmann::json params;
  params["module"] = GetTestDataDir() + "unauthorized_write.njs";

  // This module tries to write to key 3003 which is NOT in its meta.writes
  // It should throw an error when JS tries to call ctx.batch.writeF32(3003)
  REQUIRE_THROWS_WITH(
      runner.Run(exec_ctx, batch, params),
      Catch::Matchers::ContainsSubstring("not in meta.writes"));
}

TEST_CASE("QuickJS execution - budget exceeded fails", "[njs][quickjs][budget]") {
  // Create a large input batch (100 rows) to exceed the budget
  auto score_col = std::make_shared<F32Column>(100);
  for (size_t i = 0; i < 100; i++) {
    score_col->Set(i, static_cast<float>(i));
  }

  ColumnBatch batch(100);
  batch.SetColumn(keys::id::SCORE_BASE, score_col);

  KeyRegistry registry;
  registry.LoadFromCompiled();

  ExecContext exec_ctx;
  exec_ctx.registry = &registry;

  NjsRunner runner;

  nlohmann::json params;
  params["module"] = GetTestDataDir() + "budget_exceeded.njs";

  // This module has budget max_write_cells=10, but we're passing 100 rows
  // It should fail when trying to allocate the write array
  REQUIRE_THROWS_WITH(
      runner.Run(exec_ctx, batch, params),
      Catch::Matchers::ContainsSubstring("max_write_cells"));
}
