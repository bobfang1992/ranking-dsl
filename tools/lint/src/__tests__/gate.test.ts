import { describe, it, expect } from 'vitest';
import { runGate } from '../gate.js';

describe('runGate', () => {
  describe('valid plan.js code', () => {
    it('accepts simple variable declarations', () => {
      const result = runGate(`
        const config = { alpha: 0.7 };
        const p = dsl.sourcer("main", {});
      `);
      expect(result.ok).toBe(true);
      expect(result.errors).toHaveLength(0);
    });

    it('accepts if/else branching', () => {
      const result = runGate(`
        let p = dsl.sourcer("main", {});
        if (config.useNewModel) {
          p = p.model("v2", {});
        } else {
          p = p.model("v1", {});
        }
      `);
      expect(result.ok).toBe(true);
      expect(result.errors).toHaveLength(0);
    });

    it('accepts ternary expressions', () => {
      const result = runGate(`
        const alpha = config.useNewModel ? 0.8 : 0.7;
      `);
      expect(result.ok).toBe(true);
    });

    it('accepts dsl.expr() with arrow function', () => {
      const result = runGate(`
        const expr = dsl.expr(() => 0.7 * Keys.SCORE_BASE);
        p = p.score(expr, {});
      `);
      expect(result.ok).toBe(true);
      expect(result.errors).toHaveLength(0);
    });

    it('accepts regular function declarations', () => {
      const result = runGate(`
        function buildPipeline(cfg) {
          return dsl.sourcer("main", cfg);
        }
        const p = buildPipeline(config);
      `);
      expect(result.ok).toBe(true);
    });

    it('accepts object literals and array literals', () => {
      const result = runGate(`
        const params = { alpha: 0.7, keys: [1, 2, 3] };
        const arr = [1, 2, 3];
      `);
      expect(result.ok).toBe(true);
    });
  });

  describe('disallowed: import/export', () => {
    // Note: import/export are parse errors in script mode (sourceType: 'script')
    // This is correct - plan.js files are scripts, not ES modules
    it('rejects import statements (parse error in script mode)', () => {
      const result = runGate(`import { foo } from 'bar';`);
      expect(result.ok).toBe(false);
      expect(result.errors).toContainEqual(
        expect.objectContaining({ code: 'E_PARSE_ERROR' })
      );
      expect(result.errors[0]?.message).toContain('import');
    });

    it('rejects export statements (parse error in script mode)', () => {
      const result = runGate(`export const foo = 1;`);
      expect(result.ok).toBe(false);
      expect(result.errors).toContainEqual(
        expect.objectContaining({ code: 'E_PARSE_ERROR' })
      );
      expect(result.errors[0]?.message).toContain('export');
    });

    it('rejects export default (parse error in script mode)', () => {
      const result = runGate(`export default {};`);
      expect(result.ok).toBe(false);
      expect(result.errors).toContainEqual(
        expect.objectContaining({ code: 'E_PARSE_ERROR' })
      );
    });
  });

  describe('disallowed: require/dynamic import', () => {
    it('rejects require()', () => {
      const result = runGate(`const fs = require('fs');`);
      expect(result.ok).toBe(false);
      expect(result.errors).toContainEqual(
        expect.objectContaining({ code: 'E_REQUIRE_CALL' })
      );
    });

    it('rejects dynamic import()', () => {
      const result = runGate(`const m = import('module');`);
      expect(result.ok).toBe(false);
      expect(result.errors).toContainEqual(
        expect.objectContaining({ code: 'E_DYNAMIC_IMPORT' })
      );
    });
  });

  describe('disallowed: eval/Function', () => {
    it('rejects eval()', () => {
      const result = runGate(`eval('code');`);
      expect(result.ok).toBe(false);
      expect(result.errors).toContainEqual(
        expect.objectContaining({ code: 'E_EVAL_CALL' })
      );
    });

    it('rejects Function constructor', () => {
      const result = runGate(`const fn = Function('return 1');`);
      expect(result.ok).toBe(false);
      expect(result.errors).toContainEqual(
        expect.objectContaining({ code: 'E_FUNCTION_CONSTRUCTOR' })
      );
    });
  });

  describe('disallowed: loops', () => {
    it('rejects for loop', () => {
      const result = runGate(`for (let i = 0; i < 10; i++) {}`);
      expect(result.ok).toBe(false);
      expect(result.errors).toContainEqual(
        expect.objectContaining({ code: 'E_LOOP_STATEMENT' })
      );
    });

    it('rejects for-in loop', () => {
      const result = runGate(`for (const k in obj) {}`);
      expect(result.ok).toBe(false);
      expect(result.errors).toContainEqual(
        expect.objectContaining({ code: 'E_LOOP_STATEMENT' })
      );
    });

    it('rejects for-of loop', () => {
      const result = runGate(`for (const x of arr) {}`);
      expect(result.ok).toBe(false);
      expect(result.errors).toContainEqual(
        expect.objectContaining({ code: 'E_LOOP_STATEMENT' })
      );
    });

    it('rejects while loop', () => {
      const result = runGate(`while (true) {}`);
      expect(result.ok).toBe(false);
      expect(result.errors).toContainEqual(
        expect.objectContaining({ code: 'E_LOOP_STATEMENT' })
      );
    });

    it('rejects do-while loop', () => {
      const result = runGate(`do {} while (true);`);
      expect(result.ok).toBe(false);
      expect(result.errors).toContainEqual(
        expect.objectContaining({ code: 'E_LOOP_STATEMENT' })
      );
    });
  });

  describe('disallowed: classes', () => {
    it('rejects class declaration', () => {
      const result = runGate(`class Foo {}`);
      expect(result.ok).toBe(false);
      expect(result.errors).toContainEqual(
        expect.objectContaining({ code: 'E_CLASS_DECLARATION' })
      );
    });

    it('rejects class expression', () => {
      const result = runGate(`const Foo = class {};`);
      expect(result.ok).toBe(false);
      expect(result.errors).toContainEqual(
        expect.objectContaining({ code: 'E_CLASS_DECLARATION' })
      );
    });
  });

  describe('disallowed: generators and async', () => {
    it('rejects generator function', () => {
      const result = runGate(`function* gen() { yield 1; }`);
      expect(result.ok).toBe(false);
      expect(result.errors).toContainEqual(
        expect.objectContaining({ code: 'E_GENERATOR_FUNCTION' })
      );
    });

    it('rejects async function', () => {
      const result = runGate(`async function foo() {}`);
      expect(result.ok).toBe(false);
      expect(result.errors).toContainEqual(
        expect.objectContaining({ code: 'E_ASYNC_FUNCTION' })
      );
    });

    it('rejects async arrow function', () => {
      const result = runGate(`const fn = dsl.expr(async () => 1);`);
      expect(result.ok).toBe(false);
      expect(result.errors).toContainEqual(
        expect.objectContaining({ code: 'E_ASYNC_FUNCTION' })
      );
    });
  });

  describe('disallowed: arrow functions outside dsl.expr()', () => {
    it('rejects standalone arrow function', () => {
      const result = runGate(`const fn = () => 1;`);
      expect(result.ok).toBe(false);
      expect(result.errors).toContainEqual(
        expect.objectContaining({ code: 'E_ARROW_OUTSIDE_EXPR' })
      );
    });

    it('rejects arrow function in array', () => {
      const result = runGate(`const fns = [() => 1, () => 2];`);
      expect(result.ok).toBe(false);
      expect(result.errors.filter(e => e.code === 'E_ARROW_OUTSIDE_EXPR')).toHaveLength(2);
    });

    it('rejects arrow function in method call', () => {
      const result = runGate(`arr.map(x => x * 2);`);
      expect(result.ok).toBe(false);
      expect(result.errors).toContainEqual(
        expect.objectContaining({ code: 'E_ARROW_OUTSIDE_EXPR' })
      );
    });
  });

  describe('disallowed: dangerous globals', () => {
    it('rejects process', () => {
      const result = runGate(`const env = process.env;`);
      expect(result.ok).toBe(false);
      expect(result.errors).toContainEqual(
        expect.objectContaining({ code: 'E_DISALLOWED_GLOBAL' })
      );
    });

    it('rejects fs', () => {
      const result = runGate(`fs.readFileSync('file');`);
      expect(result.ok).toBe(false);
      expect(result.errors).toContainEqual(
        expect.objectContaining({ code: 'E_DISALLOWED_GLOBAL' })
      );
    });

    it('rejects fetch', () => {
      const result = runGate(`fetch('http://example.com');`);
      expect(result.ok).toBe(false);
      expect(result.errors).toContainEqual(
        expect.objectContaining({ code: 'E_DISALLOWED_GLOBAL' })
      );
    });

    it('rejects setTimeout', () => {
      const result = runGate(`setTimeout(fn, 1000);`);
      expect(result.ok).toBe(false);
      expect(result.errors).toContainEqual(
        expect.objectContaining({ code: 'E_DISALLOWED_GLOBAL' })
      );
    });

    it('rejects globalThis', () => {
      const result = runGate(`globalThis.foo = 1;`);
      expect(result.ok).toBe(false);
      expect(result.errors).toContainEqual(
        expect.objectContaining({ code: 'E_DISALLOWED_GLOBAL' })
      );
    });
  });

  describe('disallowed: with statement', () => {
    it('rejects with statement', () => {
      const result = runGate(`with (obj) { foo(); }`);
      expect(result.ok).toBe(false);
      expect(result.errors).toContainEqual(
        expect.objectContaining({ code: 'E_WITH_STATEMENT' })
      );
    });
  });

  describe('complexity limits', () => {
    it('rejects file exceeding size limit', () => {
      const result = runGate('x'.repeat(60_000));
      expect(result.ok).toBe(false);
      expect(result.errors).toContainEqual(
        expect.objectContaining({ code: 'E_FILE_TOO_LARGE' })
      );
    });

    it('tracks statement count', () => {
      const result = runGate(`
        const a = 1;
        const b = 2;
        const c = 3;
      `);
      expect(result.metrics.statementCount).toBe(3);
    });

    it('tracks branch count', () => {
      const result = runGate(`
        if (a) { x(); }
        if (b) { y(); } else { z(); }
        const val = cond ? 1 : 2;
      `);
      expect(result.metrics.branchCount).toBe(3);
    });

    it('rejects too many branches', () => {
      const branches = Array.from({ length: 60 }, (_, i) => `if (x${i}) {}`).join('\n');
      const result = runGate(branches);
      expect(result.ok).toBe(false);
      expect(result.errors).toContainEqual(
        expect.objectContaining({ code: 'E_TOO_MANY_BRANCHES' })
      );
    });

    it('respects custom limits', () => {
      const result = runGate('const a = 1;', {
        limits: { maxStatements: 0 },
      });
      expect(result.ok).toBe(false);
      expect(result.errors).toContainEqual(
        expect.objectContaining({ code: 'E_TOO_MANY_STATEMENTS' })
      );
    });
  });

  describe('error location reporting', () => {
    it('includes line and column in errors', () => {
      const result = runGate(`const a = 1;
for (let i = 0; i < 10; i++) {}`);
      expect(result.ok).toBe(false);
      const loopError = result.errors.find(e => e.code === 'E_LOOP_STATEMENT');
      expect(loopError?.loc).toBeDefined();
      expect(loopError?.loc?.line).toBe(2);
    });
  });

  describe('parse errors', () => {
    it('reports syntax errors', () => {
      const result = runGate(`const x = {`);
      expect(result.ok).toBe(false);
      expect(result.errors).toContainEqual(
        expect.objectContaining({ code: 'E_PARSE_ERROR' })
      );
    });
  });

  describe('metrics', () => {
    it('returns file size', () => {
      const source = 'const x = 1;';
      const result = runGate(source);
      expect(result.metrics.fileSize).toBe(source.length);
    });

    it('tracks max nesting depth', () => {
      const result = runGate(`
        if (a) {
          if (b) {
            if (c) {
              x();
            }
          }
        }
      `);
      expect(result.metrics.maxNestingDepth).toBeGreaterThanOrEqual(3);
    });
  });
});
