/**
 * Parser for keys/registry.yaml.
 */

import * as fs from 'node:fs';
import * as path from 'node:path';
import YAML from 'yaml';
import {
  validateRegistry,
  validateSemantics,
  type Registry,
  type ValidationError,
} from './schema.js';

/**
 * Parse result.
 */
export type ParseResult =
  | { success: true; registry: Registry }
  | { success: false; errors: ValidationError[] };

/**
 * Parse a registry YAML string.
 */
export function parseRegistryYaml(content: string): ParseResult {
  // Parse YAML
  let data: unknown;
  try {
    data = YAML.parse(content);
  } catch (e) {
    const message = e instanceof Error ? e.message : String(e);
    return {
      success: false,
      errors: [{ path: [], message: `YAML parse error: ${message}` }],
    };
  }

  // Validate against schema
  const schemaResult = validateRegistry(data);
  if (!schemaResult.success) {
    return { success: false, errors: schemaResult.errors };
  }

  // Validate semantics
  const semanticErrors = validateSemantics(schemaResult.data);
  if (semanticErrors.length > 0) {
    return { success: false, errors: semanticErrors };
  }

  return { success: true, registry: schemaResult.data };
}

/**
 * Parse a registry YAML file.
 */
export function parseRegistryFile(filePath: string): ParseResult {
  // Check file exists
  if (!fs.existsSync(filePath)) {
    return {
      success: false,
      errors: [{ path: [], message: `File not found: ${filePath}` }],
    };
  }

  // Read file
  let content: string;
  try {
    content = fs.readFileSync(filePath, 'utf-8');
  } catch (e) {
    const message = e instanceof Error ? e.message : String(e);
    return {
      success: false,
      errors: [{ path: [], message: `Failed to read file: ${message}` }],
    };
  }

  return parseRegistryYaml(content);
}

/**
 * Format validation errors for display.
 */
export function formatErrors(errors: ValidationError[]): string {
  return errors
    .map((err) => {
      const pathStr = err.path.length > 0 ? err.path.join('.') + ': ' : '';
      return `  - ${pathStr}${err.message}`;
    })
    .join('\n');
}
