/**
 * Static gate implementation for plan.js files.
 */

import { parse, type ParserOptions } from '@babel/parser';
import type { Node, SourceLocation } from '@babel/types';
import { GateLimits, DEFAULT_LIMITS } from './limits.js';

/**
 * Error codes for gate violations.
 */
export type GateErrorCode =
  | 'E_PARSE_ERROR'
  | 'E_IMPORT_STATEMENT'
  | 'E_EXPORT_STATEMENT'
  | 'E_REQUIRE_CALL'
  | 'E_DYNAMIC_IMPORT'
  | 'E_EVAL_CALL'
  | 'E_FUNCTION_CONSTRUCTOR'
  | 'E_LOOP_STATEMENT'
  | 'E_CLASS_DECLARATION'
  | 'E_GENERATOR_FUNCTION'
  | 'E_ASYNC_FUNCTION'
  | 'E_WITH_STATEMENT'
  | 'E_ARROW_OUTSIDE_EXPR'
  | 'E_DISALLOWED_GLOBAL'
  | 'E_FILE_TOO_LARGE'
  | 'E_NESTING_TOO_DEEP'
  | 'E_TOO_MANY_BRANCHES'
  | 'E_TOO_MANY_STATEMENTS';

/**
 * A single gate error with location information.
 */
export interface GateError {
  code: GateErrorCode;
  message: string;
  loc?: {
    line: number;
    column: number;
    endLine?: number;
    endColumn?: number;
  };
}

/**
 * Result of running the static gate.
 */
export interface GateResult {
  ok: boolean;
  errors: GateError[];
  /** Metrics collected during analysis. */
  metrics: {
    fileSize: number;
    statementCount: number;
    branchCount: number;
    maxNestingDepth: number;
  };
}

/**
 * Options for the static gate.
 */
export interface GateOptions {
  /** File path for error reporting. */
  filePath?: string;
  /** Custom limits (merged with defaults). */
  limits?: Partial<GateLimits>;
}

/** Disallowed global identifiers that indicate IO or unsafe operations. */
const DISALLOWED_GLOBALS = new Set([
  'require',
  'process',
  'fs',
  'fetch',
  'XMLHttpRequest',
  'WebSocket',
  '__dirname',
  '__filename',
  'module',
  'exports',
  'global',
  'globalThis',
  'setTimeout',
  'setInterval',
  'setImmediate',
]);

/**
 * Run the static gate on plan.js source code.
 */
export function runGate(source: string, options: GateOptions = {}): GateResult {
  const limits = { ...DEFAULT_LIMITS, ...options.limits };
  const errors: GateError[] = [];
  const metrics = {
    fileSize: source.length,
    statementCount: 0,
    branchCount: 0,
    maxNestingDepth: 0,
  };

  // Check file size first
  if (source.length > limits.maxFileSize) {
    errors.push({
      code: 'E_FILE_TOO_LARGE',
      message: `File size ${source.length} exceeds limit ${limits.maxFileSize}`,
    });
  }

  // Parse the source
  let ast: Node;
  try {
    const parserOptions: ParserOptions = {
      sourceType: 'script',
      plugins: [],
      errorRecovery: false,
    };
    ast = parse(source, parserOptions);
  } catch (e) {
    const err = e as Error & { loc?: { line: number; column: number } };
    errors.push({
      code: 'E_PARSE_ERROR',
      message: err.message,
      loc: err.loc ? { line: err.loc.line, column: err.loc.column } : undefined,
    });
    return { ok: false, errors, metrics };
  }

  // Walk the AST
  walkNode(ast, {
    errors,
    metrics,
    limits,
    depth: 0,
    inDslExpr: false,
  });

  // Check complexity limits
  if (metrics.statementCount > limits.maxStatements) {
    errors.push({
      code: 'E_TOO_MANY_STATEMENTS',
      message: `Statement count ${metrics.statementCount} exceeds limit ${limits.maxStatements}`,
    });
  }

  if (metrics.branchCount > limits.maxBranches) {
    errors.push({
      code: 'E_TOO_MANY_BRANCHES',
      message: `Branch count ${metrics.branchCount} exceeds limit ${limits.maxBranches}`,
    });
  }

  if (metrics.maxNestingDepth > limits.maxNestingDepth) {
    errors.push({
      code: 'E_NESTING_TOO_DEEP',
      message: `Nesting depth ${metrics.maxNestingDepth} exceeds limit ${limits.maxNestingDepth}`,
    });
  }

  return {
    ok: errors.length === 0,
    errors,
    metrics,
  };
}

