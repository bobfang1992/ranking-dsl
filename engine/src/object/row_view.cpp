#include "object/row_view.h"

#include <stdexcept>

#include "keys/registry.h"

namespace ranking_dsl {

RowView::RowView(const ColumnBatch* batch, size_t row_index)
    : batch_(batch), row_index_(row_index), builder_(nullptr) {}

RowView::RowView(const ColumnBatch* batch, size_t row_index,
                 BatchBuilder* builder)
    : batch_(batch), row_index_(row_index), builder_(builder) {}

std::optional<Value> RowView::Get(int32_t key_id) const {
  if (!batch_) {
    return std::nullopt;
  }

  // First check if the builder has modified this column
  if (builder_ && builder_->IsModified(key_id)) {
    // The builder has this column - but we can't easily access it yet
    // since modified columns are internal. For now, reads still go
    // through the original batch. The changes will be visible after Build().
    //
    // Note: This is a design decision. An alternative would be to have
    // the builder expose a GetValue() method for reading modified values.
    // For MVP, reads see the original batch until Build() is called.
  }

  Value val = batch_->GetValue(row_index_, key_id);
  if (IsNull(val)) {
    return std::nullopt;
  }
  return val;
}

RowView RowView::Set(int32_t key_id, Value value,
                     const KeyRegistry* registry) const {
  if (!builder_) {
    throw std::runtime_error("RowView is read-only (no builder)");
  }

  builder_->Set(row_index_, key_id, std::move(value), registry);

  // Return a new RowView with the same batch, row_index, and builder
  return RowView(batch_, row_index_, builder_);
}

bool RowView::Has(int32_t key_id) const {
  if (!batch_) {
    return false;
  }
  return batch_->HasColumn(key_id);
}

std::vector<int32_t> RowView::Keys() const {
  if (!batch_) {
    return {};
  }
  return batch_->ColumnKeys();
}

}  // namespace ranking_dsl
