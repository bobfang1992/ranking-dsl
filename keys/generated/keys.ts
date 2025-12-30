/**
 * Auto-generated key definitions from registry.yaml.
 * DO NOT EDIT - regenerate with `rankdsl codegen`.
 */

/* eslint-disable */

import type { Key, KeyType } from '@ranking-dsl/shared';

/**
 * Key type constants.
 */
export const KeyTypes = {
  bool: 'bool',
  i64: 'i64',
  f32: 'f32',
  string: 'string',
  bytes: 'bytes',
  f32vec: 'f32vec',
} as const;

/**
 * Key handles for use in plan.js and njs modules.
 */
export const Keys = {
  /** Unique candidate identifier */
  CAND_CANDIDATE_ID: {
    id: 1001,
    name: 'cand.candidate_id',
    type: 'i64' as KeyType,
  } satisfies Key,

  /** Freshness score in [0,1] */
  FEAT_FRESHNESS: {
    id: 2001,
    name: 'feat.freshness',
    type: 'f32' as KeyType,
  } satisfies Key,

  /** Candidate embedding vector */
  FEAT_EMBEDDING: {
    id: 2002,
    name: 'feat.embedding',
    type: 'f32vec' as KeyType,
  } satisfies Key,

  /** Query embedding vector */
  FEAT_QUERY_EMBEDDING: {
    id: 2003,
    name: 'feat.query_embedding',
    type: 'f32vec' as KeyType,
  } satisfies Key,

  /** Base retrieval score from sourcer */
  SCORE_BASE: {
    id: 3001,
    name: 'score.base',
    type: 'f32' as KeyType,
  } satisfies Key,

  /** ML model prediction score */
  SCORE_ML: {
    id: 3002,
    name: 'score.ml',
    type: 'f32' as KeyType,
  } satisfies Key,

  /** Score after adjustments (e.g., from njs nodes) */
  SCORE_ADJUSTED: {
    id: 3003,
    name: 'score.adjusted',
    type: 'f32' as KeyType,
  } satisfies Key,

  /** Final ranking score used for ordering */
  SCORE_FINAL: {
    id: 3999,
    name: 'score.final',
    type: 'f32' as KeyType,
  } satisfies Key,

  /** Penalty for constraint violations */
  PENALTY_CONSTRAINTS: {
    id: 4001,
    name: 'penalty.constraints',
    type: 'f32' as KeyType,
  } satisfies Key,

  /** Penalty for diversity enforcement */
  PENALTY_DIVERSITY: {
    id: 4002,
    name: 'penalty.diversity',
    type: 'f32' as KeyType,
  } satisfies Key,

  /** JSON string of per-node timing information */
  DEBUG_NODE_TIMINGS: {
    id: 9001,
    name: 'debug.node_timings',
    type: 'string' as KeyType,
  } satisfies Key,

} as const;

/**
 * Type of the Keys object.
 */
export type KeysType = typeof Keys;

/**
 * Union of all key constant names.
 */
export type KeyName = keyof typeof Keys;

/**
 * Lookup key by ID.
 */
export const KeysById: ReadonlyMap<number, Key> = new Map([
  [1001, Keys.CAND_CANDIDATE_ID],
  [2001, Keys.FEAT_FRESHNESS],
  [2002, Keys.FEAT_EMBEDDING],
  [2003, Keys.FEAT_QUERY_EMBEDDING],
  [3001, Keys.SCORE_BASE],
  [3002, Keys.SCORE_ML],
  [3003, Keys.SCORE_ADJUSTED],
  [3999, Keys.SCORE_FINAL],
  [4001, Keys.PENALTY_CONSTRAINTS],
  [4002, Keys.PENALTY_DIVERSITY],
  [9001, Keys.DEBUG_NODE_TIMINGS],
]);

/**
 * Lookup key by name.
 */
export const KeysByName: ReadonlyMap<string, Key> = new Map([
  ['cand.candidate_id', Keys.CAND_CANDIDATE_ID],
  ['feat.freshness', Keys.FEAT_FRESHNESS],
  ['feat.embedding', Keys.FEAT_EMBEDDING],
  ['feat.query_embedding', Keys.FEAT_QUERY_EMBEDDING],
  ['score.base', Keys.SCORE_BASE],
  ['score.ml', Keys.SCORE_ML],
  ['score.adjusted', Keys.SCORE_ADJUSTED],
  ['score.final', Keys.SCORE_FINAL],
  ['penalty.constraints', Keys.PENALTY_CONSTRAINTS],
  ['penalty.diversity', Keys.PENALTY_DIVERSITY],
  ['debug.node_timings', Keys.DEBUG_NODE_TIMINGS],
]);