interface WalkContext {
  errors: GateError[];
  metrics: GateResult['metrics'];
  limits: GateLimits;
  depth: number;
  inDslExpr: boolean;
}

function locFromNode(node: Node): GateError['loc'] {
  if (!node.loc) return undefined;
  return {
    line: node.loc.start.line,
    column: node.loc.start.column,
    endLine: node.loc.end.line,
    endColumn: node.loc.end.column,
  };
}

function walkNode(node: Node | null | undefined, ctx: WalkContext): void {
  if (!node) return;

  // Track nesting depth
  ctx.metrics.maxNestingDepth = Math.max(ctx.metrics.maxNestingDepth, ctx.depth);

  switch (node.type) {
    // Disallowed: import/export
    case 'ImportDeclaration':
      ctx.errors.push({
        code: 'E_IMPORT_STATEMENT',
        message: 'import statements are not allowed in plan.js',
        loc: locFromNode(node),
      });
      break;

    case 'ExportDefaultDeclaration':
    case 'ExportNamedDeclaration':
    case 'ExportAllDeclaration':
      ctx.errors.push({
        code: 'E_EXPORT_STATEMENT',
        message: 'export statements are not allowed in plan.js',
        loc: locFromNode(node),
      });
      break;

    // Disallowed: loops
    case 'ForStatement':
    case 'ForInStatement':
    case 'ForOfStatement':
    case 'WhileStatement':
    case 'DoWhileStatement':
      ctx.errors.push({
        code: 'E_LOOP_STATEMENT',
        message: `${node.type} is not allowed in plan.js`,
        loc: locFromNode(node),
      });
      break;

    // Disallowed: classes
    case 'ClassDeclaration':
    case 'ClassExpression':
      ctx.errors.push({
        code: 'E_CLASS_DECLARATION',
        message: 'Classes are not allowed in plan.js',
        loc: locFromNode(node),
      });
      break;

    // Disallowed: with statement
    case 'WithStatement':
      ctx.errors.push({
        code: 'E_WITH_STATEMENT',
        message: 'with statements are not allowed in plan.js',
        loc: locFromNode(node),
      });
      break;

    // Check function declarations for generators/async
    case 'FunctionDeclaration':
    case 'FunctionExpression':
      if ('generator' in node && node.generator) {
        ctx.errors.push({
          code: 'E_GENERATOR_FUNCTION',
          message: 'Generator functions are not allowed in plan.js',
          loc: locFromNode(node),
        });
      }
      if ('async' in node && node.async) {
        ctx.errors.push({
          code: 'E_ASYNC_FUNCTION',
          message: 'Async functions are not allowed in plan.js',
          loc: locFromNode(node),
        });
      }
      // Walk children
      walkChildren(node, ctx);
      break;

    // Arrow functions: only allowed inside dsl.expr()
    case 'ArrowFunctionExpression':
      if (!ctx.inDslExpr) {
        ctx.errors.push({
          code: 'E_ARROW_OUTSIDE_EXPR',
          message: 'Arrow functions are only allowed inside dsl.expr()',
          loc: locFromNode(node),
        });
      }
      if ('async' in node && node.async) {
        ctx.errors.push({
          code: 'E_ASYNC_FUNCTION',
          message: 'Async arrow functions are not allowed in plan.js',
          loc: locFromNode(node),
        });
      }
      // Don't walk into arrow function body inside dsl.expr
      // (the expression will be parsed separately during translation)
      break;

    // Call expressions: check for disallowed calls
    case 'CallExpression':
      checkCallExpression(node, ctx);
      // Check if this is dsl.expr() to allow arrow functions inside
      if (isDslExprCall(node)) {
        const newCtx = { ...ctx, inDslExpr: true, depth: ctx.depth + 1 };
        walkChildren(node, newCtx);
      } else {
        walkChildren(node, { ...ctx, depth: ctx.depth + 1 });
      }
      break;

    // New expressions: check for new Function()
    case 'NewExpression':
      checkNewExpression(node, ctx);
      walkChildren(node, { ...ctx, depth: ctx.depth + 1 });
      break;

    // Track statements
    case 'ExpressionStatement':
    case 'VariableDeclaration':
    case 'ReturnStatement':
    case 'ThrowStatement':
      ctx.metrics.statementCount++;
      walkChildren(node, ctx);
      break;

    // Track branches
    case 'IfStatement':
      ctx.metrics.branchCount++;
      ctx.metrics.statementCount++;
      walkChildren(node, { ...ctx, depth: ctx.depth + 1 });
      break;

    case 'ConditionalExpression':
      ctx.metrics.branchCount++;
      walkChildren(node, { ...ctx, depth: ctx.depth + 1 });
      break;

    case 'SwitchStatement':
      ctx.metrics.statementCount++;
      if ('cases' in node && Array.isArray(node.cases)) {
        ctx.metrics.branchCount += node.cases.length;
      }
      walkChildren(node, { ...ctx, depth: ctx.depth + 1 });
      break;

    // Check identifiers for disallowed globals
    case 'Identifier':
      if (DISALLOWED_GLOBALS.has(node.name)) {
        ctx.errors.push({
          code: 'E_DISALLOWED_GLOBAL',
          message: `'${node.name}' is not allowed in plan.js`,
          loc: locFromNode(node),
        });
      }
      break;

    // Default: walk children
    default:
      walkChildren(node, ctx);
  }
}

