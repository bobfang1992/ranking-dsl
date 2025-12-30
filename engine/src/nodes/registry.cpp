#include "nodes/registry.h"

#include <algorithm>

namespace ranking_dsl {

NodeRegistry& NodeRegistry::Instance() {
  static NodeRegistry instance;
  return instance;
}

NodeRegistry::NodeRegistry() {
  // Core nodes are registered via static initialization
}

void NodeRegistry::Register(const std::string& op, NodeRunnerFactory factory, NodeSpec spec) {
  factories_[op] = std::move(factory);
  specs_[op] = std::move(spec);
}

std::unique_ptr<NodeRunner> NodeRegistry::Create(const std::string& op) const {
  auto it = factories_.find(op);
  if (it == factories_.end()) {
    return nullptr;
  }
  return it->second();
}

bool NodeRegistry::HasOp(const std::string& op) const {
  return factories_.count(op) > 0;
}

const NodeSpec* NodeRegistry::GetSpec(const std::string& op) const {
  auto it = specs_.find(op);
  if (it == specs_.end()) {
    return nullptr;
  }
  return &it->second;
}

std::vector<NodeSpec> NodeRegistry::GetAllSpecs() const {
  std::vector<NodeSpec> result;
  result.reserve(specs_.size());
  for (const auto& [op, spec] : specs_) {
    result.push_back(spec);
  }
  // Sort by namespace_path for determinism
  std::sort(result.begin(), result.end(),
            [](const NodeSpec& a, const NodeSpec& b) {
              return a.namespace_path < b.namespace_path;
            });
  return result;
}

}  // namespace ranking_dsl
