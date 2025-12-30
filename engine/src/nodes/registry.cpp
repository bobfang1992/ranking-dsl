#include "nodes/registry.h"

namespace ranking_dsl {

NodeRegistry& NodeRegistry::Instance() {
  static NodeRegistry instance;
  return instance;
}

NodeRegistry::NodeRegistry() {
  // Core nodes are registered via static initialization
}

void NodeRegistry::Register(const std::string& op, NodeRunnerFactory factory) {
  factories_[op] = std::move(factory);
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

}  // namespace ranking_dsl
