#pragma once

#include <cstddef>
#include <cstdint>
#include <memory>
#include <string>
#include <vector>

#include "object/value.h"

namespace ranking_dsl {

/**
 * Column type enumeration.
 */
enum class ColumnType {
  F32,
  I64,
  Bool,
  String,
  F32Vec,
  Bytes,
  Null
};

/**
 * Base class for typed columns.
 *
 * Typed columns provide contiguous storage for efficient access and
 * zero-copy views for JS integration.
 */
class TypedColumn {
 public:
  virtual ~TypedColumn() = default;

  /**
   * Get the column type.
   */
  virtual ColumnType Type() const = 0;

  /**
   * Get the number of rows.
   */
  virtual size_t Size() const = 0;

  /**
   * Get value at row index as a Value variant (slower path).
   */
  virtual Value GetValue(size_t row_index) const = 0;

  /**
   * Set value at row index from a Value variant (slower path).
   */
  virtual void SetValue(size_t row_index, const Value& value) = 0;

  /**
   * Clone this column.
   */
  virtual std::shared_ptr<TypedColumn> Clone() const = 0;

  /**
   * Check if value at row index is null.
   */
  virtual bool IsNull(size_t row_index) const = 0;

  /**
   * Set value at row index to null.
   */
  virtual void SetNull(size_t row_index) = 0;
};

using TypedColumnPtr = std::shared_ptr<TypedColumn>;

/**
 * F32Column - contiguous float storage.
 *
 * Supports zero-copy Float32Array views for JS.
 */
class F32Column : public TypedColumn {
 public:
  F32Column() = default;
  explicit F32Column(size_t row_count);
  explicit F32Column(std::vector<float> data, std::vector<bool> null_mask);

  ColumnType Type() const override { return ColumnType::F32; }
  size_t Size() const override { return data_.size(); }
  Value GetValue(size_t row_index) const override;
  void SetValue(size_t row_index, const Value& value) override;
  std::shared_ptr<TypedColumn> Clone() const override;
  bool IsNull(size_t row_index) const override;
  void SetNull(size_t row_index) override;

  // Typed accessors (fast path)
  float Get(size_t row_index) const { return data_[row_index]; }
  void Set(size_t row_index, float value);

  // Zero-copy access
  float* Data() { return data_.data(); }
  const float* Data() const { return data_.data(); }

 private:
  std::vector<float> data_;
  std::vector<bool> null_mask_;  // true = null
};

/**
 * I64Column - contiguous int64 storage.
 */
class I64Column : public TypedColumn {
 public:
  I64Column() = default;
  explicit I64Column(size_t row_count);
  explicit I64Column(std::vector<int64_t> data, std::vector<bool> null_mask);

  ColumnType Type() const override { return ColumnType::I64; }
  size_t Size() const override { return data_.size(); }
  Value GetValue(size_t row_index) const override;
  void SetValue(size_t row_index, const Value& value) override;
  std::shared_ptr<TypedColumn> Clone() const override;
  bool IsNull(size_t row_index) const override;
  void SetNull(size_t row_index) override;

  // Typed accessors
  int64_t Get(size_t row_index) const { return data_[row_index]; }
  void Set(size_t row_index, int64_t value);

  // Zero-copy access
  int64_t* Data() { return data_.data(); }
  const int64_t* Data() const { return data_.data(); }

 private:
  std::vector<int64_t> data_;
  std::vector<bool> null_mask_;
};

/**
 * BoolColumn - bool storage.
 */
class BoolColumn : public TypedColumn {
 public:
  BoolColumn() = default;
  explicit BoolColumn(size_t row_count);

  ColumnType Type() const override { return ColumnType::Bool; }
  size_t Size() const override { return data_.size(); }
  Value GetValue(size_t row_index) const override;
  void SetValue(size_t row_index, const Value& value) override;
  std::shared_ptr<TypedColumn> Clone() const override;
  bool IsNull(size_t row_index) const override;
  void SetNull(size_t row_index) override;

