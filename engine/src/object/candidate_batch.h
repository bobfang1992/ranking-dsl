#pragma once

#include "object/column_batch.h"
#include "object/row_view.h"

namespace ranking_dsl {

/**
 * CandidateBatch is now an alias for ColumnBatch.
 *
 * The columnar layout stores data as Map<KeyId, Column> where each
 * Column is a vector<Value>, one value per row.
 *
 * For row-oriented access (e.g., in njs nodes), use RowView:
 *   RowView view(&batch, row_index, &builder);
 *   auto val = view.Get(key_id);
 *   view = view.Set(key_id, new_value);
 */
using CandidateBatch = ColumnBatch;

}  // namespace ranking_dsl
