#pragma once

#include <memory>
#include <string>

#include <nlohmann/json_fwd.hpp>

#include "object/candidate_batch.h"

namespace ranking_dsl {

class KeyRegistry;

/**
 * Execution context passed to node runners.
 */
struct ExecContext {
  const KeyRegistry* registry = nullptr;
  // Request-level context can be added here
};

/**
 * Base interface for node runners.
 */
class NodeRunner {
 public:
  virtual ~NodeRunner() = default;

  /**
   * Run the node on a batch of candidates.
   * Returns the transformed batch.
   */
  virtual CandidateBatch Run(const ExecContext& ctx,
                             const CandidateBatch& input,
                             const nlohmann::json& params) = 0;

  /**
   * Get the node type name.
   */
  virtual std::string TypeName() const = 0;
};

}  // namespace ranking_dsl
