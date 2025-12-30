#include <catch2/catch_test_macros.hpp>

#include "object/row_view.h"
#include "object/column_batch.h"
#include "object/batch_builder.h"
#include "keys/registry.h"
#include "keys.h"

using namespace ranking_dsl;

TEST_CASE("RowView read operations", "[row_view]") {
  // Create a batch with test data
  auto id_col = std::make_shared<Column>(3);
  id_col->Set(0, int64_t{100});
  id_col->Set(1, int64_t{200});
  id_col->Set(2, int64_t{300});

  auto score_col = std::make_shared<Column>(3);
  score_col->Set(0, 0.5f);
  score_col->Set(1, 0.6f);
  score_col->Set(2, 0.7f);

  ColumnBatch batch(3);
  batch.SetColumn(keys::id::CAND_CANDIDATE_ID, id_col);
  batch.SetColumn(keys::id::SCORE_BASE, score_col);

  SECTION("Read-only RowView") {
    RowView view(&batch, 1);

    REQUIRE(view.IsValid());
    REQUIRE_FALSE(view.IsWritable());
    REQUIRE(view.RowIndex() == 1);

    auto id_opt = view.Get(keys::id::CAND_CANDIDATE_ID);
    REQUIRE(id_opt.has_value());
    auto* id = std::get_if<int64_t>(&*id_opt);
    REQUIRE(id != nullptr);
    REQUIRE(*id == 200);

    auto score_opt = view.Get(keys::id::SCORE_BASE);
    REQUIRE(score_opt.has_value());
    auto* score = std::get_if<float>(&*score_opt);
    REQUIRE(score != nullptr);
    REQUIRE(*score == 0.6f);
  }

  SECTION("Get non-existent key returns nullopt") {
    RowView view(&batch, 0);
    auto missing = view.Get(keys::id::SCORE_FINAL);
    REQUIRE_FALSE(missing.has_value());
  }

  SECTION("Has returns correct values") {
    RowView view(&batch, 0);
    REQUIRE(view.Has(keys::id::CAND_CANDIDATE_ID));
    REQUIRE(view.Has(keys::id::SCORE_BASE));
    REQUIRE_FALSE(view.Has(keys::id::SCORE_FINAL));
  }

  SECTION("Keys returns all column keys") {
    RowView view(&batch, 0);
    auto keys = view.Keys();
    REQUIRE(keys.size() == 2);
    // Order may vary, just check both are present
    bool has_id = false, has_score = false;
    for (int32_t k : keys) {
      if (k == keys::id::CAND_CANDIDATE_ID) has_id = true;
      if (k == keys::id::SCORE_BASE) has_score = true;
    }
    REQUIRE(has_id);
    REQUIRE(has_score);
  }

  SECTION("Invalid RowView") {
    RowView view;
    REQUIRE_FALSE(view.IsValid());
    REQUIRE_FALSE(view.IsWritable());
    REQUIRE_FALSE(view.Get(keys::id::SCORE_BASE).has_value());
    REQUIRE_FALSE(view.Has(keys::id::SCORE_BASE));
  }
}

TEST_CASE("RowView write operations", "[row_view]") {
  auto score_col = std::make_shared<Column>(3);
  score_col->Set(0, 0.5f);
  score_col->Set(1, 0.6f);
  score_col->Set(2, 0.7f);

  ColumnBatch batch(3);
  batch.SetColumn(keys::id::SCORE_BASE, score_col);

  SECTION("Set requires builder") {
    RowView view(&batch, 0);
    REQUIRE_THROWS(view.Set(keys::id::SCORE_FINAL, 1.0f));
  }

  SECTION("Set with builder succeeds") {
    BatchBuilder builder(batch);
    RowView view(&batch, 1, &builder);

    REQUIRE(view.IsWritable());

    // Set returns new RowView
    RowView new_view = view.Set(keys::id::SCORE_FINAL, 0.99f);
    REQUIRE(new_view.IsValid());
    REQUIRE(new_view.RowIndex() == 1);

    // Build the batch to materialize changes
    ColumnBatch result = builder.Build();

    // Check the result has the new value
    auto final_col = result.GetColumn(keys::id::SCORE_FINAL);
    REQUIRE(final_col != nullptr);
    auto* val = std::get_if<float>(&final_col->Get(1));
    REQUIRE(val != nullptr);
    REQUIRE(*val == 0.99f);
  }

  SECTION("Set with type enforcement") {
    KeyRegistry registry;
    registry.LoadFromCompiled();

    BatchBuilder builder(batch);
    RowView view(&batch, 0, &builder);

    // Setting correct type should work
    REQUIRE_NOTHROW(view.Set(keys::id::SCORE_FINAL, 0.5f, &registry));

    // Setting wrong type should throw
    REQUIRE_THROWS(view.Set(keys::id::SCORE_FINAL, std::string("wrong"), &registry));
  }
}
