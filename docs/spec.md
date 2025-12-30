# Embedded Ranking DSL + JS Nodes Spec (v0.2.4) — Key Registry (Key Handles) + JS Expr Sugar + Columnar Batches

This spec defines an embedded DSL for ranking pipelines where:

- **Plan files** (JavaScript) build a **Plan** (pure data).
- The **engine** (C++ today) compiles and executes the Plan.
- **Nodes** are implemented either in **C++** (core nodes) or **JavaScript** (njs nodes).
- The object model uses **immutable key-value maps**, and **all keys are Key Handles** from a **Key Registry** (no free-form string keys).
- Expressions can be authored either as **Expr IR** (AST builder) or as a restricted **JS expression** that is translated to Expr IR at compile time.
- Candidate batches are **abstract** at the API level but **SHOULD** be implemented internally as **columnar (Struct-of-Arrays / SoA)** for performance.

---

## 0. RFC Keywords

The key words **MUST**, **MUST NOT**, **REQUIRED**, **SHOULD**, **SHOULD NOT**, **MAY**, and **OPTIONAL** are to be interpreted as described in RFC 2119.

---

## 1. Terms

- **Plan**: A JSON-serializable description of a pipeline graph (DAG). Contains nodes and options only.
- **Node**: An operator in the graph. Implemented by either **C++** (core) or **JavaScript** (njs).
- **Key Registry**: Single source of truth of all allowed keys; each key has a stable **id** and metadata.
- **Key Handle**: A small typed object representing a key (includes `id`, `name`, `type`). Runtime uses `id`.
- **Value**: Typed scalar/bytes/vector value stored under a KeyId.
- **Obj (RowView)**: One candidate object; an immutable view over a single row in a CandidateBatch.
- **CandidateBatch**: A batch of candidates processed together. API is language-agnostic; internal layout is implementation-defined.
- **ColumnBatch (SoA)**: A CandidateBatch implementation where data is stored as columns (KeyId -> column array).
- **BatchBuilder**: A write-optimized builder used to produce a new immutable CandidateBatch from an input batch.
- **plan.js**: Default plan file; intended to stay simple; allows limited control flow for configuration selection.
- **.njs**: A JavaScript node module defining a node implementation in JS.
- **Expr IR**: JSON expression tree used by the engine to evaluate formulas during execution.
- **JS Expr Sugar**: A restricted JS expression syntax embedded in plan.js and translated to Expr IR at compile time.

---

## 2. Goals / Non-goals

### 2.1 Goals (REQUIRED)

1. **No string keys** at runtime: all reads/writes use Key Handles (stable integer ids).
2. **Two-phase design**: `plan.js` builds Plan only; engine executes.
3. **Pluggable nodes**: infra engineers implement and register core nodes (C++). JS nodes are allowed via njs modules.
4. **No arbitrary IO** in JS by default: no filesystem, network, process, or dynamic module loading for plan.js or njs at runtime. **Exception:** the engine MAY expose **explicit, policy-gated host IO capabilities** (default OFF) to specific njs modules.
5. **Governance**: enforce constraints via:
   - plan.js static gate (AST rules + complexity limits),
   - JS sandbox budgets,
   - Plan compile-time validation (shape + params + key usage),
   - code review policy for njs modules.
6. **Unified tracing/logging**: automatic per-node envelope spans, metrics, and optional sampled dumps.
7. **Expression authoring ergonomics**: allow a restricted JS expression form that compiles to Expr IR.
8. **Performance-ready data model**: CandidateBatch is abstract; implementations SHOULD be columnar (SoA) with immutable semantics.

### 2.2 Non-goals (v0.2.3)

- Distributed execution, cross-request caching, deep GPU/TPU optimization
- General-purpose formula parsing language (no arbitrary string-language parser)
- IDE plugins / advanced TS type inference

---

## 3. Repository Layout (Mono Repo)

All files MUST live in a single mono repo.

### 3.1 Recommended structure

```
repo/
  keys/
    registry.yaml
    generated/
      keys.ts
      keys.h
      keys.json
  plans/
    example_rank_pipeline.plan.js
  njs/
    foo/
      foo.njs
      README.md
  engine/
    cpp/
      ...
  tools/
    codegen/
    lint/
    cli/
    shared/
```

