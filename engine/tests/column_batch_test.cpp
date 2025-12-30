#include <catch2/catch_test_macros.hpp>
#include <catch2/catch_approx.hpp>

#include "object/typed_column.h"
#include "object/column_batch.h"
#include "object/batch_builder.h"
#include "keys/registry.h"
#include "keys.h"

using namespace ranking_dsl;

TEST_CASE("TypedColumn operations", "[column]") {
  SECTION("Create empty F32Column") {
    F32Column col;
    REQUIRE(col.Size() == 0);
  }

  SECTION("Create F32Column with size") {
    F32Column col(10);
    REQUIRE(col.Size() == 10);
    // All values should be null
    for (size_t i = 0; i < 10; ++i) {
      REQUIRE(col.IsNull(i));
    }
  }

  SECTION("F32Column set and get values") {
    F32Column col(3);
    col.Set(0, 1.5f);
    col.Set(1, 2.5f);
    col.Set(2, 3.5f);

    REQUIRE(col.Get(0) == 1.5f);
    REQUIRE(col.Get(1) == 2.5f);
    REQUIRE(col.Get(2) == 3.5f);
    REQUIRE_FALSE(col.IsNull(0));
  }

  SECTION("I64Column operations") {
    I64Column col(3);
    col.Set(0, int64_t{100});
    col.Set(1, int64_t{200});
    col.Set(2, int64_t{300});

    REQUIRE(col.Get(0) == 100);
    REQUIRE(col.Get(1) == 200);
    REQUIRE(col.Get(2) == 300);
  }

  SECTION("F32VecColumn contiguous storage") {
    F32VecColumn col(3, 4);  // 3 rows, 4 dimensions
    REQUIRE(col.Size() == 3);
    REQUIRE(col.Dim() == 4);
    REQUIRE(col.DataSize() == 12);  // 3 * 4

    col.Set(0, {1.0f, 2.0f, 3.0f, 4.0f});
    col.Set(1, {5.0f, 6.0f, 7.0f, 8.0f});
    col.Set(2, {9.0f, 10.0f, 11.0f, 12.0f});

    // Check contiguous data layout
    const float* data = col.Data();
    REQUIRE(data[0] == 1.0f);  // row 0, dim 0
    REQUIRE(data[4] == 5.0f);  // row 1, dim 0
    REQUIRE(data[8] == 9.0f);  // row 2, dim 0

    // Check GetRow returns correct pointer
    REQUIRE(col.GetRow(1)[0] == 5.0f);
    REQUIRE(col.GetRow(1)[3] == 8.0f);
  }

  SECTION("Clone typed column") {
    F32Column col(3);
    col.Set(0, 1.0f);
    col.Set(1, 2.0f);
    col.Set(2, 3.0f);

    auto clone = col.Clone();
    auto* f32_clone = static_cast<F32Column*>(clone.get());

    REQUIRE(f32_clone->Size() == col.Size());
    REQUIRE(f32_clone->Get(1) == 2.0f);

    // Modify original, clone should be unchanged
    col.Set(1, 100.0f);
    REQUIRE(f32_clone->Get(1) == 2.0f);
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

    auto col1 = std::make_shared<F32Column>(3);
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

    auto col = std::make_shared<F32Column>(3);
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

  SECTION("Typed column accessors") {
    ColumnBatch batch(3);

    auto f32_col = std::make_shared<F32Column>(3);
    f32_col->Set(0, 1.0f);
    batch.SetColumn(keys::id::SCORE_BASE, f32_col);

    auto i64_col = std::make_shared<I64Column>(3);
    i64_col->Set(0, int64_t{100});
    batch.SetColumn(keys::id::CAND_CANDIDATE_ID, i64_col);

    REQUIRE(batch.GetF32Column(keys::id::SCORE_BASE) != nullptr);
    REQUIRE(batch.GetI64Column(keys::id::CAND_CANDIDATE_ID) != nullptr);
    REQUIRE(batch.GetF32Column(keys::id::CAND_CANDIDATE_ID) == nullptr);  // Wrong type
  }

  SECTION("UseCount for column sharing") {
    ColumnBatch batch(3);

    auto col = std::make_shared<F32Column>(3);
    REQUIRE(col.use_count() == 1);

    batch.SetColumn(keys::id::SCORE_BASE, col);
    REQUIRE(col.use_count() == 2);  // batch + local var

    REQUIRE(batch.UseCount(keys::id::SCORE_BASE) == 2);
  }
}

TEST_CASE("BatchBuilder COW semantics", "[batch_builder]") {
  // Create a source batch with two columns
  auto id_col = std::make_shared<I64Column>(3);
  id_col->Set(0, int64_t{1});
  id_col->Set(1, int64_t{2});
  id_col->Set(2, int64_t{3});

  auto score_col = std::make_shared<F32Column>(3);
  score_col->Set(0, 0.5f);
  score_col->Set(1, 0.6f);
  score_col->Set(2, 0.7f);

  ColumnBatch source(3);
  source.SetColumn(keys::id::CAND_CANDIDATE_ID, id_col);
  source.SetColumn(keys::id::SCORE_BASE, score_col);

  SECTION("Adding new column shares existing columns") {
    BatchBuilder builder(source);

    // Add a new column
    auto new_col = std::make_shared<F32Column>(3);
    new_col->Set(0, 1.0f);
    new_col->Set(1, 2.0f);
    new_col->Set(2, 3.0f);
    builder.AddF32Column(keys::id::SCORE_FINAL, new_col);

    ColumnBatch result = builder.Build();

    // Original columns should be shared
    REQUIRE(result.GetColumn(keys::id::CAND_CANDIDATE_ID) == id_col);
    REQUIRE(result.GetColumn(keys::id::SCORE_BASE) == score_col);

    // New column should exist
    REQUIRE(result.HasColumn(keys::id::SCORE_FINAL));

    // Verify use counts show sharing
    REQUIRE(id_col.use_count() == 3);
    REQUIRE(score_col.use_count() == 3);
  }

  SECTION("Modifying existing column triggers COW") {
    BatchBuilder builder(source);

    // Modify a value in score column
    builder.Set(1, keys::id::SCORE_BASE, 0.99f);

    ColumnBatch result = builder.Build();

    // ID column should still be shared (not modified)
    REQUIRE(result.GetColumn(keys::id::CAND_CANDIDATE_ID) == id_col);

    // Score column should NOT be shared (it was modified - COW triggered)
    REQUIRE(result.GetColumn(keys::id::SCORE_BASE) != score_col);

    // Verify the original is unchanged
    REQUIRE(score_col->Get(1) == 0.6f);

    // Verify the result has the new value
    auto* result_score_col = result.GetF32Column(keys::id::SCORE_BASE);
    REQUIRE(result_score_col != nullptr);
    REQUIRE(result_score_col->Get(1) == 0.99f);

    // Other values in the COW'd column should be preserved
    REQUIRE(result_score_col->Get(0) == 0.5f);
    REQUIRE(result_score_col->Get(2) == 0.7f);
  }

  SECTION("Multiple modifications to same column share one COW copy") {
    BatchBuilder builder(source);

    builder.Set(0, keys::id::SCORE_BASE, 0.1f);
    builder.Set(1, keys::id::SCORE_BASE, 0.2f);
    builder.Set(2, keys::id::SCORE_BASE, 0.3f);

    ColumnBatch result = builder.Build();

    // Score column should be modified once (COW'd once)
    REQUIRE(result.GetColumn(keys::id::SCORE_BASE) != score_col);

    auto* result_col = result.GetF32Column(keys::id::SCORE_BASE);
    REQUIRE(result_col->Get(0) == 0.1f);
    REQUIRE(result_col->Get(1) == 0.2f);
    REQUIRE(result_col->Get(2) == 0.3f);
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
