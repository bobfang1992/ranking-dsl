/**
 * Zod schema for keys/registry.yaml validation.
 */

import { z } from 'zod';

/**
 * Valid key types.
 */
export const KeyTypeSchema = z.enum([
  'bool',
  'i64',
  'f32',
  'string',
  'bytes',
  'f32vec',
]);

/**
 * Valid key scopes.
 */
export const KeyScopeSchema = z.enum([
  'candidate',
  'feature',
  'score',
  'debug',
  'tmp',
  'penalty',
]);

/**
 * Schema for a single key definition.
 */
export const KeyDefSchema = z.object({
  /** Unique integer ID (must not be reused). */
  id: z
    .number()
    .int()
    .positive()
    .describe('Unique key ID (must be globally unique and never reused)'),

  /** Human-readable name (e.g., "score.base"). */
  name: z
    .string()
    .min(1)
    .regex(
      /^[a-z][a-z0-9]*(\.[a-z][a-z0-9_]*)*$/,
      'Key name must be lowercase with dots (e.g., "score.base")'
    )
    .describe('Human-readable key name'),

  /** Value type. */
  type: KeyTypeSchema.describe('Value type'),

  /** Scope for governance. */
  scope: KeyScopeSchema.describe('Scope for governance'),

  /** Owner team/group. */
  owner: z.string().min(1).describe('Owner team or group'),

  /** Documentation string. */
  doc: z.string().min(1).describe('Documentation string'),
});

/**
 * Schema for the full registry file.
 */
export const RegistrySchema = z.object({
  /** Registry version. */
  version: z
    .number()
    .int()
    .positive()
    .describe('Registry schema version'),

  /** List of key definitions. */
  keys: z.array(KeyDefSchema).describe('Key definitions'),
});

/**
 * Inferred types from schemas.
 */
export type KeyDefInput = z.input<typeof KeyDefSchema>;
export type KeyDef = z.output<typeof KeyDefSchema>;
export type RegistryInput = z.input<typeof RegistrySchema>;
export type Registry = z.output<typeof RegistrySchema>;

/**
 * Validation error with details.
 */
export interface ValidationError {
  path: (string | number)[];
  message: string;
}

/**
 * Validation result.
 */
export type ValidationResult =
  | { success: true; data: Registry }
  | { success: false; errors: ValidationError[] };

/**
 * Validate registry data against the schema.
 */
export function validateRegistry(data: unknown): ValidationResult {
  const result = RegistrySchema.safeParse(data);

  if (result.success) {
    return { success: true, data: result.data };
  }

  const errors: ValidationError[] = result.error.errors.map((err) => ({
    path: err.path,
    message: err.message,
  }));

  return { success: false, errors };
}

/**
 * Additional semantic validation (uniqueness, etc.).
 */
export function validateSemantics(registry: Registry): ValidationError[] {
  const errors: ValidationError[] = [];

  // Check for duplicate IDs
  const seenIds = new Map<number, number>();
  for (let i = 0; i < registry.keys.length; i++) {
    const key = registry.keys[i]!;
    const existingIndex = seenIds.get(key.id);
    if (existingIndex !== undefined) {
      errors.push({
        path: ['keys', i, 'id'],
        message: `Duplicate key ID ${key.id} (also at index ${existingIndex})`,
      });
    } else {
      seenIds.set(key.id, i);
    }
  }

  // Check for duplicate names
  const seenNames = new Map<string, number>();
  for (let i = 0; i < registry.keys.length; i++) {
    const key = registry.keys[i]!;
    const existingIndex = seenNames.get(key.name);
    if (existingIndex !== undefined) {
      errors.push({
        path: ['keys', i, 'name'],
        message: `Duplicate key name "${key.name}" (also at index ${existingIndex})`,
      });
    } else {
      seenNames.set(key.name, i);
    }
  }

  return errors;
}
