#pragma once

#include <cstddef>
#include <memory>
#include <vector>

#include "object/value.h"

namespace ranking_dsl {

/**
 * Column - a vector of Values, one per row.
 *
 * This is the basic building block for columnar storage.
 * All values in a column should be of the same type (enforced at set time).
 */
class Column {
 public:
  using Storage = std::vector<Value>;

  /**
   * Create an empty column.
   */
  Column() = default;

  /**
   * Create a column with n rows, all initialized to null.
   */
  explicit Column(size_t row_count);

  /**
   * Create a column from existing values.
   */
  explicit Column(Storage values);

  /**
   * Get the number of rows.
   */
  size_t Size() const { return values_.size(); }

  /**
   * Get value at row index.
   */
  const Value& Get(size_t row_index) const;

  /**
   * Set value at row index.
   * Resizes if necessary (fills with null).
   */
  void Set(size_t row_index, Value value);

  /**
   * Get the underlying storage (for iteration).
   */
  const Storage& Values() const { return values_; }

  /**
   * Get mutable access (for batch operations).
   */
  Storage& MutableValues() { return values_; }

  /**
   * Resize the column, filling new slots with null.
   */
  void Resize(size_t new_size);

  /**
   * Clone this column.
   */
  Column Clone() const;

 private:
  Storage values_;
};

/**
 * Shared pointer to a column (for COW sharing).
 */
using ColumnPtr = std::shared_ptr<Column>;

/**
 * Create a new column with n null values.
 */
inline ColumnPtr MakeColumn(size_t row_count) {
  return std::make_shared<Column>(row_count);
}

/**
 * Create a column from existing values.
 */
inline ColumnPtr MakeColumn(Column::Storage values) {
  return std::make_shared<Column>(std::move(values));
}

}  // namespace ranking_dsl