  // Typed accessors
  bool Get(size_t row_index) const { return data_[row_index]; }
  void Set(size_t row_index, bool value);

 private:
  std::vector<bool> data_;
  std::vector<bool> null_mask_;
};

/**
 * StringColumn - string storage.
 */
class StringColumn : public TypedColumn {
 public:
  StringColumn() = default;
  explicit StringColumn(size_t row_count);

  ColumnType Type() const override { return ColumnType::String; }
  size_t Size() const override { return data_.size(); }
  Value GetValue(size_t row_index) const override;
  void SetValue(size_t row_index, const Value& value) override;
  std::shared_ptr<TypedColumn> Clone() const override;
  bool IsNull(size_t row_index) const override;
  void SetNull(size_t row_index) override;

  // Typed accessors
  const std::string& Get(size_t row_index) const { return data_[row_index]; }
  void Set(size_t row_index, std::string value);

 private:
  std::vector<std::string> data_;
  std::vector<bool> null_mask_;
};

/**
 * F32VecColumn - contiguous N×D float storage for embeddings.
 *
 * Data is stored row-major: [r0d0, r0d1, ..., r0dD, r1d0, r1d1, ...]
 * This enables zero-copy Float32Array views with subarray slicing.
 */
class F32VecColumn : public TypedColumn {
 public:
  F32VecColumn() = default;
  F32VecColumn(size_t row_count, size_t dim);
  F32VecColumn(std::vector<float> data, size_t dim, std::vector<bool> null_mask);

  ColumnType Type() const override { return ColumnType::F32Vec; }
  size_t Size() const override { return dim_ > 0 ? data_.size() / dim_ : 0; }
  Value GetValue(size_t row_index) const override;
  void SetValue(size_t row_index, const Value& value) override;
  std::shared_ptr<TypedColumn> Clone() const override;
  bool IsNull(size_t row_index) const override;
  void SetNull(size_t row_index) override;

  // Dimension accessor
  size_t Dim() const { return dim_; }

  // Get row as span (pointer + size)
  const float* GetRow(size_t row_index) const {
    return data_.data() + row_index * dim_;
  }
  float* GetRowMutable(size_t row_index) {
    return data_.data() + row_index * dim_;
  }

  // Get row as vector (copy)
  std::vector<float> Get(size_t row_index) const;

  // Set row
  void Set(size_t row_index, const std::vector<float>& value);

  // Zero-copy access to entire data buffer
  float* Data() { return data_.data(); }
  const float* Data() const { return data_.data(); }
  size_t DataSize() const { return data_.size(); }

 private:
  std::vector<float> data_;  // N×D contiguous
  size_t dim_ = 0;
  std::vector<bool> null_mask_;
};

/**
 * BytesColumn - bytes/blob storage.
 */
class BytesColumn : public TypedColumn {
 public:
  BytesColumn() = default;
  explicit BytesColumn(size_t row_count);

  ColumnType Type() const override { return ColumnType::Bytes; }
  size_t Size() const override { return data_.size(); }
  Value GetValue(size_t row_index) const override;
  void SetValue(size_t row_index, const Value& value) override;
  std::shared_ptr<TypedColumn> Clone() const override;
  bool IsNull(size_t row_index) const override;
  void SetNull(size_t row_index) override;

  // Typed accessors
  const std::vector<uint8_t>& Get(size_t row_index) const { return data_[row_index]; }
  void Set(size_t row_index, std::vector<uint8_t> value);

 private:
  std::vector<std::vector<uint8_t>> data_;
  std::vector<bool> null_mask_;
};

/**
 * Create a typed column for the given value type.
 */
TypedColumnPtr MakeTypedColumn(ColumnType type, size_t row_count, size_t dim = 0);

/**
 * Convert ValueType to ColumnType.
 */
ColumnType ValueTypeToColumnType(ValueType vt);

/**
 * Convert ColumnType to ValueType.
 */
ValueType ColumnTypeToValueType(ColumnType ct);

}  // namespace ranking_dsl
