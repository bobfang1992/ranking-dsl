#pragma once

#include <cstddef>
#include <string>

namespace ranking_dsl {

/**
 * Tracing context for njs modules.
 * Used to track trace_prefix for nested native calls.
 */
struct TraceContext {
  std::string trace_prefix;   // Derived from njs filename stem
  std::string njs_file;       // Full njs file path
};

/**
 * Tracer - structured logging for pipeline execution.
 */
class Tracer {
 public:
  /**
   * Log node execution start.
   * @param trace_key Optional trace key for this node (empty = not set)
   * @param trace_ctx Optional trace context for njs nested calls
   */
  static void LogNodeStart(const std::string& plan_name,
                           const std::string& node_id,
                           const std::string& op,
                           const std::string& trace_key = "",
                           const TraceContext* trace_ctx = nullptr);

  /**
   * Log node execution end.
   * @param trace_key Optional trace key for this node (empty = not set)
   * @param trace_ctx Optional trace context for njs nested calls
   */
  static void LogNodeEnd(const std::string& plan_name,
                         const std::string& node_id,
                         const std::string& op,
                         double duration_ms,
                         size_t rows_in,
                         size_t rows_out,
                         const std::string& error = "",
                         const std::string& trace_key = "",
                         const TraceContext* trace_ctx = nullptr);

  /**
   * Compute span name from op and trace_key.
   * Format: op(trace_key) if trace_key is present, otherwise just op.
   */
  static std::string SpanName(const std::string& op, const std::string& trace_key);

  /**
   * Compute prefixed trace_key for njs nested calls.
   * Format: {trace_prefix}::{child_trace_key} or just {trace_prefix} if child is empty.
   */
  static std::string PrefixedTraceKey(const std::string& trace_prefix,
                                       const std::string& child_trace_key);

  /**
   * Derive trace_prefix from njs file path.
   * Extracts the filename stem (e.g., "rank_vm.njs" -> "rank_vm").
   */
  static std::string DeriveTracePrefix(const std::string& njs_file_path);

  /**
   * Enable/disable tracing output.
   */
  static void SetEnabled(bool enabled);

  /**
   * Check if tracing is enabled.
   */
  static bool IsEnabled();

 private:
  static bool enabled_;
};

}  // namespace ranking_dsl
