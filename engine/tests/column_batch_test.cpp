#include <catch2/catch_test_macros.hpp>

#include "object/column.h"
#include "object/column_batch.h"
#include "object/batch_builder.h"
#include "keys/registry.h"
#include "keys.h"

using namespace ranking_dsl;

TEST_CASE("Column operations", "[column]") {
  SECTION("Create empty column") {
    Column col;
    REQUIRE(col.Size() == 0);
  }

  SECTION("Create column with size") {
    Column col(10);
    REQUIRE(col.Size() == 10);
    // All values should be null
    for (size_t i = 0; i < 10; ++i) {
      REQUIRE(IsNull(col.Get(i)));
    }
  }

  SECTION("Set and get values") {
    Column col(3);
    col.Set(0, 1.5f);
    col.Set(1, 2.5f);
    col.Set(2, 3.5f);

    auto* v0 = std::get_if<float>(&col.Get(0));
    auto* v1 = std::get_if<float>(&col.Get(1));
    auto* v2 = std::get_if<float>(&col.Get(2));

    REQUIRE(v0 != nullptr);
    REQUIRE(v1 != nullptr);
    REQUIRE(v2 != nullptr);
    REQUIRE(*v0 == 1.5f);
    REQUIRE(*v1 == 2.5f);
    REQUIRE(*v2 == 3.5f);
  }

  SECTION("Auto-resize on set") {
    Column col;
    col.Set(5, 42.0f);
    REQUIRE(col.Size() == 6);
    REQUIRE(IsNull(col.Get(0)));
    auto* v = std::get_if<float>(&col.Get(5));
    REQUIRE(v != nullptr);
    REQUIRE(*v == 42.0f);
  }

  SECTION("Clone column") {
    Column col(3);
    col.Set(0, 1.0f);
    col.Set(1, 2.0f);
    col.Set(2, 3.0f);

    Column clone = col.Clone();
    REQUIRE(clone.Size() == col.Size());

    auto* orig = std::get_if<float>(&col.Get(1));
    auto* cloned = std::get_if<float>(&clone.Get(1));
    REQUIRE(*orig == *cloned);

    // Modify original, clone should be unchanged
    col.Set(1, 100.0f);
    cloned = std::get_if<float>(&clone.Get(1));
    REQUIRE(*cloned == 2.0f);
  }
}

TEST_CASE("ColumnBatch operations", "[column_batch]") {
  SECTION("Create empty batch") {
    ColumnBatch batch;
    REQUIRE(batch.RowCount() == 0);
    REQUIRE(batch.ColumnCount() == 0);
  }

  SECTION("Create batch with row count") {
    ColumnBatch batch(10);
    REQUIRE(batch.RowCount() == 10);
    REQUIRE(batch.ColumnCount() == 0);
  }

  SECTION("Add and get columns") {
    ColumnBatch batch(3);

    auto col1 = std::make_shared<Column>(3);
    col1->Set(0, 1.0f);
    col1->Set(1, 2.0f);
    col1->Set(2, 3.0f);

    batch.SetColumn(keys::id::SCORE_BASE, col1);

    REQUIRE(batch.ColumnCount() == 1);
    REQUIRE(batch.HasColumn(keys::id::SCORE_BASE));
    REQUIRE_FALSE(batch.HasColumn(keys::id::SCORE_FINAL));

    auto retrieved = batch.GetColumn(keys::id::SCORE_BASE);
    REQUIRE(retrieved == col1);  // Same pointer
  }

  SECTION("GetValue") {
    ColumnBatch batch(3);

    auto col = std::make_shared<Column>(3);
    col->Set(0, 10.0f);
    col->Set(1, 20.0f);
    col->Set(2, 30.0f);
    batch.SetColumn(keys::id::SCORE_BASE, col);

    auto val = batch.GetValue(1, keys::id::SCORE_BASE);
    auto* f = std::get_if<float>(&val);
    REQUIRE(f != nullptr);
    REQUIRE(*f == 20.0f);

    // Missing column returns null
    auto missing = batch.GetValue(1, keys::id::SCORE_FINAL);
    REQUIRE(IsNull(missing));
  }

  SECTION("UseCount for column sharing") {
    ColumnBatch batch(3);

    auto col = std::make_shared<Column>(3);
    REQUIRE(col.use_count() == 1);

    batch.SetColumn(keys::id::SCORE_BASE, col);
    REQUIRE(col.use_count() == 2);  // batch + local var

    REQUIRE(batch.UseCount(keys::id::SCORE_BASE) == 2);
  }
}

