#include "logging/trace.h"

#include <iostream>

#include <fmt/format.h>
#include <nlohmann/json.hpp>

namespace ranking_dsl {

bool Tracer::enabled_ = true;

void Tracer::LogNodeStart(const std::string& plan_name,
                          const std::string& node_id,
                          const std::string& op) {
  if (!enabled_) return;

  nlohmann::json log;
  log["event"] = "node_start";
  log["plan_name"] = plan_name;
  log["node_id"] = node_id;
  log["op"] = op;

  std::cout << log.dump() << std::endl;
}

void Tracer::LogNodeEnd(const std::string& plan_name,
                        const std::string& node_id,
                        const std::string& op,
                        double duration_ms,
                        size_t rows_in,
                        size_t rows_out,
                        const std::string& error) {
  if (!enabled_) return;

  nlohmann::json log;
  log["event"] = "node_end";
  log["plan_name"] = plan_name;
  log["node_id"] = node_id;
  log["op"] = op;
  log["duration_ms"] = duration_ms;
  log["rows_in"] = rows_in;
  log["rows_out"] = rows_out;

  if (!error.empty()) {
    log["error"] = error;
  }

  std::cout << log.dump() << std::endl;
}

void Tracer::SetEnabled(bool enabled) {
  enabled_ = enabled;
}

bool Tracer::IsEnabled() {
  return enabled_;
}

}  // namespace ranking_dsl
