#include "nodes/js/batch_context.h"

#include <stdexcept>

namespace ranking_dsl {

BatchContext::BatchContext(const ColumnBatch& batch,
                           BatchBuilder& builder,
                           const KeyRegistry* registry,
                           const std::set<int32_t>& allowed_writes,
                           NjsBudget& budget)
    : batch_(batch),
      builder_(builder),
      registry_(registry),
      allowed_writes_(allowed_writes),
      budget_(budget) {}

std::pair<const float*, size_t> BatchContext::GetF32Raw(int32_t key_id) const {
  auto* col = batch_.GetF32Column(key_id);
  if (!col) {
    return {nullptr, 0};
  }
  return {col->Data(), col->Size()};
}

std::vector<float> BatchContext::GetF32(int32_t key_id) const {
  auto [data, size] = GetF32Raw(key_id);
  if (data) {
    return std::vector<float>(data, data + size);
  }
  // Return zeros for missing column
  return std::vector<float>(batch_.RowCount(), 0.0f);
}

F32VecView BatchContext::GetF32VecRaw(int32_t key_id) const {
  auto* col = batch_.GetF32VecColumn(key_id);
  if (!col) {
    return {nullptr, 0, 0, 0};
  }
  return {
    col->Data(),
    col->DataSize(),
    col->Dim(),
    col->Size()
  };
}

std::vector<std::vector<float>> BatchContext::GetF32Vec(int32_t key_id) const {
  std::vector<std::vector<float>> result(batch_.RowCount());
  auto* col = batch_.GetF32VecColumn(key_id);
  if (!col) return result;

  for (size_t i = 0; i < col->Size(); ++i) {
    if (!col->IsNull(i)) {
      result[i] = col->Get(i);
    }
  }
  return result;
}

std::pair<const int64_t*, size_t> BatchContext::GetI64Raw(int32_t key_id) const {
  auto* col = batch_.GetI64Column(key_id);
  if (!col) {
    return {nullptr, 0};
  }
  return {col->Data(), col->Size()};
}

std::vector<int64_t> BatchContext::GetI64(int32_t key_id) const {
  auto [data, size] = GetI64Raw(key_id);
  if (data) {
    return std::vector<int64_t>(data, data + size);
  }
  return std::vector<int64_t>(batch_.RowCount(), 0);
}

void BatchContext::CheckWriteAllowed(int32_t key_id, keys::KeyType expected_type) {
  // Check meta.writes
  if (allowed_writes_.find(key_id) == allowed_writes_.end()) {
    throw std::runtime_error("Write to key " + std::to_string(key_id) +
                             " not allowed - not in meta.writes");
  }

  // Check type via registry
  if (registry_) {
    auto* key_info = registry_->GetById(key_id);
    if (!key_info) {
      throw std::runtime_error("Unknown key: " + std::to_string(key_id));
    }
    if (key_info->type != expected_type) {
      throw std::runtime_error("Type mismatch for key " + std::to_string(key_id) +
                               ": expected " + std::to_string(static_cast<int>(expected_type)) +
                               ", got " + std::to_string(static_cast<int>(key_info->type)));
    }
  }
}

void BatchContext::CheckBudget(int64_t bytes, int64_t cells) {
  if (budget_.bytes_written + bytes > budget_.max_write_bytes) {
    throw std::runtime_error("Budget exceeded: max_write_bytes (" +
                             std::to_string(budget_.max_write_bytes) + ")");
  }
  if (budget_.cells_written + cells > budget_.max_write_cells) {
    throw std::runtime_error("Budget exceeded: max_write_cells (" +
                             std::to_string(budget_.max_write_cells) + ")");
  }
  budget_.bytes_written += bytes;
  budget_.cells_written += cells;
}

float* BatchContext::AllocateF32(int32_t key_id) {
  CheckWriteAllowed(key_id, keys::KeyType::F32);
  CheckBudget(batch_.RowCount() * sizeof(float), batch_.RowCount());

  auto col = std::make_shared<F32Column>(batch_.RowCount());
  float* data = col->Data();

  allocated_columns_.push_back({key_id, col});
  return data;
}

float* BatchContext::AllocateF32Vec(int32_t key_id, size_t dim) {
  CheckWriteAllowed(key_id, keys::KeyType::F32Vec);
  CheckBudget(batch_.RowCount() * dim * sizeof(float), batch_.RowCount());

  auto col = std::make_shared<F32VecColumn>(batch_.RowCount(), dim);
  float* data = col->Data();

  allocated_columns_.push_back({key_id, col});
  return data;
}

int64_t* BatchContext::AllocateI64(int32_t key_id) {
  CheckWriteAllowed(key_id, keys::KeyType::I64);
  CheckBudget(batch_.RowCount() * sizeof(int64_t), batch_.RowCount());

  auto col = std::make_shared<I64Column>(batch_.RowCount());
  int64_t* data = col->Data();

  allocated_columns_.push_back({key_id, col});
  return data;
}

void BatchContext::Commit() {
  for (auto& alloc : allocated_columns_) {
    builder_.AddColumn(alloc.key_id, alloc.column);
  }
  allocated_columns_.clear();
}

}  // namespace ranking_dsl
