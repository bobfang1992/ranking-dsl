#include "object/batch_builder.h"

#include <stdexcept>

#include "keys/registry.h"

namespace ranking_dsl {

namespace {

// Map KeyType to ColumnType
ColumnType KeyTypeToColumnType(keys::KeyType kt) {
  switch (kt) {
    case keys::KeyType::Bool:   return ColumnType::Bool;
    case keys::KeyType::I64:    return ColumnType::I64;
    case keys::KeyType::F32:    return ColumnType::F32;
    case keys::KeyType::String: return ColumnType::String;
    case keys::KeyType::Bytes:  return ColumnType::Bytes;
    case keys::KeyType::F32Vec: return ColumnType::F32Vec;
    default: return ColumnType::Null;
  }
}

// Infer column type from a Value
ColumnType InferColumnType(const Value& value) {
  if (std::holds_alternative<float>(value)) return ColumnType::F32;
  if (std::holds_alternative<int64_t>(value)) return ColumnType::I64;
  if (std::holds_alternative<bool>(value)) return ColumnType::Bool;
  if (std::holds_alternative<std::string>(value)) return ColumnType::String;
  if (std::holds_alternative<std::vector<float>>(value)) return ColumnType::F32Vec;
  if (std::holds_alternative<std::vector<uint8_t>>(value)) return ColumnType::Bytes;
  return ColumnType::Null;
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

  // Determine column type
  ColumnType col_type = ColumnType::Null;

  // Type validation via registry
  if (registry && !ranking_dsl::IsNull(value)) {
    auto* key_info = registry->GetById(key_id);
    if (key_info) {
      col_type = KeyTypeToColumnType(key_info->type);
      ColumnType actual_type = InferColumnType(value);
      if (actual_type != col_type) {
        throw std::runtime_error(
            "Type mismatch for key " + std::to_string(key_id) +
            ": expected " + std::to_string(static_cast<int>(col_type)) +
            ", got " + std::to_string(static_cast<int>(actual_type)));
      }
    } else {
      throw std::runtime_error("Unknown key: " + std::to_string(key_id));
    }
  } else {
    // Infer type from value
    col_type = InferColumnType(value);
  }

  TypedColumnPtr col = EnsureWritable(key_id, col_type);
  col->SetValue(row_index, value);
}

void BatchBuilder::AddColumn(int32_t key_id, TypedColumnPtr column) {
  modified_columns_[key_id] = std::move(column);
  modified_keys_.insert(key_id);
}

void BatchBuilder::AddF32Column(int32_t key_id, std::shared_ptr<F32Column> column) {
  AddColumn(key_id, std::move(column));
}

void BatchBuilder::AddI64Column(int32_t key_id, std::shared_ptr<I64Column> column) {
  AddColumn(key_id, std::move(column));
}

void BatchBuilder::AddF32VecColumn(int32_t key_id, std::shared_ptr<F32VecColumn> column) {
  AddColumn(key_id, std::move(column));
}

TypedColumnPtr BatchBuilder::EnsureWritable(int32_t key_id, ColumnType type_hint) {
  // Check if already modified
  auto it = modified_columns_.find(key_id);
  if (it != modified_columns_.end()) {
    return it->second;
  }

  // Need to create or copy
  TypedColumnPtr col;
  if (source_ && source_->HasColumn(key_id)) {
    // COW: clone from source
    col = source_->GetColumn(key_id)->Clone();
  } else {
    // New column: create with appropriate type
    if (type_hint == ColumnType::Null) {
      // Default to F32 if no hint
      type_hint = ColumnType::F32;
    }
    col = MakeTypedColumn(type_hint, row_count_);
  }

  modified_keys_.insert(key_id);
  modified_columns_[key_id] = col;
  return col;
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

  // Add modified columns
  for (auto& [key_id, col] : modified_columns_) {
    result_columns[key_id] = std::move(col);
  }

  return ColumnBatch(row_count_, std::move(result_columns));
}

}  // namespace ranking_dsl
