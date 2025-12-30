import { describe, it, expect } from 'vitest';
import {
  validateRegistry,
  validateSemantics,
  KeyDefSchema,
  RegistrySchema,
} from '../schema.js';

describe('KeyDefSchema', () => {
  it('accepts valid key definition', () => {
    const result = KeyDefSchema.safeParse({
      id: 1001,
      name: 'score.base',
      type: 'f32',
      scope: 'score',
      owner: 'team-a',
      doc: 'Base score',
    });
    expect(result.success).toBe(true);
  });

  it('rejects invalid id (negative)', () => {
    const result = KeyDefSchema.safeParse({
      id: -1,
      name: 'score.base',
      type: 'f32',
      scope: 'score',
      owner: 'team-a',
      doc: 'Base score',
    });
    expect(result.success).toBe(false);
  });

  it('rejects invalid id (non-integer)', () => {
    const result = KeyDefSchema.safeParse({
      id: 1.5,
      name: 'score.base',
      type: 'f32',
      scope: 'score',
      owner: 'team-a',
      doc: 'Base score',
    });
    expect(result.success).toBe(false);
  });

  it('rejects invalid name format (uppercase)', () => {
    const result = KeyDefSchema.safeParse({
      id: 1001,
      name: 'Score.Base',
      type: 'f32',
      scope: 'score',
      owner: 'team-a',
      doc: 'Base score',
    });
    expect(result.success).toBe(false);
  });

  it('rejects invalid name format (starting with dot)', () => {
    const result = KeyDefSchema.safeParse({
      id: 1001,
      name: '.score.base',
      type: 'f32',
      scope: 'score',
      owner: 'team-a',
      doc: 'Base score',
    });
    expect(result.success).toBe(false);
  });

  it('accepts valid name with underscores', () => {
    const result = KeyDefSchema.safeParse({
      id: 1001,
      name: 'score.base_value',
      type: 'f32',
      scope: 'score',
      owner: 'team-a',
      doc: 'Base score',
    });
    expect(result.success).toBe(true);
  });

  it('rejects invalid type', () => {
    const result = KeyDefSchema.safeParse({
      id: 1001,
      name: 'score.base',
      type: 'float64',
      scope: 'score',
      owner: 'team-a',
      doc: 'Base score',
    });
    expect(result.success).toBe(false);
  });

  it('rejects invalid scope', () => {
    const result = KeyDefSchema.safeParse({
      id: 1001,
      name: 'score.base',
      type: 'f32',
      scope: 'invalid',
      owner: 'team-a',
      doc: 'Base score',
    });
    expect(result.success).toBe(false);
  });

  it('accepts all valid types', () => {
    const types = ['bool', 'i64', 'f32', 'string', 'bytes', 'f32vec'];
    for (const type of types) {
      const result = KeyDefSchema.safeParse({
        id: 1001,
        name: 'test.key',
        type,
        scope: 'score',
        owner: 'team-a',
        doc: 'Test key',
      });
      expect(result.success, `type ${type} should be valid`).toBe(true);
    }
  });

  it('accepts all valid scopes', () => {
    const scopes = ['candidate', 'feature', 'score', 'debug', 'tmp', 'penalty'];
    for (const scope of scopes) {
      const result = KeyDefSchema.safeParse({
        id: 1001,
        name: 'test.key',
        type: 'f32',
        scope,
        owner: 'team-a',
        doc: 'Test key',
      });
      expect(result.success, `scope ${scope} should be valid`).toBe(true);
    }
  });
});

describe('validateRegistry', () => {
  it('validates a correct registry', () => {
    const result = validateRegistry({
      version: 1,
      keys: [
        {
          id: 1001,
          name: 'score.base',
          type: 'f32',
          scope: 'score',
          owner: 'team-a',
          doc: 'Base score',
        },
      ],
    });
    expect(result.success).toBe(true);
  });

  it('rejects registry with missing version', () => {
    const result = validateRegistry({
      keys: [],
    });
    expect(result.success).toBe(false);
  });

  it('rejects registry with invalid key', () => {
    const result = validateRegistry({
      version: 1,
      keys: [
        {
          id: -1,
          name: 'score.base',
          type: 'f32',
          scope: 'score',
          owner: 'team-a',
          doc: 'Base score',
        },
      ],
    });
    expect(result.success).toBe(false);
  });
});

describe('validateSemantics', () => {
  it('detects duplicate IDs', () => {
    const registry = {
      version: 1,
      keys: [
        {
          id: 1001,
          name: 'score.a',
          type: 'f32' as const,
          scope: 'score' as const,
          owner: 'team-a',
          doc: 'A',
        },
        {
          id: 1001,
          name: 'score.b',
          type: 'f32' as const,
          scope: 'score' as const,
          owner: 'team-a',
          doc: 'B',
        },
      ],
    };
    const errors = validateSemantics(registry);
    expect(errors.length).toBe(1);
    expect(errors[0]!.message).toContain('Duplicate key ID');
  });

  it('detects duplicate names', () => {
    const registry = {
      version: 1,
      keys: [
        {
          id: 1001,
          name: 'score.base',
          type: 'f32' as const,
          scope: 'score' as const,
          owner: 'team-a',
          doc: 'A',
        },
        {
          id: 1002,
          name: 'score.base',
          type: 'f32' as const,
          scope: 'score' as const,
          owner: 'team-a',
          doc: 'B',
        },
      ],
    };
    const errors = validateSemantics(registry);
    expect(errors.length).toBe(1);
    expect(errors[0]!.message).toContain('Duplicate key name');
  });

  it('returns empty array for valid registry', () => {
    const registry = {
      version: 1,
      keys: [
        {
          id: 1001,
          name: 'score.a',
          type: 'f32' as const,
          scope: 'score' as const,
          owner: 'team-a',
          doc: 'A',
        },
        {
          id: 1002,
          name: 'score.b',
          type: 'f32' as const,
          scope: 'score' as const,
          owner: 'team-a',
          doc: 'B',
        },
      ],
    };
    const errors = validateSemantics(registry);
    expect(errors.length).toBe(0);
  });
});
