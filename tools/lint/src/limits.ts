/**
 * Complexity limits for plan.js static gate.
 */

export interface GateLimits {
  /** Maximum file size in bytes. */
  maxFileSize: number;
  /** Maximum nesting depth (if/else, ternary, function calls). */
  maxNestingDepth: number;
  /** Maximum number of if/else branches. */
  maxBranches: number;
  /** Maximum number of statements. */
  maxStatements: number;
}

/**
 * Default limits for plan.js files.
 * These are intentionally conservative to keep plans simple.
 */
export const DEFAULT_LIMITS: GateLimits = {
  maxFileSize: 50_000,      // 50KB
  maxNestingDepth: 10,
  maxBranches: 50,
  maxStatements: 200,
};
