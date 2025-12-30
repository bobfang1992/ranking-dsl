#pragma once

#include <cstddef>
#include <string>

namespace ranking_dsl {

/**
 * Tracer - structured logging for pipeline execution.
 */
class Tracer {
 public:
  /**
   * Log node execution start.
   */
  static void LogNodeStart(const std::string& plan_name,
                           const std::string& node_id,
                           const std::string& op);

  /**
   * Log node execution end.
   */
  static void LogNodeEnd(const std::string& plan_name,
                         const std::string& node_id,
                         const std::string& op,
                         double duration_ms,
                         size_t rows_in,
                         size_t rows_out,
                         const std::string& error = "");

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
