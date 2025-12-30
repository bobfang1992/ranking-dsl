#pragma once

#include <memory>
#include <string>

#include "object/candidate_batch.h"
#include "plan/compiler.h"

namespace ranking_dsl {

class KeyRegistry;

/**
 * Executor - runs a compiled plan.
 */
class Executor {
 public:
  explicit Executor(const KeyRegistry& registry);

  /**
   * Execute a compiled plan.
   * Returns the final candidate batch.
   */
  CandidateBatch Execute(const CompiledPlan& plan,
                         std::string* error_out = nullptr);

 private:
  const KeyRegistry& registry_;
};

}  // namespace ranking_dsl
