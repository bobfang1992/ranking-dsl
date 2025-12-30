/**
 * Generate keys.ts from registry.
 */

import type { Registry } from '../schema.js';

/**
 * Convert key name to a valid TypeScript constant name.
 * e.g., "score.base" -> "SCORE_BASE"
 */
function toConstName(name: string): string {
  return name.toUpperCase().replace(/\./g, '_');
}

/**
 * Map registry type to TypeScript type.
 */
function toTsType(type: string): string {
  switch (type) {
    case 'bool':
      return 'boolean';
    case 'i64':
      return 'bigint';
    case 'f32':
      return 'number';
    case 'string':
      return 'string';
    case 'bytes':
      return 'Uint8Array';
    case 'f32vec':
      return 'Float32Array';
    default:
      return 'unknown';
  }
}

/**
 * Generate the keys.ts file content.
 */
export function generateTypescript(registry: Registry): string {
  const lines: string[] = [];

  // Header
  lines.push('/**');
  lines.push(' * Auto-generated key definitions from registry.yaml.');
  lines.push(' * DO NOT EDIT - regenerate with `rankdsl codegen`.');
  lines.push(' */');
  lines.push('');
  lines.push('/* eslint-disable */');
  lines.push('');

  // Import Key type
  lines.push("import type { Key, KeyType } from '@ranking-dsl/shared';");
  lines.push('');

  // Key type enum for reference
  lines.push('/**');
  lines.push(' * Key type constants.');
  lines.push(' */');
  lines.push('export const KeyTypes = {');
  lines.push("  bool: 'bool',");
  lines.push("  i64: 'i64',");
  lines.push("  f32: 'f32',");
  lines.push("  string: 'string',");
  lines.push("  bytes: 'bytes',");
  lines.push("  f32vec: 'f32vec',");
  lines.push('} as const;');
  lines.push('');

  // Generate individual key constants
  lines.push('/**');
  lines.push(' * Key handles for use in plan.js and njs modules.');
  lines.push(' */');
  lines.push('export const Keys = {');

  for (const key of registry.keys) {
    const constName = toConstName(key.name);
    lines.push(`  /** ${key.doc} */`);
    lines.push(`  ${constName}: {`);
    lines.push(`    id: ${key.id},`);
    lines.push(`    name: '${key.name}',`);
    lines.push(`    type: '${key.type}' as KeyType,`);
    lines.push('  } satisfies Key,');
    lines.push('');
  }

  lines.push('} as const;');
  lines.push('');

  // Generate type for Keys object
  lines.push('/**');
  lines.push(' * Type of the Keys object.');
  lines.push(' */');
  lines.push('export type KeysType = typeof Keys;');
  lines.push('');

  // Generate union type of all key names
  lines.push('/**');
  lines.push(' * Union of all key constant names.');
  lines.push(' */');
  lines.push('export type KeyName = keyof typeof Keys;');
  lines.push('');

  // Generate lookup by ID
  lines.push('/**');
  lines.push(' * Lookup key by ID.');
  lines.push(' */');
  lines.push('export const KeysById: ReadonlyMap<number, Key> = new Map([');
  for (const key of registry.keys) {
    const constName = toConstName(key.name);
    lines.push(`  [${key.id}, Keys.${constName}],`);
  }
  lines.push(']);');
  lines.push('');

  // Generate lookup by name
  lines.push('/**');
  lines.push(' * Lookup key by name.');
  lines.push(' */');
  lines.push(
    'export const KeysByName: ReadonlyMap<string, Key> = new Map(['
  );
  for (const key of registry.keys) {
    const constName = toConstName(key.name);
    lines.push(`  ['${key.name}', Keys.${constName}],`);
  }
  lines.push(']);');
  lines.push('');

  return lines.join('\n');
}
