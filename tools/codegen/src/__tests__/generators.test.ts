import { describe, it, expect } from 'vitest';
import { generateTypescript } from '../generators/typescript.js';
import { generateCpp } from '../generators/cpp.js';
import { generateJson, generateJsonObject } from '../generators/json.js';
import type { Registry } from '../schema.js';

const testRegistry: Registry = {
  version: 1,
  keys: [
    {
      id: 1001,
      name: 'score.base',
      type: 'f32',
      scope: 'score',
      owner: 'team-a',
      doc: 'Base retrieval score',
    },
    {
      id: 2001,
      name: 'feat.embedding',
      type: 'f32vec',
      scope: 'feature',
      owner: 'team-b',
      doc: 'Embedding vector',
    },
    {
      id: 3001,
      name: 'cand.id',
      type: 'i64',
      scope: 'candidate',
      owner: 'team-c',
      doc: 'Candidate ID',
    },
  ],
};

describe('generateTypescript', () => {
  it('generates valid TypeScript', () => {
    const output = generateTypescript(testRegistry);

    // Check header
    expect(output).toContain('Auto-generated key definitions');
    expect(output).toContain('DO NOT EDIT');

    // Check imports
    expect(output).toContain("import type { Key, KeyType } from '@ranking-dsl/shared'");

    // Check key constants
    expect(output).toContain('SCORE_BASE');
    expect(output).toContain('FEAT_EMBEDDING');
    expect(output).toContain('CAND_ID');

    // Check key properties
    expect(output).toContain('id: 1001');
    expect(output).toContain("name: 'score.base'");
    expect(output).toContain("type: 'f32'");

    // Check doc comments
    expect(output).toContain('Base retrieval score');

    // Check lookups
    expect(output).toContain('KeysById');
    expect(output).toContain('KeysByName');
  });

  it('converts key names to constants correctly', () => {
    const output = generateTypescript(testRegistry);

    // score.base -> SCORE_BASE
    expect(output).toContain('SCORE_BASE:');

    // feat.embedding -> FEAT_EMBEDDING
    expect(output).toContain('FEAT_EMBEDDING:');
  });

  it('handles all key types', () => {
    const registry: Registry = {
      version: 1,
      keys: [
        { id: 1, name: 'a.bool', type: 'bool', scope: 'tmp', owner: 'x', doc: 'bool' },
        { id: 2, name: 'a.i64', type: 'i64', scope: 'tmp', owner: 'x', doc: 'i64' },
        { id: 3, name: 'a.f32', type: 'f32', scope: 'tmp', owner: 'x', doc: 'f32' },
        { id: 4, name: 'a.string', type: 'string', scope: 'tmp', owner: 'x', doc: 'string' },
        { id: 5, name: 'a.bytes', type: 'bytes', scope: 'tmp', owner: 'x', doc: 'bytes' },
        { id: 6, name: 'a.f32vec', type: 'f32vec', scope: 'tmp', owner: 'x', doc: 'f32vec' },
      ],
    };
    const output = generateTypescript(registry);

    expect(output).toContain("type: 'bool'");
    expect(output).toContain("type: 'i64'");
    expect(output).toContain("type: 'f32'");
    expect(output).toContain("type: 'string'");
    expect(output).toContain("type: 'bytes'");
    expect(output).toContain("type: 'f32vec'");
  });
});

describe('generateCpp', () => {
  it('generates valid C++ header', () => {
    const output = generateCpp(testRegistry);

    // Check header guard
    expect(output).toContain('#pragma once');

    // Check includes
    expect(output).toContain('#include <cstdint>');
    expect(output).toContain('#include <string_view>');
    expect(output).toContain('#include <array>');

    // Check namespace
    expect(output).toContain('namespace ranking_dsl');
    expect(output).toContain('namespace keys');

    // Check KeyType enum
    expect(output).toContain('enum class KeyType');
    expect(output).toContain('Bool');
    expect(output).toContain('I64');
    expect(output).toContain('F32');
    expect(output).toContain('String');
    expect(output).toContain('Bytes');
    expect(output).toContain('F32Vec');

    // Check key ID constants
    expect(output).toContain('SCORE_BASE = 1001');
    expect(output).toContain('FEAT_EMBEDDING = 2001');
    expect(output).toContain('CAND_ID = 3001');

    // Check kAllKeys array
    expect(output).toContain('kAllKeys');
    expect(output).toContain('"score.base"');
    expect(output).toContain('KeyType::F32');
    expect(output).toContain('KeyType::F32Vec');
    expect(output).toContain('KeyType::I64');

    // Check GetKeyById function
    expect(output).toContain('GetKeyById');
  });

  it('includes doc comments', () => {
    const output = generateCpp(testRegistry);
    expect(output).toContain('Base retrieval score');
    expect(output).toContain('Embedding vector');
  });
});

describe('generateJson', () => {
  it('generates valid JSON', () => {
    const output = generateJson(testRegistry);
    const parsed = JSON.parse(output);

    expect(parsed.version).toBe(1);
    expect(parsed.keys.length).toBe(3);
  });

  it('preserves all fields', () => {
    const obj = generateJsonObject(testRegistry);

    expect(obj.keys[0]).toEqual({
      id: 1001,
      name: 'score.base',
      type: 'f32',
      scope: 'score',
      owner: 'team-a',
      doc: 'Base retrieval score',
    });
  });

  it('generates pretty-printed JSON', () => {
    const output = generateJson(testRegistry);
    // Check for indentation
    expect(output).toContain('  "version"');
    expect(output).toContain('    "id"');
  });
});
