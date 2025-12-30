#pragma once

#include <cstddef>
#include <cstdint>
#include <memory>
#include <unordered_map>
#include <vector>

#include "object/typed_column.h"
#include "object/value.h"

namespace ranking_dsl {

/**
 * ColumnBatch - columnar storage for candidate objects.
 *
 * Stores data in Structure-of-Arrays (SoA) format with typed columns:
 * - Each column has contiguous typed storage (F32Column, I64Column, etc.)
 * - Columns are keyed by key_id from the registry
 * - Columns can be shared between batches (copy-on-write via shared_ptr)
 *
 * This layout enables:
 * - Cache-efficient iteration over columns
 * - Zero-copy Float32Array views for JS integration
 * - Efficient vectorized operations
 */
class ColumnBatch {
 public:
  using ColumnMap = std::unordered_map<int32_t, TypedColumnPtr>;

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
   * Get a column by key_id (generic typed column).
   * Returns nullptr if not present.
   */
  TypedColumnPtr GetColumn(int32_t key_id) const;

  /**
   * Get typed column accessors (fast path, returns nullptr if wrong type).
   */
  F32Column* GetF32Column(int32_t key_id) const;
  I64Column* GetI64Column(int32_t key_id) const;
  BoolColumn* GetBoolColumn(int32_t key_id) const;
  StringColumn* GetStringColumn(int32_t key_id) const;
  F32VecColumn* GetF32VecColumn(int32_t key_id) const;
  BytesColumn* GetBytesColumn(int32_t key_id) const;

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
   */
  void SetColumn(int32_t key_id, TypedColumnPtr column);

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
