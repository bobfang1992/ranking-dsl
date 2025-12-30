/**
 * Generate keys.json from registry.
 *
 * This JSON file is loaded by the C++ engine at runtime for validation.
 */

import type { Registry } from '../schema.js';

/**
 * JSON output format for a key.
 */
export interface KeyJson {
  id: number;
  name: string;
  type: string;
  scope: string;
  owner: string;
  doc: string;
}

/**
 * JSON output format for the registry.
 */
export interface RegistryJson {
  version: number;
  keys: KeyJson[];
}

/**
 * Generate the keys.json content (as object).
 */
export function generateJsonObject(registry: Registry): RegistryJson {
  return {
    version: registry.version,
    keys: registry.keys.map((key) => ({
      id: key.id,
      name: key.name,
      type: key.type,
      scope: key.scope,
      owner: key.owner,
      doc: key.doc,
    })),
  };
}

/**
 * Generate the keys.json file content (as string).
 */
export function generateJson(registry: Registry): string {
  const obj = generateJsonObject(registry);
  return JSON.stringify(obj, null, 2) + '\n';
}
