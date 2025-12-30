import { describe, it, expect } from 'vitest';
import { parseRegistryYaml, formatErrors } from '../parser.js';

describe('parseRegistryYaml', () => {
  it('parses valid YAML', () => {
    const yaml = `
version: 1
keys:
  - id: 1001
    name: score.base
    type: f32
    scope: score
    owner: team-a
    doc: Base score
`;
    const result = parseRegistryYaml(yaml);
    expect(result.success).toBe(true);
    if (result.success) {
      expect(result.registry.version).toBe(1);
      expect(result.registry.keys.length).toBe(1);
      expect(result.registry.keys[0]!.id).toBe(1001);
      expect(result.registry.keys[0]!.name).toBe('score.base');
    }
  });

  it('handles invalid YAML syntax', () => {
    const yaml = `
version: 1
keys:
  - id: 1001
    name: score.base
    type: [invalid yaml
`;
    const result = parseRegistryYaml(yaml);
    expect(result.success).toBe(false);
    if (!result.success) {
      expect(result.errors[0]!.message).toContain('YAML parse error');
    }
  });

  it('handles schema validation errors', () => {
    const yaml = `
version: 1
keys:
  - id: -1
    name: score.base
    type: f32
    scope: score
    owner: team-a
    doc: Base score
`;
    const result = parseRegistryYaml(yaml);
    expect(result.success).toBe(false);
  });

  it('handles semantic validation errors (duplicate IDs)', () => {
    const yaml = `
version: 1
keys:
  - id: 1001
    name: score.a
    type: f32
    scope: score
    owner: team-a
    doc: A
  - id: 1001
    name: score.b
    type: f32
    scope: score
    owner: team-a
    doc: B
`;
    const result = parseRegistryYaml(yaml);
    expect(result.success).toBe(false);
    if (!result.success) {
      expect(result.errors[0]!.message).toContain('Duplicate key ID');
    }
  });

  it('parses multiple keys', () => {
    const yaml = `
version: 1
keys:
  - id: 1001
    name: score.base
    type: f32
    scope: score
    owner: team-a
    doc: Base score
  - id: 2001
    name: feat.freshness
    type: f32
    scope: feature
    owner: team-b
    doc: Freshness score
`;
    const result = parseRegistryYaml(yaml);
    expect(result.success).toBe(true);
    if (result.success) {
      expect(result.registry.keys.length).toBe(2);
    }
  });
});

describe('formatErrors', () => {
  it('formats errors with path', () => {
    const errors = [{ path: ['keys', 0, 'id'], message: 'Invalid ID' }];
    const formatted = formatErrors(errors);
    expect(formatted).toBe('  - keys.0.id: Invalid ID');
  });

  it('formats errors without path', () => {
    const errors = [{ path: [], message: 'General error' }];
    const formatted = formatErrors(errors);
    expect(formatted).toBe('  - General error');
  });

  it('formats multiple errors', () => {
    const errors = [
      { path: ['keys', 0, 'id'], message: 'Error 1' },
      { path: ['keys', 1, 'name'], message: 'Error 2' },
    ];
    const formatted = formatErrors(errors);
    expect(formatted).toBe('  - keys.0.id: Error 1\n  - keys.1.name: Error 2');
  });
});
