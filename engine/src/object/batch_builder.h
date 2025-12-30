#pragma once

#include <cstddef>
#include <cstdint>
#include <unordered_map>
#include <unordered_set>

#include "object/column_batch.h"
#include "object/value.h"

namespace ranking_dsl {

class KeyRegistry;

/**
 * BatchBuilder - builds a new ColumnBatch with copy-on-write semantics.
 *
 * Given a source batch, modifications via Set() are accumulated in newly
 * allocated columns. Unchanged columns from the source are shared.
 *
 * Usage:
 *   BatchBuilder builder(source_batch);
 *   builder.Set(row_idx, key_id, value);  // COW: copies column on first write
 *   ColumnBatch result = builder.Build(); // Shares unchanged columns
 */
class BatchBuilder {
 public:
  /**
   * Create a builder from a source batch.
   * The source is not modified.
   */
  explicit BatchBuilder(const ColumnBatch& source);

  /**
   * Create a builder for a new batch with n rows.
   */
  explicit BatchBuilder(size_t row_count);

  /**
   * Set a value at (row_index, key_id).
   *
   * If this is the first write to this column:
   * - If column exists in source, copy it (COW)
   * - Otherwise, create a new column filled with nulls
   *
   * If registry is provided, validates the value type.
   */
  void Set(size_t row_index, int32_t key_id, Value value,
           const KeyRegistry* registry = nullptr);

  /**
   * Add a new column (no COW needed - this is a new key).
   * The column must have at least row_count values.
   */
  void AddColumn(int32_t key_id, ColumnPtr column);

  /**
   * Build the final batch.
   *
   * Unchanged columns from source are shared (same shared_ptr).
   * Modified columns are newly allocated.
   */
  ColumnBatch Build();

  /**
   * Get the row count.
   */
  size_t RowCount() const { return row_count_; }

  /**
   * Check if a column has been modified.
   */
  bool IsModified(int32_t key_id) const;

 private:
  /**
   * Ensure we have a writable column for key_id.
   * Copies from source if needed (COW).
   */
  Column& EnsureWritable(int32_t key_id);

  const ColumnBatch* source_ = nullptr;  // May be null for new batches
  size_t row_count_ = 0;

  // Columns that have been modified (owned by builder)
  std::unordered_map<int32_t, Column> modified_columns_;

  // Keys that have been modified (for tracking)
  std::unordered_set<int32_t> modified_keys_;
};

}  // namespace ranking_dsl
