#pragma once

#include <cstddef>
#include <cstdint>
#include <memory>
#include <unordered_map>
#include <vector>

#include "object/column.h"
#include "object/value.h"

namespace ranking_dsl {

/**
 * ColumnBatch - columnar storage for candidate objects.
 *
 * Stores data in Structure-of-Arrays (SoA) format:
 * - Each column is a vector of values, one per row
 * - Columns are keyed by key_id from the registry
 * - Columns can be shared between batches (copy-on-write via shared_ptr)
 *
 * This layout is cache-efficient for operations that iterate over
 * a single key across all candidates (e.g., score_formula).
 */
class ColumnBatch {
 public:
  using ColumnMap = std::unordered_map<int32_t, ColumnPtr>;

  /**
   * Create an empty batch with 0 rows.
   */
  ColumnBatch() = default;

  /**
   * Create a batch with n rows (no columns yet).
   */
  explicit ColumnBatch(size_t row_count);

  /**
   * Create a batch from existing columns.
   */
  ColumnBatch(size_t row_count, ColumnMap columns);

  /**
   * Get the number of rows in this batch.
   */
  size_t RowCount() const { return row_count_; }

  /**
   * Get the number of columns.
   */
  size_t ColumnCount() const { return columns_.size(); }

  /**
   * Check if a column exists.
   */
  bool HasColumn(int32_t key_id) const;

  /**
   * Get a column by key_id.
   * Returns nullptr if not present.
   */
  ColumnPtr GetColumn(int32_t key_id) const;

  /**
   * Get a value at (row_index, key_id).
   * Returns null if column doesn't exist or row is out of bounds.
   */
  Value GetValue(size_t row_index, int32_t key_id) const;

  /**
   * Get all column key IDs.
   */
  std::vector<int32_t> ColumnKeys() const;

  /**
   * Get the underlying column map (for iteration/inspection).
   */
  const ColumnMap& Columns() const { return columns_; }

  /**
   * Get mutable access to columns (for BatchBuilder).
   * Use with care - this bypasses COW semantics.
   */
  ColumnMap& MutableColumns() { return columns_; }

  /**
   * Set the row count.
   */
  void SetRowCount(size_t row_count) { row_count_ = row_count; }

  /**
   * Add or replace a column.
   * The column must have at least row_count_ values.
   */
  void SetColumn(int32_t key_id, ColumnPtr column);

  /**
   * Get the reference count for a column (for testing COW).
   * Returns 0 if column doesn't exist.
   */
  long UseCount(int32_t key_id) const;

 private:
  size_t row_count_ = 0;
  ColumnMap columns_;
};

}  // namespace ranking_dsl
