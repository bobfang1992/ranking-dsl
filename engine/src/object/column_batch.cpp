#include "object/column_batch.h"

namespace ranking_dsl {

ColumnBatch::ColumnBatch(size_t row_count) : row_count_(row_count) {}

ColumnBatch::ColumnBatch(size_t row_count, ColumnMap columns)
    : row_count_(row_count), columns_(std::move(columns)) {}

bool ColumnBatch::HasColumn(int32_t key_id) const {
  return columns_.find(key_id) != columns_.end();
}

ColumnPtr ColumnBatch::GetColumn(int32_t key_id) const {
  auto it = columns_.find(key_id);
  if (it == columns_.end()) {
    return nullptr;
  }
  return it->second;
}

Value ColumnBatch::GetValue(size_t row_index, int32_t key_id) const {
  auto col = GetColumn(key_id);
  if (!col || row_index >= row_count_) {
    return MakeNull();
  }
  return col->Get(row_index);
}

std::vector<int32_t> ColumnBatch::ColumnKeys() const {
  std::vector<int32_t> keys;
  keys.reserve(columns_.size());
  for (const auto& [key_id, _] : columns_) {
    keys.push_back(key_id);
  }
  return keys;
}

void ColumnBatch::SetColumn(int32_t key_id, ColumnPtr column) {
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
