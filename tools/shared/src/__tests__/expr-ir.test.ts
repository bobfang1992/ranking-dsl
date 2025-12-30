import { describe, it, expect } from 'vitest';
import {
  ExprBuilder,
  isExprIR,
  collectKeyIds,
  type ExprIR,
} from '../expr-ir.js';

const F = ExprBuilder;

describe('ExprBuilder', () => {
  describe('const', () => {
    it('creates a constant expression', () => {
      const expr = F.const(0.7);
      expect(expr).toEqual({ op: 'const', value: 0.7 });
    });

    it('handles negative values', () => {
      const expr = F.const(-1.5);
      expect(expr).toEqual({ op: 'const', value: -1.5 });
    });

    it('handles zero', () => {
      const expr = F.const(0);
      expect(expr).toEqual({ op: 'const', value: 0 });
    });
  });

  describe('signal', () => {
    it('creates a signal expression from key ID', () => {
      const expr = F.signal(3001);
      expect(expr).toEqual({ op: 'signal', key_id: 3001 });
    });

    it('creates a signal expression from key object', () => {
      const key = { id: 3001, name: 'score.base', type: 'f32' as const };
      const expr = F.signal(key);
      expect(expr).toEqual({ op: 'signal', key_id: 3001 });
    });
  });

  describe('add', () => {
    it('creates an addition expression', () => {
      const expr = F.add(F.const(1), F.const(2));
      expect(expr).toEqual({
        op: 'add',
        args: [
          { op: 'const', value: 1 },
          { op: 'const', value: 2 },
        ],
      });
    });

    it('handles multiple arguments', () => {
      const expr = F.add(F.const(1), F.const(2), F.const(3));
      expect(expr.args.length).toBe(3);
    });

    it('throws on less than 2 arguments', () => {
      expect(() => F.add(F.const(1))).toThrow('at least 2 arguments');
    });
  });

  describe('mul', () => {
    it('creates a multiplication expression', () => {
      const expr = F.mul(F.const(0.7), F.signal(3001));
      expect(expr).toEqual({
        op: 'mul',
        args: [
          { op: 'const', value: 0.7 },
          { op: 'signal', key_id: 3001 },
        ],
      });
    });

    it('throws on less than 2 arguments', () => {
      expect(() => F.mul(F.const(1))).toThrow('at least 2 arguments');
    });
  });

  describe('cos / cosSim', () => {
    it('creates a cosine similarity expression', () => {
      const expr = F.cos(F.signal(2001), F.signal(2002));
      expect(expr).toEqual({
        op: 'cos',
        a: { op: 'signal', key_id: 2001 },
        b: { op: 'signal', key_id: 2002 },
      });
    });

    it('cosSim is an alias for cos', () => {
      const expr1 = F.cos(F.signal(2001), F.signal(2002));
      const expr2 = F.cosSim(F.signal(2001), F.signal(2002));
      expect(expr1).toEqual(expr2);
    });
  });

  describe('min', () => {
    it('creates a minimum expression', () => {
      const expr = F.min(F.signal(3001), F.signal(3002));
      expect(expr).toEqual({
        op: 'min',
        args: [
          { op: 'signal', key_id: 3001 },
          { op: 'signal', key_id: 3002 },
        ],
      });
    });

    it('handles multiple arguments', () => {
      const expr = F.min(F.const(1), F.const(2), F.const(3));
      expect(expr.args.length).toBe(3);
    });

    it('throws on less than 2 arguments', () => {
      expect(() => F.min(F.const(1))).toThrow('at least 2 arguments');
    });
  });

  describe('max', () => {
    it('creates a maximum expression', () => {
      const expr = F.max(F.signal(3001), F.const(0));
      expect(expr).toEqual({
        op: 'max',
        args: [
          { op: 'signal', key_id: 3001 },
          { op: 'const', value: 0 },
        ],
      });
    });

    it('throws on less than 2 arguments', () => {
      expect(() => F.max(F.const(1))).toThrow('at least 2 arguments');
    });
  });

  describe('clamp', () => {
    it('creates a clamp expression', () => {
      const expr = F.clamp(F.signal(3001), F.const(0), F.const(1));
      expect(expr).toEqual({
        op: 'clamp',
        x: { op: 'signal', key_id: 3001 },
        lo: { op: 'const', value: 0 },
        hi: { op: 'const', value: 1 },
      });
    });
  });

  describe('penalty', () => {
    it('creates a penalty expression', () => {
      const expr = F.penalty('constraints');
      expect(expr).toEqual({ op: 'penalty', name: 'constraints' });
    });
  });

  describe('neg', () => {
    it('negates an expression', () => {
      const expr = F.neg(F.signal(3001));
      expect(expr).toEqual({
        op: 'mul',
        args: [
          { op: 'const', value: -1 },
          { op: 'signal', key_id: 3001 },
        ],
      });
    });
  });

  describe('sub', () => {
    it('subtracts two expressions', () => {
      const expr = F.sub(F.signal(3001), F.signal(3002));
      expect(expr).toEqual({
        op: 'add',
        args: [
          { op: 'signal', key_id: 3001 },
          {
            op: 'mul',
            args: [
              { op: 'const', value: -1 },
              { op: 'signal', key_id: 3002 },
            ],
          },
        ],
      });
    });
  });

  describe('complex expressions', () => {
    it('builds nested expressions', () => {
      // 0.7 * score.base + 0.3 * score.ml
      const expr = F.add(
        F.mul(F.const(0.7), F.signal(3001)),
        F.mul(F.const(0.3), F.signal(3002))
      );

      expect(expr.op).toBe('add');
      expect(expr.args.length).toBe(2);
      expect(expr.args[0]!.op).toBe('mul');
      expect(expr.args[1]!.op).toBe('mul');
    });
  });
});