function walkChildren(node: Node, ctx: WalkContext): void {
  for (const key of Object.keys(node)) {
    if (key === 'loc' || key === 'start' || key === 'end' || key === 'type') continue;
    const child = (node as unknown as Record<string, unknown>)[key];
    if (Array.isArray(child)) {
      for (const item of child) {
        if (item && typeof item === 'object' && 'type' in item) {
          walkNode(item as Node, ctx);
        }
      }
    } else if (child && typeof child === 'object' && 'type' in child) {
      walkNode(child as Node, ctx);
    }
  }
}

function checkCallExpression(node: Node, ctx: WalkContext): void {
  if (node.type !== 'CallExpression') return;

  const callee = node.callee;

  // Check for require()
  if (callee.type === 'Identifier' && callee.name === 'require') {
    ctx.errors.push({
      code: 'E_REQUIRE_CALL',
      message: 'require() is not allowed in plan.js',
      loc: locFromNode(node),
    });
    return;
  }

  // Check for eval()
  if (callee.type === 'Identifier' && callee.name === 'eval') {
    ctx.errors.push({
      code: 'E_EVAL_CALL',
      message: 'eval() is not allowed in plan.js',
      loc: locFromNode(node),
    });
    return;
  }

  // Check for Function()
  if (callee.type === 'Identifier' && callee.name === 'Function') {
    ctx.errors.push({
      code: 'E_FUNCTION_CONSTRUCTOR',
      message: 'Function constructor is not allowed in plan.js',
      loc: locFromNode(node),
    });
    return;
  }

  // Check for dynamic import()
  if (callee.type === 'Import') {
    ctx.errors.push({
      code: 'E_DYNAMIC_IMPORT',
      message: 'Dynamic import() is not allowed in plan.js',
      loc: locFromNode(node),
    });
  }
}

function checkNewExpression(node: Node, ctx: WalkContext): void {
  if (node.type !== 'NewExpression') return;

  const callee = node.callee;

  // Check for new Function()
  if (callee.type === 'Identifier' && callee.name === 'Function') {
    ctx.errors.push({
      code: 'E_FUNCTION_CONSTRUCTOR',
      message: 'Function constructor is not allowed in plan.js',
      loc: locFromNode(node),
    });
  }
}

/**
 * Check if a call expression is dsl.expr(...).
 */
function isDslExprCall(node: Node): boolean {
  if (node.type !== 'CallExpression') return false;
  const callee = node.callee;
  if (callee.type !== 'MemberExpression') return false;
  if (callee.object.type !== 'Identifier' || callee.object.name !== 'dsl') return false;
  if (callee.property.type !== 'Identifier' || callee.property.name !== 'expr') return false;
  return true;
}
