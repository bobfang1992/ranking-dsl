#pragma once

#include <functional>
#include <memory>
#include <string>
#include <unordered_map>

#include "nodes/node_runner.h"

namespace ranking_dsl {

/**
 * Factory function type for creating node runners.
 */
using NodeRunnerFactory = std::function<std::unique_ptr<NodeRunner>()>;

/**
 * Registry of node runners.
 */
class NodeRegistry {
 public:
  /**
   * Get the global singleton instance.
   */
  static NodeRegistry& Instance();

  /**
   * Register a node runner factory.
   */
  void Register(const std::string& op, NodeRunnerFactory factory);

  /**
   * Create a node runner for the given op.
   * Returns nullptr if op is not registered.
   */
  std::unique_ptr<NodeRunner> Create(const std::string& op) const;

  /**
   * Check if an op is registered.
   */
  bool HasOp(const std::string& op) const;

 private:
  NodeRegistry();
  std::unordered_map<std::string, NodeRunnerFactory> factories_;
};

/**
 * Helper macro for registering node runners.
 */
#define REGISTER_NODE_RUNNER(op_name, runner_class) \
  static bool _registered_##runner_class = []() { \
    NodeRegistry::Instance().Register(op_name, []() { \
      return std::make_unique<runner_class>(); \
    }); \
    return true; \
  }()

}  // namespace ranking_dsl
