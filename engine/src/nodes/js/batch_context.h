#pragma once

#include <cstdint>
#include <memory>
#include <set>
#include <string>
#include <unordered_map>
#include <vector>

#include "object/column_batch.h"
#include "object/batch_builder.h"
#include "object/typed_column.h"
#include "keys/registry.h"

namespace ranking_dsl {

/**
 * Budget enforcement for njs modules.
 */
struct NjsBudget {
  int64_t max_write_bytes = 1048576;   // 1MB default
  int64_t max_write_cells = 100000;    // 100k cells default
  int64_t max_set_per_obj = 10;        // For row-level API
  int64_t max_io_read_bytes = 0;       // 0 = no IO allowed
  int64_t max_io_read_rows = 0;        // 0 = no IO allowed

  int64_t bytes_written = 0;
  int64_t cells_written = 0;
  int64_t io_bytes_read = 0;
  int64_t io_rows_read = 0;
};

/**
 * IO capabilities for njs modules (default: all false).
 */
struct NjsIoCapabilities {
  bool csv_read = false;
};

/**
 * Capabilities that an njs module may request.
 */
struct NjsCapabilities {
  NjsIoCapabilities io;
};

/**
 * F32VecView - zero-copy view of F32VecColumn for JS.
 *
 * Provides contiguous N*D storage access:
 *   { data: Float32Array(N*D), dim: D, rowCount: N }
 *
 * Access row i: data.subarray(i * dim, (i + 1) * dim)
 */
struct F32VecView {
  const float* data;   // Pointer to contiguous N*D storage
  size_t data_size;    // Total size (N * D)
  size_t dim;          // Dimension per row
  size_t row_count;    // Number of rows

  // Get row as span
  const float* GetRow(size_t row) const {
    return data + row * dim;
  }
};

/**
 * BatchContext provides the ctx.batch API for njs modules.
 *
 * This class wraps a ColumnBatch and BatchBuilder to provide:
 * - Read-only column views (f32, f32vec, i64) with zero-copy where possible
 * - Write column allocation (writeF32, writeF32Vec, writeI64)
 * - Budget enforcement
 * - meta.writes enforcement
 */
class BatchContext {
 public:
  BatchContext(const ColumnBatch& batch,
               BatchBuilder& builder,
               const KeyRegistry* registry,
               const std::set<int32_t>& allowed_writes,
               NjsBudget& budget);

  // Read APIs
  size_t RowCount() const { return batch_.RowCount(); }

  // Get zero-copy view of f32 column (returns pointer + size)
  // Returns { data, size } where data is nullptr if column missing
  std::pair<const float*, size_t> GetF32Raw(int32_t key_id) const;

  // Get f32 column as vector (copies if needed for missing values)
  std::vector<float> GetF32(int32_t key_id) const;

  // Get zero-copy view of f32vec column
  // Returns F32VecView with contiguous N*D storage
  F32VecView GetF32VecRaw(int32_t key_id) const;

  // Get f32vec as vector of vectors (legacy, copies)
  std::vector<std::vector<float>> GetF32Vec(int32_t key_id) const;

  // Get zero-copy view of i64 column
  std::pair<const int64_t*, size_t> GetI64Raw(int32_t key_id) const;

  // Get i64 column as vector (copies if needed)
  std::vector<int64_t> GetI64(int32_t key_id) const;

  // Write APIs - these allocate columns backed by BatchBuilder

  /**
   * Allocate a writable f32 column.
   * Throws if key not in meta.writes or wrong type.
   * Returns pointer to contiguous float storage for direct writes.
   */
  float* AllocateF32(int32_t key_id);

  /**
   * Allocate a writable f32vec column with given dimension.
   * Returns pointer to contiguous N*D storage.
   */
  float* AllocateF32Vec(int32_t key_id, size_t dim);

  /**
   * Allocate a writable i64 column.
   */
  int64_t* AllocateI64(int32_t key_id);

  // Commit all allocated columns to the builder
  void Commit();

  // Check if any column writers were used
  bool HasColumnWrites() const { return !allocated_columns_.empty(); }

 private:
  void CheckWriteAllowed(int32_t key_id, keys::KeyType expected_type);
  void CheckBudget(int64_t bytes, int64_t cells);

  const ColumnBatch& batch_;
  BatchBuilder& builder_;
  const KeyRegistry* registry_;
  const std::set<int32_t>& allowed_writes_;
  NjsBudget& budget_;

  // Track allocated writable columns
  struct AllocatedColumn {
    int32_t key_id;
    TypedColumnPtr column;
  };
  std::vector<AllocatedColumn> allocated_columns_;
};

}  // namespace ranking_dsl
