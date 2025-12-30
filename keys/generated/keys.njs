/**
 * Auto-generated key definitions from registry.yaml.
 * DO NOT EDIT - regenerate with `rankdsl codegen`.
 *
 * CommonJS module for use in .njs files.
 */

exports.Keys = {
  /** Unique candidate identifier (i64) */
  CAND_CANDIDATE_ID: 1001,

  /** Freshness score in [0,1] (f32) */
  FEAT_FRESHNESS: 2001,

  /** Candidate embedding vector (f32vec) */
  FEAT_EMBEDDING: 2002,

  /** Query embedding vector (f32vec) */
  FEAT_QUERY_EMBEDDING: 2003,

  /** Base retrieval score from sourcer (f32) */
  SCORE_BASE: 3001,

  /** ML model prediction score (f32) */
  SCORE_ML: 3002,

  /** Score after adjustments (e.g., from njs nodes) (f32) */
  SCORE_ADJUSTED: 3003,

  /** Final ranking score used for ordering (f32) */
  SCORE_FINAL: 3999,

  /** Penalty for constraint violations (f32) */
  PENALTY_CONSTRAINTS: 4001,

  /** Penalty for diversity enforcement (f32) */
  PENALTY_DIVERSITY: 4002,

  /** JSON string of per-node timing information (string) */
  DEBUG_NODE_TIMINGS: 9001,

};

exports.KeyInfo = {
  CAND_CANDIDATE_ID: {
    id: 1001,
    name: 'cand.candidate_id',
    type: 'i64'
  },
  FEAT_FRESHNESS: {
    id: 2001,
    name: 'feat.freshness',
    type: 'f32'
  },
  FEAT_EMBEDDING: {
    id: 2002,
    name: 'feat.embedding',
    type: 'f32vec'
  },
  FEAT_QUERY_EMBEDDING: {
    id: 2003,
    name: 'feat.query_embedding',
    type: 'f32vec'
  },
  SCORE_BASE: {
    id: 3001,
    name: 'score.base',
    type: 'f32'
  },
  SCORE_ML: {
    id: 3002,
    name: 'score.ml',
    type: 'f32'
  },
  SCORE_ADJUSTED: {
    id: 3003,
    name: 'score.adjusted',
    type: 'f32'
  },
  SCORE_FINAL: {
    id: 3999,
    name: 'score.final',
    type: 'f32'
  },
  PENALTY_CONSTRAINTS: {
    id: 4001,
    name: 'penalty.constraints',
    type: 'f32'
  },
  PENALTY_DIVERSITY: {
    id: 4002,
    name: 'penalty.diversity',
    type: 'f32'
  },
  DEBUG_NODE_TIMINGS: {
    id: 9001,
    name: 'debug.node_timings',
    type: 'string'
  },
};