### 3.2 Governance via code review (REQUIRED)

- All changes to `njs/**` MUST go through code review.
- The repo MUST use `CODEOWNERS` such that `njs/**` requires approval from a designated group (e.g., Ranking Infra + Safety).
- `keys/registry.yaml` MUST also have owners and required reviewers.
- CI MUST run:
  - key registry validation + codegen consistency checks,
  - plan.js static gate,
  - njs module lint/validation (meta schema, key allowlists, budgets),
  - unit tests for node APIs / expression translation + eval.

---

## 4. Key Registry (Key Handles)

### 4.1 Registry file (REQUIRED)

`keys/registry.yaml` is the single source of truth.

Fields:

- `id` (REQUIRED): int32; MUST be globally unique and MUST NOT be reused.
- `name` (REQUIRED): unique string; used for readability/logs only (runtime uses id).
- `type` (REQUIRED): `bool | i64 | f32 | string | bytes | f32vec`
- `scope` (REQUIRED): `candidate | feature | score | debug | tmp`
- `owner` (REQUIRED): ownership tag (team/group)
- `doc` (REQUIRED): human-readable description

### 4.2 Registry governance rules (REQUIRED)

- Keys MUST be **additive**. Renaming MUST be done by adding a new key and deprecating the old one (deprecation fields MAY be added in v0.3).
- CI MUST validate:
  - unique `id`,
  - unique `name`,
  - valid `type` and `scope`.
- All pipeline code MUST reference keys through generated constants.

### 4.3 Code generation outputs (REQUIRED)

From `registry.yaml`, generate:

- `keys/generated/keys.ts`
- `keys/generated/keys.h`
- `keys/generated/keys.json`

No generated file may be hand-edited. CI MUST fail if generated outputs are stale.

---

## 5. Data Model (Immutable Semantics + Columnar-Friendly)

### 5.1 Value types (REQUIRED)

Supported runtime value types:

- `null`
- `bool`
- `i64`
- `f32`
- `string`
- `bytes`
- `f32vec` (vector<float32>)

### 5.2 CandidateBatch is an abstraction (REQUIRED)

At the node interface level, CandidateBatch MUST be treated as an abstract type with:

- `row_count` (N)
- ability to read values for a given key and row
- ability to produce a new batch with updated values (immutability semantics)

Engines MAY choose any physical layout.

### 5.3 Preferred physical layout: ColumnBatch (SoA) (SHOULD)

Engines SHOULD implement CandidateBatch internally as **columnar SoA**:

- `columns: Map<KeyId, Column>`
- Each Column is a typed array of length `N` (or a structured representation for bytes/vectors)
- Columns MAY have a null-mask (bitmap) or sentinel defaults (policy-defined)

This layout enables:

- vectorized operations (SIMD-friendly)
- efficient model inference (batch inputs)
- cheap immutability via column sharing

### 5.4 Row objects are views (REQUIRED)

The `Obj` exposed to JS nodes is a **RowView** into a CandidateBatch:

- `obj.get(Key)` reads the value at the row index from the underlying batch
- `obj.set(Key, value)` does not mutate the batch; it creates a new RowView (or a lightweight wrapper) that records the updated value for that row in a builder

Plan authors and njs authors MUST assume `Obj` is immutable and may be backed by columnar storage.

### 5.5 BatchBuilder for immutability (SHOULD)

To implement immutability efficiently, engines SHOULD use a BatchBuilder:

- On node execution, create `builder = BatchBuilder(input_batch)`
- `obj.set(key, value)` writes to `builder` at `(key_id, row_idx)`
- `builder` MAY also support whole-column writes (replacement columns) to enable columnar fast paths (e.g., njs `ctx.batch` writers).
- On completion, `builder.finish()` returns `output_batch`:
  - unchanged columns are **shared** (zero-copy)
  - updated columns are new arrays (or sparse overlays materialized)

This preserves immutable semantics while remaining fast.

### 5.6 Column representation notes (RECOMMENDED)

- `f32`, `i64`, `bool`: typed arrays with optional null-mask
- `string`, `bytes`: offset+data buffer or `vector<bytes>` (implementation-defined)
- `f32vec`:
  - RECOMMENDED: fixed-dimension vectors stored as one contiguous `float32[N * D]` with known `D`
  - row access returns a view (`subarray(row*D, (row+1)*D)`) without copying
  - future: allow optional metadata `vector_dim` in Key Registry for validation (not required in v0.2.3)

