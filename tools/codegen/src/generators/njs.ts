/**
 * Generate keys.njs (CommonJS) from registry.
 * For use in njs modules with QuickJS.
 */

import type { Registry } from '../schema.js';

/**
 * Convert key name to a valid JS constant name.
 * e.g., "score.base" -> "SCORE_BASE"
 */
function toConstName(name: string): string {
  return name.toUpperCase().replace(/\./g, '_');
}

/**
 * Generate the keys.njs file content (CommonJS format).
 */
export function generateNjs(registry: Registry): string {
  const lines: string[] = [];

  // Header
  lines.push('/**');
  lines.push(' * Auto-generated key definitions from registry.yaml.');
  lines.push(' * DO NOT EDIT - regenerate with `rankdsl codegen`.');
  lines.push(' *');
  lines.push(' * CommonJS module for use in .njs files.');
  lines.push(' */');
  lines.push('');

  // Keys object with id values
  lines.push('exports.Keys = {');
  for (const key of registry.keys) {
    const constName = toConstName(key.name);
    lines.push(`  /** ${key.doc} (${key.type}) */`);
    lines.push(`  ${constName}: ${key.id},`);
    lines.push('');
  }
  lines.push('};');
  lines.push('');

  // KeyInfo object with full metadata (for advanced use)
  lines.push('exports.KeyInfo = {');
  for (const key of registry.keys) {
    const constName = toConstName(key.name);
    lines.push(`  ${constName}: {`);
    lines.push(`    id: ${key.id},`);
    lines.push(`    name: '${key.name}',`);
    lines.push(`    type: '${key.type}'`);
    lines.push('  },');
  }
  lines.push('};');
  lines.push('');

  return lines.join('\n');
}
