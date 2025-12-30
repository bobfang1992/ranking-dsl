#include "logging/trace.h"

#include <iostream>

#include <fmt/format.h>
#include <nlohmann/json.hpp>

namespace ranking_dsl {

bool Tracer::enabled_ = true;

std::string Tracer::SpanName(const std::string& op, const std::string& trace_key) {
  if (trace_key.empty()) {
    return op;
  }
  return fmt::format("{}({})", op, trace_key);
}

std::string Tracer::PrefixedTraceKey(const std::string& trace_prefix,
                                      const std::string& child_trace_key) {
  if (trace_prefix.empty()) {
    return child_trace_key;
  }
  if (child_trace_key.empty()) {
    return trace_prefix;
  }
  return fmt::format("{}::{}", trace_prefix, child_trace_key);
}

std::string Tracer::DeriveTracePrefix(const std::string& njs_file_path) {
  if (njs_file_path.empty()) {
    return "";
  }

  // Find the last path separator
  size_t last_sep = njs_file_path.find_last_of("/\\");
  std::string filename = (last_sep == std::string::npos)
                             ? njs_file_path
                             : njs_file_path.substr(last_sep + 1);

  // Remove .njs extension if present
  const std::string ext = ".njs";
  if (filename.length() > ext.length() &&
      filename.substr(filename.length() - ext.length()) == ext) {
    return filename.substr(0, filename.length() - ext.length());
  }

  // Remove any other extension
  size_t dot_pos = filename.find_last_of('.');
  if (dot_pos != std::string::npos && dot_pos > 0) {
    return filename.substr(0, dot_pos);
  }

  return filename;
}

void Tracer::LogNodeStart(const std::string& plan_name,
                          const std::string& node_id,
                          const std::string& op,
                          const std::string& trace_key,
                          const TraceContext* trace_ctx) {
  if (!enabled_) return;

  nlohmann::json log;
  log["event"] = "node_start";
  log["plan_name"] = plan_name;
  log["node_id"] = node_id;
  log["op"] = op;
  log["span_name"] = SpanName(op, trace_key);

  if (!trace_key.empty()) {
    log["trace_key"] = trace_key;
  }

  if (trace_ctx) {
    if (!trace_ctx->trace_prefix.empty()) {
      log["trace_prefix"] = trace_ctx->trace_prefix;
    }
    if (!trace_ctx->njs_file.empty()) {
      log["njs_file"] = trace_ctx->njs_file;
    }
  }

  std::cout << log.dump() << std::endl;
}

void Tracer::LogNodeEnd(const std::string& plan_name,
                        const std::string& node_id,
                        const std::string& op,
                        double duration_ms,
                        size_t rows_in,
                        size_t rows_out,
                        const std::string& error,
                        const std::string& trace_key,
                        const TraceContext* trace_ctx) {
  if (!enabled_) return;

  nlohmann::json log;
  log["event"] = "node_end";
  log["plan_name"] = plan_name;
  log["node_id"] = node_id;
  log["op"] = op;
  log["span_name"] = SpanName(op, trace_key);
  log["duration_ms"] = duration_ms;
  log["rows_in"] = rows_in;
  log["rows_out"] = rows_out;

  if (!trace_key.empty()) {
    log["trace_key"] = trace_key;
  }

  if (trace_ctx) {
    if (!trace_ctx->trace_prefix.empty()) {
      log["trace_prefix"] = trace_ctx->trace_prefix;
    }
    if (!trace_ctx->njs_file.empty()) {
      log["njs_file"] = trace_ctx->njs_file;
    }
  }

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