describe('isExprIR', () => {
  it('returns true for valid const', () => {
    expect(isExprIR({ op: 'const', value: 1 })).toBe(true);
  });

  it('returns true for valid signal', () => {
    expect(isExprIR({ op: 'signal', key_id: 3001 })).toBe(true);
  });

  it('returns true for valid add', () => {
    expect(
      isExprIR({
        op: 'add',
        args: [
          { op: 'const', value: 1 },
          { op: 'const', value: 2 },
        ],
      })
    ).toBe(true);
  });

  it('returns true for valid cos', () => {
    expect(
      isExprIR({
        op: 'cos',
        a: { op: 'signal', key_id: 2001 },
        b: { op: 'signal', key_id: 2002 },
      })
    ).toBe(true);
  });

  it('returns true for valid clamp', () => {
    expect(
      isExprIR({
        op: 'clamp',
        x: { op: 'signal', key_id: 3001 },
        lo: { op: 'const', value: 0 },
        hi: { op: 'const', value: 1 },
      })
    ).toBe(true);
  });

  it('returns true for valid penalty', () => {
    expect(isExprIR({ op: 'penalty', name: 'constraints' })).toBe(true);
  });

  it('returns false for null', () => {
    expect(isExprIR(null)).toBe(false);
  });

  it('returns false for non-object', () => {
    expect(isExprIR('string')).toBe(false);
  });

  it('returns false for unknown op', () => {
    expect(isExprIR({ op: 'unknown', value: 1 })).toBe(false);
  });

  it('returns false for const with non-number value', () => {
    expect(isExprIR({ op: 'const', value: 'string' })).toBe(false);
  });

  it('returns false for add with non-array args', () => {
    expect(isExprIR({ op: 'add', args: 'not an array' })).toBe(false);
  });

  it('returns false for nested invalid expressions', () => {
    expect(
      isExprIR({
        op: 'add',
        args: [
          { op: 'const', value: 1 },
          { op: 'unknown' },
        ],
      })
    ).toBe(false);
  });
});

describe('collectKeyIds', () => {
  it('collects key IDs from signal', () => {
    const expr = F.signal(3001);
    const ids = collectKeyIds(expr);
    expect(ids).toEqual(new Set([3001]));
  });

  it('returns empty set for const', () => {
    const expr = F.const(1);
    const ids = collectKeyIds(expr);
    expect(ids.size).toBe(0);
  });

  it('collects from nested expressions', () => {
    const expr = F.add(
      F.mul(F.const(0.7), F.signal(3001)),
      F.mul(F.const(0.3), F.signal(3002))
    );
    const ids = collectKeyIds(expr);
    expect(ids).toEqual(new Set([3001, 3002]));
  });

  it('deduplicates key IDs', () => {
    const expr = F.add(F.signal(3001), F.signal(3001));
    const ids = collectKeyIds(expr);
    expect(ids).toEqual(new Set([3001]));
  });

  it('collects from cos', () => {
    const expr = F.cos(F.signal(2001), F.signal(2002));
    const ids = collectKeyIds(expr);
    expect(ids).toEqual(new Set([2001, 2002]));
  });

  it('collects from clamp', () => {
    const expr = F.clamp(F.signal(3001), F.signal(3002), F.signal(3003));
    const ids = collectKeyIds(expr);
    expect(ids).toEqual(new Set([3001, 3002, 3003]));
  });

  it('returns empty set for penalty', () => {
    const expr = F.penalty('constraints');
    const ids = collectKeyIds(expr);
    expect(ids.size).toBe(0);
  });
});
