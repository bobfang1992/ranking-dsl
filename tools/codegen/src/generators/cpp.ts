/**
 * Generate keys.h from registry.
 */

import type { Registry } from '../schema.js';

/**
 * Convert key name to a valid C++ constant name.
 * e.g., "score.base" -> "SCORE_BASE"
 */
function toConstName(name: string): string {
  return name.toUpperCase().replace(/\./g, '_');
}

/**
 * Map registry type to C++ type comment.
 */
function toCppTypeComment(type: string): string {
  switch (type) {
    case 'bool':
      return 'bool';
    case 'i64':
      return 'int64_t';
    case 'f32':
      return 'float';
    case 'string':
      return 'std::string';
    case 'bytes':
      return 'std::vector<uint8_t>';
    case 'f32vec':
      return 'std::vector<float>';
    default:
      return 'unknown';
  }
}

/**
 * Generate the keys.h file content.
 */
export function generateCpp(registry: Registry): string {
  const lines: string[] = [];

  // Header guard and includes
  lines.push('/**');
  lines.push(' * Auto-generated key definitions from registry.yaml.');
  lines.push(' * DO NOT EDIT - regenerate with `rankdsl codegen`.');
  lines.push(' */');
  lines.push('');
  lines.push('#pragma once');
  lines.push('');
  lines.push('#include <cstdint>');
  lines.push('#include <string_view>');
  lines.push('#include <array>');
  lines.push('');
  lines.push('namespace ranking_dsl {');
  lines.push('namespace keys {');
  lines.push('');

  // Key type enum
  lines.push('/**');
  lines.push(' * Value types for keys.');
  lines.push(' */');
  lines.push('enum class KeyType : uint8_t {');
  lines.push('  Bool = 0,');
  lines.push('  I64 = 1,');
  lines.push('  F32 = 2,');
  lines.push('  String = 3,');
  lines.push('  Bytes = 4,');
  lines.push('  F32Vec = 5,');
  lines.push('};');
  lines.push('');

  // Key struct
  lines.push('/**');
  lines.push(' * Key metadata.');
  lines.push(' */');
  lines.push('struct KeyDef {');
  lines.push('  int32_t id;');
  lines.push('  std::string_view name;');
  lines.push('  KeyType type;');
  lines.push('};');
  lines.push('');

  // Map type string to enum
  function toKeyTypeEnum(type: string): string {
    switch (type) {
      case 'bool':
        return 'KeyType::Bool';
      case 'i64':
        return 'KeyType::I64';
      case 'f32':
        return 'KeyType::F32';
      case 'string':
        return 'KeyType::String';
      case 'bytes':
        return 'KeyType::Bytes';
      case 'f32vec':
        return 'KeyType::F32Vec';
      default:
        return 'KeyType::F32';
    }
  }

  // Generate key ID constants
  lines.push('/**');
  lines.push(' * Key ID constants.');
  lines.push(' */');
  lines.push('namespace id {');
  for (const key of registry.keys) {
    const constName = toConstName(key.name);
    lines.push(`  /// ${key.doc} (${toCppTypeComment(key.type)})`);
    lines.push(`  inline constexpr int32_t ${constName} = ${key.id};`);
  }
  lines.push('} // namespace id');
  lines.push('');

  // Generate key definitions array
  lines.push('/**');
  lines.push(' * All key definitions.');
  lines.push(' */');
  lines.push(
    `inline constexpr std::array<KeyDef, ${registry.keys.length}> kAllKeys = {{`
  );
  for (const key of registry.keys) {
    const typeEnum = toKeyTypeEnum(key.type);
    lines.push(`  {${key.id}, "${key.name}", ${typeEnum}},`);
  }
  lines.push('}};');
  lines.push('');

  // Helper to get key by ID at compile time
  lines.push('/**');
  lines.push(' * Get key definition by ID (linear search, constexpr).');
  lines.push(' */');
  lines.push('constexpr const KeyDef* GetKeyById(int32_t id) {');
  lines.push('  for (const auto& key : kAllKeys) {');
  lines.push('    if (key.id == id) return &key;');
  lines.push('  }');
  lines.push('  return nullptr;');
  lines.push('}');
  lines.push('');

  lines.push('} // namespace keys');
  lines.push('} // namespace ranking_dsl');
  lines.push('');

  return lines.join('\n');
}
