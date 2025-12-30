/**
 * Auto-generated key definitions from registry.yaml.
 * DO NOT EDIT - regenerate with `rankdsl codegen`.
 */
import type { Key, KeyType } from '@ranking-dsl/shared';
/**
 * Key type constants.
 */
export declare const KeyTypes: {
    readonly bool: "bool";
    readonly i64: "i64";
    readonly f32: "f32";
    readonly string: "string";
    readonly bytes: "bytes";
    readonly f32vec: "f32vec";
};
/**
 * Key handles for use in plan.js and njs modules.
 */
export declare const Keys: {
    /** Unique candidate identifier */
    readonly CAND_CANDIDATE_ID: {
        id: number;
        name: string;
        type: KeyType;
    };
    /** Freshness score in [0,1] */
    readonly FEAT_FRESHNESS: {
        id: number;
        name: string;
        type: KeyType;
    };
    /** Candidate embedding vector */
    readonly FEAT_EMBEDDING: {
        id: number;
        name: string;
        type: KeyType;
    };
    /** Query embedding vector */
    readonly FEAT_QUERY_EMBEDDING: {
        id: number;
        name: string;
        type: KeyType;
    };
    /** Base retrieval score from sourcer */
    readonly SCORE_BASE: {
        id: number;
        name: string;
        type: KeyType;
    };
    /** ML model prediction score */
    readonly SCORE_ML: {
        id: number;
        name: string;
        type: KeyType;
    };
    /** Score after adjustments (e.g., from njs nodes) */
    readonly SCORE_ADJUSTED: {
        id: number;
        name: string;
        type: KeyType;
    };
    /** Final ranking score used for ordering */
    readonly SCORE_FINAL: {
        id: number;
        name: string;
        type: KeyType;
    };
    /** Penalty for constraint violations */
    readonly PENALTY_CONSTRAINTS: {
        id: number;
        name: string;
        type: KeyType;
    };
    /** Penalty for diversity enforcement */
    readonly PENALTY_DIVERSITY: {
        id: number;
        name: string;
        type: KeyType;
    };
    /** JSON string of per-node timing information */
    readonly DEBUG_NODE_TIMINGS: {
        id: number;
        name: string;
        type: KeyType;
    };
};
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
export declare const KeysById: ReadonlyMap<number, Key>;
/**
 * Lookup key by name.
 */
export declare const KeysByName: ReadonlyMap<string, Key>;
//# sourceMappingURL=keys.d.ts.map