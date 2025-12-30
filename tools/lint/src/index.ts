/**
 * @ranking-dsl/lint - Static gate for plan.js files.
 *
 * Enforces AST rules to keep plan.js files simple and auditable:
 * - No IO (require, import, fetch, fs, etc.)
 * - No eval/Function constructor
 * - No loops (for, while, do-while, for-in, for-of)
 * - No classes or generators
 * - Arrow functions only allowed inside dsl.expr()
 * - Complexity limits (nesting depth, branch count, file size)
 */

export { runGate, type GateResult, type GateError, type GateOptions } from './gate.js';
export { type GateLimits, DEFAULT_LIMITS } from './limits.js';
