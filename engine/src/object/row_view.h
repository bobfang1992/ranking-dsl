#pragma once

#include <cstddef>
#include <cstdint>
#include <optional>
#include <vector>

#include "object/batch_builder.h"
#include "object/column_batch.h"
#include "object/value.h"

namespace ranking_dsl {

class KeyRegistry;

/**
 * RowView - a view into a single row of a ColumnBatch.
 *
 * Provides Obj-like semantics:
 * - Get(key_id) returns the value at this row
 * - Set(key_id, value) writes via BatchBuilder, returns a new RowView
 *
 * The immutability semantics of the old Obj are preserved:
 * - The original RowView is unchanged after Set()
 * - The new RowView refers to the same batch but writes go through the builder
 *
 * Note: All RowViews from the same BatchBuilder share the builder. When the
 * builder calls Build(), it produces a new ColumnBatch with the changes.
 */
class RowView {
 public:
  /**
   * Create an invalid/empty RowView.
   */
  RowView() = default;

  /**
   * Create a read-only RowView (no builder - Set() will fail).
   */
  RowView(const ColumnBatch* batch, size_t row_index);

  /**
   * Create a writable RowView with a builder.
   */
  RowView(const ColumnBatch* batch, size_t row_index, BatchBuilder* builder);

  /**
   * Get a value by key_id.
   * Returns std::nullopt if the key is not present.
   */
  std::optional<Value> Get(int32_t key_id) const;

  /**
   * Set a value, returning a new RowView.
   *
   * If this RowView has no builder, throws an error.
   * Otherwise, writes through the builder (COW semantics).
   *
   * The returned RowView has the same batch and row_index, and shares
   * the same builder. The original RowView is unchanged in terms of
   * what it "sees" until the batch is rebuilt.
   *
   * If registry is provided, validates the value type.
   */
  RowView Set(int32_t key_id, Value value,
              const KeyRegistry* registry = nullptr) const;

  /**
   * Check if a key is present.
   */
  bool Has(int32_t key_id) const;

  /**
   * Get the row index.
   */
  size_t RowIndex() const { return row_index_; }

  /**
   * Check if this is a valid view (has a batch).
   */
  bool IsValid() const { return batch_ != nullptr; }

  /**
   * Check if this view is writable (has a builder).
   */
  bool IsWritable() const { return builder_ != nullptr; }

  /**
   * Get all key IDs present in this row's batch.
   * Note: Returns all column keys, not just keys with non-null values.
   */
  std::vector<int32_t> Keys() const;

  /**
   * Get the underlying batch.
   */
  const ColumnBatch* Batch() const { return batch_; }

  /**
   * Get the builder (may be null).
   */
  BatchBuilder* Builder() const { return builder_; }

 private:
  const ColumnBatch* batch_ = nullptr;
  size_t row_index_ = 0;
  BatchBuilder* builder_ = nullptr;  // May be null for read-only views
};

}  // namespace ranking_dsl