---

## 6. Plan.js (Plan Builder DSL)

### 6.1 Purpose

A `*.plan.js` file MUST evaluate to a Plan object (JSON-serializable). It MUST NOT execute ranking logic on real candidates.

### 6.2 Global objects injected into plan.js

- `dsl`: plan builder API
- `Keys`: Key Handles (generated)
- `config`: read-only configuration (deep-frozen)

### 6.3 Allowed control flow

Plan.js MAY use simple configuration-driven branching (if/switch). Complex logic belongs in njs nodes.

### 6.4 DSL API (REQUIRED)

#### Top-level

- `dsl.sourcer(name: string, params?: object) -> SourceRef`
- `dsl.candidates(sources: SourceRef[]) -> Pipeline`

#### Pipeline (immutable)

All pipeline methods return a new Pipeline:

- `Pipeline.features(keys: Key[], params?: object) -> Pipeline`
- `Pipeline.model(name: string, params?: object) -> Pipeline`
- `Pipeline.node(op: string, params?: object) -> Pipeline`
  - core nodes: `core:<name>`
  - js nodes: `js:<path>@<version>#<digest>` (recommended)
- `Pipeline.score(expr: ExprIR | JSExprToken, params?: object) -> Pipeline`
- `Pipeline.build(options?: object) -> Plan`

#### Plan options

`options.logging`:

- `sample_rate` (float 0..1)
- `dump_keys` (KeyId[])

---

## 7. Expressions — Two Authoring Modes

Expressions ultimately execute in the engine via Expr IR. v0.2.3 supports:

1. **Expr IR builder** (`dsl.F.*`)
2. **JS Expr Sugar** (`dsl.expr(() => ...)`) compiled to Expr IR

The canonical representation stored in Plan MUST be Expr IR.

---

## 8. Expr IR (Canonical)

Core ops:

- `const`, `signal(key_id)`, `add`, `mul`, `cos` (cosine similarity), `penalty(name)`
  Optional ops:
- `min`, `max`, `clamp`, `div` (policy-gated)

`cos(a,b)` semantics:

- cosine similarity between two `f32vec`
- missing/zero-norm → 0.0
- clamp result to [-1, 1]

---

## 9. JS Expr Sugar (Restricted JS Expression → Expr IR)

### 9.1 API surface (REQUIRED)

```js
p = p.score(
  dsl.expr(() => 0.7 * Keys.SCORE_BASE + 0.3 * Keys.SCORE_ML),
  { output_key_id: Keys.SCORE_FINAL.id }
);
```

The function MUST NOT be executed; it is a syntax container only.

### 9.2 Whitelisted function namespace (REQUIRED)

Function calls in JS Expr Sugar MUST be in `dsl.fn.*` (no bare `cos(...)`):

- `dsl.fn.cosSim(a,b)` (alias: `dsl.fn.cos`)
- `dsl.fn.min(...)`
- `dsl.fn.max(...)`
- `dsl.fn.clamp(...)` (optional)
- `dsl.fn.penalty("constraints")` (optional)

All other identifiers are disallowed.

---

## 10. Nodes

### 10.1 Core node interface (REQUIRED)

Nodes operate over CandidateBatch (abstract). Engines SHOULD implement it as ColumnBatch internally.

```cpp
struct NodeRunner {
  virtual CandidateBatch run(ExecContext&, const CandidateBatch& in) = 0;
};
```

### 10.2 njs nodes (.njs) (REQUIRED)

JS nodes operate on RowViews (Obj) backed by the underlying batch.

Module contract:

```js
export const meta = {
  name: "foo",
  version: "1.0.0",
  reads:  [Keys.SCORE_BASE],
  writes: [Keys.SCORE_FINAL],
  params: { alpha: { type: "number", min: 0, max: 1, default: 0.2 } },
  budget: { max_ms_per_1k: 2, max_set_per_obj: 10 }
};

export function runBatch(objs, ctx, params) { ... }
```

Governance:

