#pragma once

#include <functional>
#include <memory>
#include <string>
#include <unordered_map>
#include <vector>

#include "nodes/node_runner.h"

namespace ranking_dsl {

/**
 * Factory function type for creating node runners.
 */
using NodeRunnerFactory = std::function<std::unique_ptr<NodeRunner>()>;

/**
 * Stability level for nodes (v0.2.8+).
 */
enum class Stability {
  kStable,
  kExperimental
};

/**
 * Writes descriptor: either static list or param-derived.
 */
struct WritesDescriptor {
  enum class Kind {
    kStatic,       // Fixed list of key IDs
    kParamDerived  // Derived from a param (e.g., "keys")
  };

  Kind kind;
  std::vector<int32_t> static_keys;  // Used when kind == kStatic
  std::string param_name;            // Used when kind == kParamDerived (e.g., "keys")
};

/**
 * NodeSpec: machine-readable metadata for a node (v0.2.8+).
 * This is the source-of-truth for node API information.
 */
struct NodeSpec {
  std::string op;                      // e.g., "core:features"
  std::string namespace_path;          // e.g., "core.features" or "experimental.core.myNode"
  Stability stability;                 // stable or experimental
  std::string doc;                     // Human description
  std::string params_schema_json;      // JSON Schema as string
  std::vector<int32_t> reads;          // Key IDs this node reads
  WritesDescriptor writes;             // What this node writes

  // Optional fields (empty if not applicable)
  std::string budgets_json;            // JSON string with budget constraints
  std::string capabilities_json;       // JSON string with required capabilities
};

/**
 * Registry of node runners with NodeSpec metadata.
 */
class NodeRegistry {
 public:
  /**
   * Get the global singleton instance.
   */
  static NodeRegistry& Instance();

  /**
   * Register a node runner factory with NodeSpec metadata.
   */
  void Register(const std::string& op, NodeRunnerFactory factory, NodeSpec spec);

  /**
   * Create a node runner for the given op.
   * Returns nullptr if op is not registered.
   */
  std::unique_ptr<NodeRunner> Create(const std::string& op) const;

  /**
   * Check if an op is registered.
   */
  bool HasOp(const std::string& op) const;

  /**
   * Get NodeSpec for a registered op.
   * Returns nullptr if op is not registered.
   */
  const NodeSpec* GetSpec(const std::string& op) const;

  /**
   * Get all registered NodeSpecs.
   */
  std::vector<NodeSpec> GetAllSpecs() const;

 private:
  NodeRegistry();
  std::unordered_map<std::string, NodeRunnerFactory> factories_;
  std::unordered_map<std::string, NodeSpec> specs_;
};

/**
 * Helper macro for registering node runners with NodeSpec (v0.2.8+).
 */
#define REGISTER_NODE_RUNNER(op_name, runner_class, node_spec) \
  static bool _registered_##runner_class = []() { \
    NodeRegistry::Instance().Register(op_name, []() { \
      return std::make_unique<runner_class>(); \
    }, node_spec); \
    return true; \
  }()

}  // namespace ranking_dsl
