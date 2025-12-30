#include "object/column_batch.h"

namespace ranking_dsl {

ColumnBatch::ColumnBatch(size_t row_count) : row_count_(row_count) {}

ColumnBatch::ColumnBatch(size_t row_count, ColumnMap columns)
    : row_count_(row_count), columns_(std::move(columns)) {}

bool ColumnBatch::HasColumn(int32_t key_id) const {
  return columns_.find(key_id) != columns_.end();
}

TypedColumnPtr ColumnBatch::GetColumn(int32_t key_id) const {
  auto it = columns_.find(key_id);
  if (it == columns_.end()) {
    return nullptr;
  }
  return it->second;
}

F32Column* ColumnBatch::GetF32Column(int32_t key_id) const {
  auto col = GetColumn(key_id);
  if (!col || col->Type() != ColumnType::F32) {
    return nullptr;
  }
  return static_cast<F32Column*>(col.get());
}

I64Column* ColumnBatch::GetI64Column(int32_t key_id) const {
  auto col = GetColumn(key_id);
  if (!col || col->Type() != ColumnType::I64) {
    return nullptr;
  }
  return static_cast<I64Column*>(col.get());
}

BoolColumn* ColumnBatch::GetBoolColumn(int32_t key_id) const {
  auto col = GetColumn(key_id);
  if (!col || col->Type() != ColumnType::Bool) {
    return nullptr;
  }
  return static_cast<BoolColumn*>(col.get());
}

StringColumn* ColumnBatch::GetStringColumn(int32_t key_id) const {
  auto col = GetColumn(key_id);
  if (!col || col->Type() != ColumnType::String) {
    return nullptr;
  }
  return static_cast<StringColumn*>(col.get());
}

F32VecColumn* ColumnBatch::GetF32VecColumn(int32_t key_id) const {
  auto col = GetColumn(key_id);
  if (!col || col->Type() != ColumnType::F32Vec) {
    return nullptr;
  }
  return static_cast<F32VecColumn*>(col.get());
}

BytesColumn* ColumnBatch::GetBytesColumn(int32_t key_id) const {
  auto col = GetColumn(key_id);
  if (!col || col->Type() != ColumnType::Bytes) {
    return nullptr;
  }
  return static_cast<BytesColumn*>(col.get());
}

Value ColumnBatch::GetValue(size_t row_index, int32_t key_id) const {
  auto col = GetColumn(key_id);
  if (!col || row_index >= row_count_) {
    return MakeNull();
  }
  return col->GetValue(row_index);
}

std::vector<int32_t> ColumnBatch::ColumnKeys() const {
  std::vector<int32_t> keys;
  keys.reserve(columns_.size());
  for (const auto& [key_id, _] : columns_) {
    keys.push_back(key_id);
  }
  return keys;
}

void ColumnBatch::SetColumn(int32_t key_id, TypedColumnPtr column) {
  columns_[key_id] = std::move(column);
}

long ColumnBatch::UseCount(int32_t key_id) const {
  auto it = columns_.find(key_id);
  if (it == columns_.end()) {
    return 0;
  }
  return it->second.use_count();
}

}  // namespace ranking_dsl
