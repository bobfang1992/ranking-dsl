/**
 * @ranking-dsl/codegen - Key registry code generation.
 */

import * as fs from 'node:fs';
import * as path from 'node:path';
import { parseRegistryFile, parseRegistryYaml, formatErrors } from './parser.js';
import { generateTypescript } from './generators/typescript.js';
import { generateCpp } from './generators/cpp.js';
import { generateJson } from './generators/json.js';
import { generateNjs } from './generators/njs.js';
import type { Registry } from './schema.js';

export { parseRegistryFile, parseRegistryYaml, formatErrors } from './parser.js';
export { generateTypescript } from './generators/typescript.js';
export { generateCpp } from './generators/cpp.js';
export { generateJson, generateJsonObject } from './generators/json.js';
export { generateNjs } from './generators/njs.js';
export * from './schema.js';

/**
 * Options for code generation.
 */
export interface CodegenOptions {
  /** Path to registry.yaml. */
  registryPath: string;
  /** Output directory for generated files. */
  outputDir: string;
  /** Whether to check freshness only (no write). */
  checkOnly?: boolean;
}

/**
 * Result of code generation.
 */
export interface CodegenResult {
  success: boolean;
  errors: string[];
  /** Files that were written (or would be written in check mode). */
  files: string[];
  /** True if any file was out of date (in check mode). */
  stale?: boolean;
}

/**
 * Run code generation.
 */
export function runCodegen(options: CodegenOptions): CodegenResult {
  const errors: string[] = [];
  const files: string[] = [];
  let stale = false;

  // Parse registry
  const parseResult = parseRegistryFile(options.registryPath);
  if (!parseResult.success) {
    return {
      success: false,
      errors: [`Failed to parse registry:\n${formatErrors(parseResult.errors)}`],
      files: [],
    };
  }

  const registry = parseResult.registry;

  // Ensure output directory exists
  if (!options.checkOnly) {
    fs.mkdirSync(options.outputDir, { recursive: true });
  }

  // Generate files
  const outputs: Array<{ filename: string; content: string }> = [
    { filename: 'keys.ts', content: generateTypescript(registry) },
    { filename: 'keys.h', content: generateCpp(registry) },
    { filename: 'keys.json', content: generateJson(registry) },
    { filename: 'keys.njs', content: generateNjs(registry) },
  ];

  for (const output of outputs) {
    const filePath = path.join(options.outputDir, output.filename);
    files.push(filePath);

    if (options.checkOnly) {
      // Check if file exists and matches
      if (fs.existsSync(filePath)) {
        const existing = fs.readFileSync(filePath, 'utf-8');
        if (existing !== output.content) {
          stale = true;
          errors.push(`File is out of date: ${filePath}`);
        }
      } else {
        stale = true;
        errors.push(`File does not exist: ${filePath}`);
      }
    } else {
      // Write file
      try {
        fs.writeFileSync(filePath, output.content, 'utf-8');
      } catch (e) {
        const message = e instanceof Error ? e.message : String(e);
        errors.push(`Failed to write ${filePath}: ${message}`);
      }
    }
  }

  if (options.checkOnly && stale) {
    return { success: false, errors, files, stale: true };
  }

  return {
    success: errors.length === 0,
    errors,
    files,
    stale: false,
  };
}

/**
 * Load and parse a registry, returning the Registry object.
 */
export function loadRegistry(registryPath: string): Registry {
  const result = parseRegistryFile(registryPath);
  if (!result.success) {
    throw new Error(`Failed to parse registry:\n${formatErrors(result.errors)}`);
  }
  return result.registry;
}
