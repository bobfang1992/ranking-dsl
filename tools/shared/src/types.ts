/**
 * Core types for the ranking DSL.
 */

/**
 * Supported value types in the key registry.
 */
export type KeyType = 'bool' | 'i64' | 'f32' | 'string' | 'bytes' | 'f32vec';

/**
 * Key scopes for governance.
 */
export type KeyScope = 'candidate' | 'feature' | 'score' | 'debug' | 'tmp' | 'penalty';

/**
 * A key definition from the registry.
 */
export interface KeyDef {
  /** Unique integer ID (must not be reused). */
  id: number;
  /** Human-readable name (e.g., "score.base"). */
  name: string;
  /** Value type. */
  type: KeyType;
  /** Scope for governance. */
  scope: KeyScope;
  /** Owner team/group. */
  owner: string;
  /** Documentation string. */
  doc: string;
}

/**
 * A Key Handle used at runtime. Contains id, name, and type for validation.
 */
export interface Key {
  /** The unique key ID used at runtime. */
  id: number;
  /** Human-readable name for logs/debugging. */
  name: string;
  /** Value type for validation. */
  type: KeyType;
}

/**
 * The full key registry.
 */
export interface KeyRegistry {
  version: number;
  keys: KeyDef[];
}

/**
 * Runtime value types (matches C++ Value variant).
 */
export type Value =
  | null
  | boolean
  | bigint // i64
  | number // f32
  | string
  | Uint8Array // bytes
  | Float32Array; // f32vec

/**
 * Plan node specification.
 */
export interface NodeSpec {
  /** Unique node ID within the plan. */
  id: string;
  /** Operation type (e.g., "core:sourcer", "js:path@version"). */
  op: string;
  /** IDs of upstream nodes. */
  inputs: string[];
  /** Node parameters. */
  params: Record<string, unknown>;
}

/**
 * Logging configuration for a plan.
 */
export interface PlanLogging {
  /** Sample rate for dumps (0..1). */
  sample_rate?: number;
  /** Key IDs to dump. */
  dump_keys?: number[];
}

/**
 * A compiled plan (JSON-serializable).
 */
export interface Plan {
  /** Plan name. */
  name: string;
  /** Plan version. */
  version: number;
  /** Nodes in the DAG. */
  nodes: NodeSpec[];
  /** Logging configuration. */
  logging?: PlanLogging;
}

/**
 * Source reference from dsl.sourcer().
 */
export interface SourceRef {
  kind: 'source_ref';
  name: string;
  params: Record<string, unknown>;
}

/**
 * JS Expression Token - placeholder for dsl.expr() that gets translated to Expr IR.
 */
export interface JSExprToken {
  kind: 'js_expr_token';
  /** Stable span ID from AST rewrite (format: "start:end"). */
  spanId: string;
}

/**
 * Error with source location.
 */
export interface SourceLocation {
  file: string;
  line: number;
  column: number;
  endLine?: number;
  endColumn?: number;
}

/**
 * DSL error with code and location.
 */
export interface DSLError {
  code: string;
  message: string;
  location?: SourceLocation;
}

/**
 * Result type for operations that can fail.
 */
export type Result<T, E = DSLError> =
  | { ok: true; value: T }
  | { ok: false; error: E };

/**
 * Create a success result.
 */
export function ok<T>(value: T): Result<T, never> {
  return { ok: true, value };
}

/**
 * Create an error result.
 */
export function err<E>(error: E): Result<never, E> {
  return { ok: false, error };
}
