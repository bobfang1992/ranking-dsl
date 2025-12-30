#include "object/column.h"

#include <stdexcept>

namespace ranking_dsl {

Column::Column(size_t row_count) : values_(row_count, MakeNull()) {}

Column::Column(Storage values) : values_(std::move(values)) {}

const Value& Column::Get(size_t row_index) const {
  if (row_index >= values_.size()) {
    static const Value null_value = MakeNull();
    return null_value;
  }
  return values_[row_index];
}

void Column::Set(size_t row_index, Value value) {
  if (row_index >= values_.size()) {
    Resize(row_index + 1);
  }
  values_[row_index] = std::move(value);
}

void Column::Resize(size_t new_size) {
  if (new_size > values_.size()) {
    values_.resize(new_size, MakeNull());
  }
}

Column Column::Clone() const {
  return Column(values_);
}

}  // namespace ranking_dsl
