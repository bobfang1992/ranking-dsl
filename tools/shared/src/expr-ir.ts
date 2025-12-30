/**
 * Expr IR - JSON expression tree for the engine.
 *
 * This is the canonical representation stored in Plan.
 * JS Expr Sugar gets translated to this format.
 */

import type { Key } from './types.js';

/**
 * Base type for all Expr IR nodes.
 */
export type ExprIR =
  | ConstExpr
  | SignalExpr
  | AddExpr
  | MulExpr
  | CosExpr
  | MinExpr
  | MaxExpr
  | ClampExpr
  | PenaltyExpr;

/**
 * Constant value.
 */
export interface ConstExpr {
  op: 'const';
  value: number;
}

/**
 * Signal (read key value from object).
 */
export interface SignalExpr {
  op: 'signal';
  key_id: number;
}

/**
 * Addition (variadic).
 */
export interface AddExpr {
  op: 'add';
  args: ExprIR[];
}

/**
 * Multiplication (variadic).
 */
export interface MulExpr {
  op: 'mul';
  args: ExprIR[];
}

/**
 * Cosine similarity between two f32vec values.
 * Returns dot(a,b) / (||a|| * ||b||), clamped to [-1, 1].
 * Returns 0 if either vector is missing or has zero norm.
 */
export interface CosExpr {
  op: 'cos';
  a: ExprIR;
  b: ExprIR;
}

/**
 * Minimum (variadic).
 */
export interface MinExpr {
  op: 'min';
  args: ExprIR[];
}

/**
 * Maximum (variadic).
 */
export interface MaxExpr {
  op: 'max';
  args: ExprIR[];
}

/**
 * Clamp value between lo and hi.
 */
export interface ClampExpr {
  op: 'clamp';
  x: ExprIR;
  lo: ExprIR;
  hi: ExprIR;
}

/**
 * Penalty lookup. Reads from penalty.{name} key.
 * Returns 0 if key missing or value is null.
 */
export interface PenaltyExpr {
  op: 'penalty';
  name: string;
}

// Reserved for future:
// export interface DivExpr {
//   op: 'div';
//   a: ExprIR;
//   b: ExprIR;
// }

/**
 * Builder API for Expr IR (dsl.F).
 *
 * Usage:
 *   const F = ExprBuilder;
 *   F.add(F.mul(F.const(0.7), F.signal(Keys.SCORE_BASE)), ...)
 */
export const ExprBuilder = {
  /**
   * Create a constant expression.
   */
  const(value: number): ConstExpr {
    return { op: 'const', value };
  },

  /**
   * Create a signal expression (read from key).
   */
  signal(key: Key | number): SignalExpr {
    const key_id = typeof key === 'number' ? key : key.id;
    return { op: 'signal', key_id };
  },

  /**
   * Create an addition expression.
   */
  add(...args: ExprIR[]): AddExpr {
    if (args.length < 2) {
      throw new Error('add() requires at least 2 arguments');
    }
    return { op: 'add', args };
  },

  /**
   * Create a multiplication expression.
   */
  mul(...args: ExprIR[]): MulExpr {
    if (args.length < 2) {
      throw new Error('mul() requires at least 2 arguments');
    }
    return { op: 'mul', args };
  },

  /**
   * Create a cosine similarity expression.
   */
  cos(a: ExprIR, b: ExprIR): CosExpr {
    return { op: 'cos', a, b };
  },

  /**
   * Alias for cos() - more descriptive name.
   */
  cosSim(a: ExprIR, b: ExprIR): CosExpr {
    return { op: 'cos', a, b };
  },

  /**
   * Create a minimum expression.
   */
  min(...args: ExprIR[]): MinExpr {
    if (args.length < 2) {
      throw new Error('min() requires at least 2 arguments');
    }
    return { op: 'min', args };
  },

  /**
   * Create a maximum expression.
   */
  max(...args: ExprIR[]): MaxExpr {
    if (args.length < 2) {
      throw new Error('max() requires at least 2 arguments');
    }
    return { op: 'max', args };
  },

  /**
   * Create a clamp expression.
   */
  clamp(x: ExprIR, lo: ExprIR, hi: ExprIR): ClampExpr {
    return { op: 'clamp', x, lo, hi };
  },

  /**
   * Create a penalty expression.
   */
  penalty(name: string): PenaltyExpr {
    return { op: 'penalty', name };
  },

  /**
   * Helper: negate an expression (multiply by -1).
   */
  neg(x: ExprIR): MulExpr {
    return { op: 'mul', args: [{ op: 'const', value: -1 }, x] };
  },

  /**
   * Helper: subtract b from a.
   */
  sub(a: ExprIR, b: ExprIR): AddExpr {
    return {
      op: 'add',
      args: [a, { op: 'mul', args: [{ op: 'const', value: -1 }, b] }],
    };
  },
};

/**
 * Type alias for the builder (for use as dsl.F).
 */
export type ExprBuilderType = typeof ExprBuilder;

/**
 * Validate that an object is a valid Expr IR node.
 */
export function isExprIR(obj: unknown): obj is ExprIR {
  if (typeof obj !== 'object' || obj === null) {
    return false;
  }

  const node = obj as Record<string, unknown>;
  const op = node['op'];

  switch (op) {
    case 'const':
      return typeof node['value'] === 'number';
    case 'signal':
      return typeof node['key_id'] === 'number';
    case 'add':
    case 'mul':
    case 'min':
    case 'max':
      return (
        Array.isArray(node['args']) &&
        node['args'].every((arg) => isExprIR(arg))
      );
    case 'cos':
      return isExprIR(node['a']) && isExprIR(node['b']);
    case 'clamp':
      return (
        isExprIR(node['x']) && isExprIR(node['lo']) && isExprIR(node['hi'])
      );
    case 'penalty':
      return typeof node['name'] === 'string';
    default:
      return false;
  }
}

/**
 * Collect all key IDs referenced in an expression.
 */
export function collectKeyIds(expr: ExprIR): Set<number> {
  const ids = new Set<number>();

  function visit(node: ExprIR): void {
    switch (node.op) {
      case 'const':
        break;
      case 'signal':
        ids.add(node.key_id);
        break;
      case 'add':
      case 'mul':
      case 'min':
      case 'max':
        for (const arg of node.args) {
          visit(arg);
        }
        break;
      case 'cos':
        visit(node.a);
        visit(node.b);
        break;
      case 'clamp':
        visit(node.x);
        visit(node.lo);
        visit(node.hi);
        break;
      case 'penalty':
        // penalty.{name} key lookup is done by engine
        break;
    }
  }

  visit(expr);
  return ids;
}