- engine MUST enforce `meta.writes` at runtime (`obj.set` rejects disallowed keys)
- engine MUST enforce budgets

---



#### 10.2.1 Optional: Column/Batch APIs for njs (`ctx.batch`)

If the engine uses a columnar (SoA) CandidateBatch internally, njs nodes MAY use optional column APIs to reduce per-row overhead. This is a performance optimization; the default RowView (`obj.get/set`) path remains supported.

When enabled, the njs runtime SHOULD expose `ctx.batch`:

- Read APIs (views):
  - `ctx.batch.rowCount() -> number`
  - `ctx.batch.f32(key: Key) -> Float32Array` (read-only view)
  - `ctx.batch.f32vec(key: Key) -> { data: Float32Array, dim: number }` (read-only view)
- Write APIs (builder-backed; MUST NOT mutate input columns in-place):
  - `ctx.batch.writeF32(key: Key) -> Float32Array`
  - `ctx.batch.writeF32Vec(key: Key, dim: number) -> { data: Float32Array, dim: number }`

Governance requirements:

- All writes MUST be restricted to `meta.writes`; all writes MUST pass Key Registry type checks.
- Reads SHOULD be restricted to `meta.reads` (policy: strict or warn).
- If `ctx.batch` writers are used, `meta.budget` MAY additionally define:
  - `max_write_bytes` (total bytes written via column writers)
  - `max_write_cells` (total cells written via column writers)
- The engine MUST enforce these budgets (in addition to time/step limits and any existing row-level budgets).

`runBatch` return semantics (recommended):

- If `runBatch` returns an `Obj[]`, the engine uses row-level updates (RowView path).
- If `runBatch` returns `undefined`/`null`, the engine assumes column writers were used and commits via `builder.finish()`.



#### 10.2.2 Sandbox + Optional Host IO Capabilities (DEFAULT OFF)

By default, the njs runtime MUST NOT expose any general-purpose IO:

- no filesystem, network, process, shell, or arbitrary module loading
- no QuickJS std/os modules (or equivalents) exposed to user code

The engine MAY expose a limited `ctx.io` object ONLY when ALL conditions hold:

1. The module declares the capability in `meta.capabilities.io`.
2. The engine-side policy allowlists this module for IO capabilities (default deny).
3. The capability is implemented by the host (engine), not by JS.

Example:

```js
exports.meta = {
  name: "my_node",
  version: "1.0.0",
  reads: [/* ... */],
  writes: [/* ... */],
  capabilities: { io: { csv_read: true } },
  budget: {
    max_write_bytes: 1048576,
    max_write_cells: 100000,
    max_io_read_bytes: 1048576,
    max_io_read_rows: 100000
  }
};
```

When enabled, `ctx.io` exposes ONLY host-provided APIs (no raw FS). MVP IO surface:

- `ctx.io.readCsv(resource: string, opts?: object) -> { columns: object, rowCount: number }`

Restrictions (REQUIRED):

- `resource` MUST be resolved by the engine through a controlled resolver (e.g. allowlisted directory or registered asset name).
- absolute paths and path traversal (`".."`) MUST be rejected.
- the engine MUST enforce IO budgets (bytes/rows/time) and fail closed.
- if `ctx.io` is not enabled, it MUST be `undefined` and any attempt to use IO MUST fail.


## 11. Engine: Compile & Execute

Compile MUST validate:

- plan version
- DAG acyclic
- ops resolvable
- params valid
- key ids exist
- translate JSExprToken → Expr IR

Execute MUST:

- run nodes in topo order
- emit structured logs and error codes

---

## 12. Logging & Tracing (REQUIRED)

Emit JSON lines:

- `node_start`, `node_end` with duration, rows_in/out, error_code
  Sample dump reads from columnar batch and prints selected dump_keys for topN rows.

---

## 13. MVP Core Nodes (Recommended baseline)

1. `core:sourcer`
2. `core:merge`
3. `core:features`
4. `core:model`
5. `core:score_formula` (Expr IR eval)

Core nodes SHOULD use columnar APIs internally for performance.

---

## 14. Future work (v0.3+)

- Deprecated keys and migration tooling
- Stronger schema inference (reads/writes)
- Arrow-compatible ColumnBatch
- Deterministic execution modes
- Safer i64 handling in JS

---