TEST_CASE("BatchBuilder COW semantics", "[batch_builder]") {
  // Create a source batch with two columns
  auto id_col = std::make_shared<Column>(3);
  id_col->Set(0, int64_t{1});
  id_col->Set(1, int64_t{2});
  id_col->Set(2, int64_t{3});

  auto score_col = std::make_shared<Column>(3);
  score_col->Set(0, 0.5f);
  score_col->Set(1, 0.6f);
  score_col->Set(2, 0.7f);

  ColumnBatch source(3);
  source.SetColumn(keys::id::CAND_CANDIDATE_ID, id_col);
  source.SetColumn(keys::id::SCORE_BASE, score_col);

  SECTION("Adding new column shares existing columns") {
    BatchBuilder builder(source);

    // Add a new column
    auto new_col = std::make_shared<Column>(3);
    new_col->Set(0, 1.0f);
    new_col->Set(1, 2.0f);
    new_col->Set(2, 3.0f);
    builder.AddColumn(keys::id::SCORE_FINAL, new_col);

    ColumnBatch result = builder.Build();

    // Original columns should be shared
    REQUIRE(result.GetColumn(keys::id::CAND_CANDIDATE_ID) == id_col);
    REQUIRE(result.GetColumn(keys::id::SCORE_BASE) == score_col);

    // New column should exist
    REQUIRE(result.HasColumn(keys::id::SCORE_FINAL));

    // Verify use counts show sharing
    // id_col: source + result = 2 (plus the local var makes 3)
    REQUIRE(id_col.use_count() == 3);
    REQUIRE(score_col.use_count() == 3);
  }

  SECTION("Modifying existing column triggers COW") {
    // Record initial use count
    long initial_score_count = score_col.use_count();

    BatchBuilder builder(source);

    // Modify a value in score column
    builder.Set(1, keys::id::SCORE_BASE, 0.99f);

    ColumnBatch result = builder.Build();

    // ID column should still be shared (not modified)
    REQUIRE(result.GetColumn(keys::id::CAND_CANDIDATE_ID) == id_col);

    // Score column should NOT be shared (it was modified - COW triggered)
    REQUIRE(result.GetColumn(keys::id::SCORE_BASE) != score_col);

    // Verify the original is unchanged
    auto* orig_val = std::get_if<float>(&score_col->Get(1));
    REQUIRE(orig_val != nullptr);
    REQUIRE(*orig_val == 0.6f);

    // Verify the result has the new value
    auto result_score_col = result.GetColumn(keys::id::SCORE_BASE);
    auto* new_val = std::get_if<float>(&result_score_col->Get(1));
    REQUIRE(new_val != nullptr);
    REQUIRE(*new_val == 0.99f);

    // Other values in the COW'd column should be preserved
    auto* val0 = std::get_if<float>(&result_score_col->Get(0));
    auto* val2 = std::get_if<float>(&result_score_col->Get(2));
    REQUIRE(*val0 == 0.5f);
    REQUIRE(*val2 == 0.7f);
  }

  SECTION("Multiple modifications to same column share one COW copy") {
    BatchBuilder builder(source);

    builder.Set(0, keys::id::SCORE_BASE, 0.1f);
    builder.Set(1, keys::id::SCORE_BASE, 0.2f);
    builder.Set(2, keys::id::SCORE_BASE, 0.3f);

    ColumnBatch result = builder.Build();

    // Score column should be modified once (COW'd once)
    REQUIRE(result.GetColumn(keys::id::SCORE_BASE) != score_col);

    auto result_col = result.GetColumn(keys::id::SCORE_BASE);
    auto* v0 = std::get_if<float>(&result_col->Get(0));
    auto* v1 = std::get_if<float>(&result_col->Get(1));
    auto* v2 = std::get_if<float>(&result_col->Get(2));
    REQUIRE(*v0 == 0.1f);
    REQUIRE(*v1 == 0.2f);
    REQUIRE(*v2 == 0.3f);
  }

  SECTION("Builder from empty creates new batch") {
    BatchBuilder builder(3);

    builder.Set(0, keys::id::SCORE_BASE, 1.0f);
    builder.Set(1, keys::id::SCORE_BASE, 2.0f);
    builder.Set(2, keys::id::SCORE_BASE, 3.0f);

    ColumnBatch result = builder.Build();

    REQUIRE(result.RowCount() == 3);
    REQUIRE(result.HasColumn(keys::id::SCORE_BASE));
  }
}
