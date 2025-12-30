#include "object/typed_column.h"

#include <stdexcept>

namespace ranking_dsl {

// F32Column implementation

F32Column::F32Column(size_t row_count)
    : data_(row_count, 0.0f), null_mask_(row_count, true) {}

F32Column::F32Column(std::vector<float> data, std::vector<bool> null_mask)
    : data_(std::move(data)), null_mask_(std::move(null_mask)) {
  if (null_mask_.size() != data_.size()) {
    null_mask_.resize(data_.size(), false);
  }
}

Value F32Column::GetValue(size_t row_index) const {
  if (row_index >= data_.size() || null_mask_[row_index]) {
    return NullValue{};
  }
  return data_[row_index];
}

void F32Column::SetValue(size_t row_index, const Value& value) {
  if (row_index >= data_.size()) {
    throw std::out_of_range("Row index out of bounds");
  }
  if (auto* f = std::get_if<float>(&value)) {
    data_[row_index] = *f;
    null_mask_[row_index] = false;
  } else if (std::holds_alternative<NullValue>(value)) {
    null_mask_[row_index] = true;
  } else {
    throw std::runtime_error("Type mismatch: expected float");
  }
}

std::shared_ptr<TypedColumn> F32Column::Clone() const {
  return std::make_shared<F32Column>(data_, null_mask_);
}

bool F32Column::IsNull(size_t row_index) const {
  return row_index >= data_.size() || null_mask_[row_index];
}

void F32Column::SetNull(size_t row_index) {
  if (row_index < null_mask_.size()) {
    null_mask_[row_index] = true;
  }
}

void F32Column::Set(size_t row_index, float value) {
  if (row_index >= data_.size()) {
    throw std::out_of_range("Row index out of bounds");
  }
  data_[row_index] = value;
  null_mask_[row_index] = false;
}

// I64Column implementation

I64Column::I64Column(size_t row_count)
    : data_(row_count, 0), null_mask_(row_count, true) {}

I64Column::I64Column(std::vector<int64_t> data, std::vector<bool> null_mask)
    : data_(std::move(data)), null_mask_(std::move(null_mask)) {
  if (null_mask_.size() != data_.size()) {
    null_mask_.resize(data_.size(), false);
  }
}

Value I64Column::GetValue(size_t row_index) const {
  if (row_index >= data_.size() || null_mask_[row_index]) {
    return NullValue{};
  }
  return data_[row_index];
}

void I64Column::SetValue(size_t row_index, const Value& value) {
  if (row_index >= data_.size()) {
    throw std::out_of_range("Row index out of bounds");
  }
  if (auto* n = std::get_if<int64_t>(&value)) {
    data_[row_index] = *n;
    null_mask_[row_index] = false;
  } else if (std::holds_alternative<NullValue>(value)) {
    null_mask_[row_index] = true;
  } else {
    throw std::runtime_error("Type mismatch: expected int64");
  }
}

std::shared_ptr<TypedColumn> I64Column::Clone() const {
  return std::make_shared<I64Column>(data_, null_mask_);
}

bool I64Column::IsNull(size_t row_index) const {
  return row_index >= data_.size() || null_mask_[row_index];
}

void I64Column::SetNull(size_t row_index) {
  if (row_index < null_mask_.size()) {
    null_mask_[row_index] = true;
  }
}

void I64Column::Set(size_t row_index, int64_t value) {
  if (row_index >= data_.size()) {
    throw std::out_of_range("Row index out of bounds");
  }
  data_[row_index] = value;
  null_mask_[row_index] = false;
}

// BoolColumn implementation

BoolColumn::BoolColumn(size_t row_count)
    : data_(row_count, false), null_mask_(row_count, true) {}

Value BoolColumn::GetValue(size_t row_index) const {
  if (row_index >= data_.size() || null_mask_[row_index]) {
    return NullValue{};
  }
  return data_[row_index];
}

void BoolColumn::SetValue(size_t row_index, const Value& value) {
  if (row_index >= data_.size()) {
    throw std::out_of_range("Row index out of bounds");
  }
  if (auto* b = std::get_if<bool>(&value)) {
    data_[row_index] = *b;
    null_mask_[row_index] = false;
  } else if (std::holds_alternative<NullValue>(value)) {
    null_mask_[row_index] = true;
  } else {
    throw std::runtime_error("Type mismatch: expected bool");
  }
}

std::shared_ptr<TypedColumn> BoolColumn::Clone() const {
  auto col = std::make_shared<BoolColumn>(data_.size());
  col->data_ = data_;
  col->null_mask_ = null_mask_;
  return col;
}

bool BoolColumn::IsNull(size_t row_index) const {
  return row_index >= data_.size() || null_mask_[row_index];
}

void BoolColumn::SetNull(size_t row_index) {
  if (row_index < null_mask_.size()) {
    null_mask_[row_index] = true;
  }
}

void BoolColumn::Set(size_t row_index, bool value) {
  if (row_index >= data_.size()) {
    throw std::out_of_range("Row index out of bounds");
  }
  data_[row_index] = value;
  null_mask_[row_index] = false;
}

// StringColumn implementation

StringColumn::StringColumn(size_t row_count)
    : data_(row_count), null_mask_(row_count, true) {}

Value StringColumn::GetValue(size_t row_index) const {
  if (row_index >= data_.size() || null_mask_[row_index]) {
    return NullValue{};
  }
  return data_[row_index];
}

void StringColumn::SetValue(size_t row_index, const Value& value) {
  if (row_index >= data_.size()) {
    throw std::out_of_range("Row index out of bounds");
  }
  if (auto* s = std::get_if<std::string>(&value)) {
    data_[row_index] = *s;
    null_mask_[row_index] = false;
  } else if (std::holds_alternative<NullValue>(value)) {
    null_mask_[row_index] = true;
  } else {
    throw std::runtime_error("Type mismatch: expected string");
  }
}

std::shared_ptr<TypedColumn> StringColumn::Clone() const {
  auto col = std::make_shared<StringColumn>(data_.size());
  col->data_ = data_;
  col->null_mask_ = null_mask_;
  return col;
}

bool StringColumn::IsNull(size_t row_index) const {
  return row_index >= data_.size() || null_mask_[row_index];
}

void StringColumn::SetNull(size_t row_index) {
  if (row_index < null_mask_.size()) {
    null_mask_[row_index] = true;
  }
}

void StringColumn::Set(size_t row_index, std::string value) {
  if (row_index >= data_.size()) {
    throw std::out_of_range("Row index out of bounds");
  }
  data_[row_index] = std::move(value);
  null_mask_[row_index] = false;
}

// F32VecColumn implementation

F32VecColumn::F32VecColumn(size_t row_count, size_t dim)
    : data_(row_count * dim, 0.0f), dim_(dim), null_mask_(row_count, true) {}

F32VecColumn::F32VecColumn(std::vector<float> data, size_t dim, std::vector<bool> null_mask)
    : data_(std::move(data)), dim_(dim), null_mask_(std::move(null_mask)) {
  size_t row_count = dim > 0 ? data_.size() / dim : 0;
  if (null_mask_.size() != row_count) {
    null_mask_.resize(row_count, false);
  }
}

Value F32VecColumn::GetValue(size_t row_index) const {
  if (row_index >= Size() || null_mask_[row_index]) {
    return NullValue{};
  }
  return Get(row_index);
}

void F32VecColumn::SetValue(size_t row_index, const Value& value) {
  if (row_index >= Size()) {
    throw std::out_of_range("Row index out of bounds");
  }
  if (auto* vec = std::get_if<std::vector<float>>(&value)) {
    Set(row_index, *vec);
  } else if (std::holds_alternative<NullValue>(value)) {
    null_mask_[row_index] = true;
  } else {
    throw std::runtime_error("Type mismatch: expected vector<float>");
  }
}

std::shared_ptr<TypedColumn> F32VecColumn::Clone() const {
  return std::make_shared<F32VecColumn>(data_, dim_, null_mask_);
}

bool F32VecColumn::IsNull(size_t row_index) const {
  return row_index >= Size() || null_mask_[row_index];
}

void F32VecColumn::SetNull(size_t row_index) {
  if (row_index < null_mask_.size()) {
    null_mask_[row_index] = true;
  }
}

std::vector<float> F32VecColumn::Get(size_t row_index) const {
  if (row_index >= Size()) {
    throw std::out_of_range("Row index out of bounds");
  }
  size_t start = row_index * dim_;
  return std::vector<float>(data_.begin() + start, data_.begin() + start + dim_);
}

void F32VecColumn::Set(size_t row_index, const std::vector<float>& value) {
  if (row_index >= Size()) {
    throw std::out_of_range("Row index out of bounds");
  }
  if (value.size() != dim_) {
    throw std::runtime_error("Dimension mismatch: expected " +
                             std::to_string(dim_) + ", got " +
                             std::to_string(value.size()));
  }
  size_t start = row_index * dim_;
  std::copy(value.begin(), value.end(), data_.begin() + start);
  null_mask_[row_index] = false;
}

// BytesColumn implementation

BytesColumn::BytesColumn(size_t row_count)
    : data_(row_count), null_mask_(row_count, true) {}

Value BytesColumn::GetValue(size_t row_index) const {
  if (row_index >= data_.size() || null_mask_[row_index]) {
    return NullValue{};
  }
  return data_[row_index];
}

void BytesColumn::SetValue(size_t row_index, const Value& value) {
  if (row_index >= data_.size()) {
    throw std::out_of_range("Row index out of bounds");
  }
  if (auto* bytes = std::get_if<std::vector<uint8_t>>(&value)) {
    data_[row_index] = *bytes;
    null_mask_[row_index] = false;
  } else if (std::holds_alternative<NullValue>(value)) {
    null_mask_[row_index] = true;
  } else {
    throw std::runtime_error("Type mismatch: expected vector<uint8_t>");
  }
}

std::shared_ptr<TypedColumn> BytesColumn::Clone() const {
  auto col = std::make_shared<BytesColumn>(data_.size());
  col->data_ = data_;
  col->null_mask_ = null_mask_;
  return col;
}

bool BytesColumn::IsNull(size_t row_index) const {
  return row_index >= data_.size() || null_mask_[row_index];
}

void BytesColumn::SetNull(size_t row_index) {
  if (row_index < null_mask_.size()) {
    null_mask_[row_index] = true;
  }
}

void BytesColumn::Set(size_t row_index, std::vector<uint8_t> value) {
  if (row_index >= data_.size()) {
    throw std::out_of_range("Row index out of bounds");
  }
  data_[row_index] = std::move(value);
  null_mask_[row_index] = false;
}

// Factory functions

TypedColumnPtr MakeTypedColumn(ColumnType type, size_t row_count, size_t dim) {
  switch (type) {
    case ColumnType::F32:
      return std::make_shared<F32Column>(row_count);
    case ColumnType::I64:
      return std::make_shared<I64Column>(row_count);
    case ColumnType::Bool:
      return std::make_shared<BoolColumn>(row_count);
    case ColumnType::String:
      return std::make_shared<StringColumn>(row_count);
    case ColumnType::F32Vec:
      return std::make_shared<F32VecColumn>(row_count, dim);
    case ColumnType::Bytes:
      return std::make_shared<BytesColumn>(row_count);
    default:
      throw std::runtime_error("Cannot create column of type Null");
  }
}

ColumnType ValueTypeToColumnType(ValueType vt) {
  switch (vt) {
    case ValueType::F32: return ColumnType::F32;
    case ValueType::I64: return ColumnType::I64;
    case ValueType::Bool: return ColumnType::Bool;
    case ValueType::String: return ColumnType::String;
    case ValueType::F32Vec: return ColumnType::F32Vec;
    case ValueType::Bytes: return ColumnType::Bytes;
    default: return ColumnType::Null;
  }
}

ValueType ColumnTypeToValueType(ColumnType ct) {
  switch (ct) {
    case ColumnType::F32: return ValueType::F32;
    case ColumnType::I64: return ValueType::I64;
    case ColumnType::Bool: return ValueType::Bool;
    case ColumnType::String: return ValueType::String;
    case ColumnType::F32Vec: return ValueType::F32Vec;
    case ColumnType::Bytes: return ValueType::Bytes;
    default: return ValueType::Null;
  }
}

}  // namespace ranking_dsl
