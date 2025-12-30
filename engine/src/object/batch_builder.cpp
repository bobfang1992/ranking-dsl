#include "object/batch_builder.h"

#include <stdexcept>

#include "keys/registry.h"

namespace ranking_dsl {

namespace {

// Map KeyType to ValueType (they have different enum values!)
ValueType KeyTypeToValueType(keys::KeyType kt) {
  switch (kt) {
    case keys::KeyType::Bool:   return ValueType::Bool;
    case keys::KeyType::I64:    return ValueType::I64;
    case keys::KeyType::F32:    return ValueType::F32;
    case keys::KeyType::String: return ValueType::String;
    case keys::KeyType::Bytes:  return ValueType::Bytes;
    case keys::KeyType::F32Vec: return ValueType::F32Vec;
    default: return ValueType::Null;
  }
}

}  // namespace

BatchBuilder::BatchBuilder(const ColumnBatch& source)
    : source_(&source), row_count_(source.RowCount()) {}

BatchBuilder::BatchBuilder(size_t row_count)
    : source_(nullptr), row_count_(row_count) {}

void BatchBuilder::Set(size_t row_index, int32_t key_id, Value value,
                       const KeyRegistry* registry) {
  if (row_index >= row_count_) {
    throw std::out_of_range("Row index out of bounds: " +
                            std::to_string(row_index) + " >= " +
                            std::to_string(row_count_));
  }

  // Type validation
  if (registry && !IsNull(value)) {
    auto* key_info = registry->GetById(key_id);
    if (key_info) {
      ValueType expected = KeyTypeToValueType(key_info->type);
      ValueType actual = GetValueType(value);
      if (actual != expected) {
        throw std::runtime_error(
            "Type mismatch for key " + std::to_string(key_id) +
            ": expected " + std::to_string(static_cast<int>(expected)) +
            ", got " + std::to_string(static_cast<int>(actual)));
      }
    } else {
      throw std::runtime_error("Unknown key: " + std::to_string(key_id));
    }
  }

  Column& col = EnsureWritable(key_id);
  col.Set(row_index, std::move(value));
}

void BatchBuilder::AddColumn(int32_t key_id, ColumnPtr column) {
  // Directly add to modified columns, converting from ColumnPtr
  modified_columns_[key_id] = column->Clone();
  modified_keys_.insert(key_id);
}

Column& BatchBuilder::EnsureWritable(int32_t key_id) {
  // Check if already modified
  auto it = modified_columns_.find(key_id);
  if (it != modified_columns_.end()) {
    return it->second;
  }

  // Need to create or copy
  Column col;
  if (source_ && source_->HasColumn(key_id)) {
    // COW: copy from source
    col = source_->GetColumn(key_id)->Clone();
  } else {
    // New column: fill with nulls
    col = Column(row_count_);
  }

  modified_keys_.insert(key_id);
  auto [inserted_it, _] = modified_columns_.emplace(key_id, std::move(col));
  return inserted_it->second;
}

bool BatchBuilder::IsModified(int32_t key_id) const {
  return modified_keys_.find(key_id) != modified_keys_.end();
}

ColumnBatch BatchBuilder::Build() {
  ColumnBatch::ColumnMap result_columns;

  // Copy shared columns from source (unchanged columns)
  if (source_) {
    for (const auto& [key_id, col_ptr] : source_->Columns()) {
      if (!IsModified(key_id)) {
        // Share the column - just copy the shared_ptr
        result_columns[key_id] = col_ptr;
      }
    }
  }

  // Add modified columns (newly allocated)
  for (auto& [key_id, col] : modified_columns_) {
    result_columns[key_id] = std::make_shared<Column>(std::move(col));
  }

  return ColumnBatch(row_count_, std::move(result_columns));
}

}  // namespace ranking_dsl
