/**
 * Auto-generated key definitions from registry.yaml.
 * DO NOT EDIT - regenerate with `rankdsl codegen`.
 */

#pragma once

#include <cstdint>
#include <string_view>
#include <array>

namespace ranking_dsl {
namespace keys {

/**
 * Value types for keys.
 */
enum class KeyType : uint8_t {
  Bool = 0,
  I64 = 1,
  F32 = 2,
  String = 3,
  Bytes = 4,
  F32Vec = 5,
};

/**
 * Key metadata.
 */
struct KeyDef {
  int32_t id;
  std::string_view name;
  KeyType type;
};

/**
 * Key ID constants.
 */
namespace id {
  /// Unique candidate identifier (int64_t)
  inline constexpr int32_t CAND_CANDIDATE_ID = 1001;
  /// Freshness score in [0,1] (float)
  inline constexpr int32_t FEAT_FRESHNESS = 2001;
  /// Candidate embedding vector (std::vector<float>)
  inline constexpr int32_t FEAT_EMBEDDING = 2002;
  /// Query embedding vector (std::vector<float>)
  inline constexpr int32_t FEAT_QUERY_EMBEDDING = 2003;
  /// Base retrieval score from sourcer (float)
  inline constexpr int32_t SCORE_BASE = 3001;
  /// ML model prediction score (float)
  inline constexpr int32_t SCORE_ML = 3002;
  /// Score after adjustments (e.g., from njs nodes) (float)
  inline constexpr int32_t SCORE_ADJUSTED = 3003;
  /// Final ranking score used for ordering (float)
  inline constexpr int32_t SCORE_FINAL = 3999;
  /// Penalty for constraint violations (float)
  inline constexpr int32_t PENALTY_CONSTRAINTS = 4001;
  /// Penalty for diversity enforcement (float)
  inline constexpr int32_t PENALTY_DIVERSITY = 4002;
  /// JSON string of per-node timing information (std::string)
  inline constexpr int32_t DEBUG_NODE_TIMINGS = 9001;
} // namespace id

/**
 * All key definitions.
 */
inline constexpr std::array<KeyDef, 11> kAllKeys = {{
  {1001, "cand.candidate_id", KeyType::I64},
  {2001, "feat.freshness", KeyType::F32},
  {2002, "feat.embedding", KeyType::F32Vec},
  {2003, "feat.query_embedding", KeyType::F32Vec},
  {3001, "score.base", KeyType::F32},
  {3002, "score.ml", KeyType::F32},
  {3003, "score.adjusted", KeyType::F32},
  {3999, "score.final", KeyType::F32},
  {4001, "penalty.constraints", KeyType::F32},
  {4002, "penalty.diversity", KeyType::F32},
  {9001, "debug.node_timings", KeyType::String},
}};

/**
 * Get key definition by ID (linear search, constexpr).
 */
constexpr const KeyDef* GetKeyById(int32_t id) {
  for (const auto& key : kAllKeys) {
    if (key.id == id) return &key;
  }
  return nullptr;
}

} // namespace keys
} // namespace ranking_dsl
