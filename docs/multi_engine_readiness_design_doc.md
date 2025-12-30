# Multi-Engine Readiness Design Doc (Go/Rust Future-Proofing)

**Status:** Draft (ready to execute)  
**Primary engine today:** C++20  
**Future goal:** Provide additional engine implementations in Go and/or Rust without changing DSL semantics.

---

## 1) Background

We are building an embedded ranking DSL system:

- `*.plan.js` builds a **Plan** (pure data; JSON-serializable).
- The **engine** compiles and executes the Plan as a DAG of nodes.
- Nodes are implemented in:
  - **C++ core nodes** (performance/infra owned)
  - **JS nodes (.njs)** for controlled “escape hatch” logic (no IO), governed by code review and runtime enforcement.
- Data model is an immutable per-candidate object: **Obj = KeyId → Value**, where KeyId comes from a **Key Registry** (no free-form strings).
- Score formulas use canonical **Expr IR**; there is optional **JS Expr Sugar** that compiles to Expr IR.

We want the option to implement the engine in **Go or Rust** later, while keeping the ecosystem stable:
- The same plan file produces the same results across engines (within defined tolerances).
- Key registry and expression semantics remain unchanged.
- Tooling remains usable and consistent.

---

## 2) Design Goals

### 2.1 Primary goals
1. **Language-agnostic contracts:** Plan IR, Expr IR, Key Registry are stable, versioned, and independent of engine language.
2. **Behavioral consistency:** the same inputs must yield the same outputs across engines.
3. **Conformance suite:** a shared set of golden tests for correctness across engines.
4. **Pluggability:** node boundaries are stable (batch-first interface), enabling re-implementation per language.
5. **Operational consistency:** structured logs + error codes are consistent across engines.

### 2.2 Non-goals (for now)
- Distributed execution / streaming pipelines
- Cross-request caching and advanced scheduling
- Full Arrow/columnar runtime in MVP (we can leave hooks)
- Untrusted arbitrary user-submitted plan execution (plan.js is repo-controlled)

---

## 3) Key Insight: Treat the Engine as a “Backend” for a Stable IR

To add Go/Rust engines later with minimal pain, **freeze the IR contracts early**:
- **Key Registry** (stable KeyId + meta)
- **Plan IR** (node graph + params)
- **Expr IR** (canonical expression tree)

Everything else (C++/Go/Rust executor, node implementations, storage layout) becomes replaceable.

---

## 4) Canonical Contracts to Freeze Now

### 4.1 Key Registry (SSOT)
- Source: `keys/registry.yaml`
- Generated artifacts:
  - `keys.ts` (tooling / plan authoring)
  - `keys.h` (C++ convenience)
  - `keys.json` (runtime meta for validation in any engine)

**Rules to freeze now**
- KeyId is **permanent** and **never reused**.
- Types: `bool | i64 | f32 | string | bytes | f32vec`.
- Scope: `candidate | feature | score | debug | tmp`.
- Additive-only changes; rename = new key + deprecate old.

### 4.2 Plan IR (DAG)
Plan MUST remain JSON-serializable. It describes:
- Nodes: `{id, op, inputs, params}`
- Options: logging, dumps, etc.

**Rules to freeze now**
- Node op naming: `core:<name>` and `js:<path>@<ver>#<digest>` (digest strongly recommended).
- Version field at top-level: `plan.version`.

### 4.3 Expr IR (canonical)
Expr IR is the single canonical representation executed by all engines.
JS Expr Sugar is *only* a frontend convenience that compiles to Expr IR.

**Freeze now**
- Core ops: `const`, `signal(key_id)`, `add`, `mul`, `cos` (cosine similarity), `penalty(name)` (or a key-based penalty rule).
- Explicit typing rules (see §5).

---

## 5) Semantics That Must Match Across Engines

This is the #1 place multi-engine efforts fail. Define these explicitly now.

### 5.1 Missing / null values
Pick one policy and freeze it:
- **Recommended MVP policy:** missing key returns `null`; arithmetic with `null` is an error **unless explicitly defaulted**.
  - (Alternate policy: treat missing as 0.0; more robust but can hide issues.)

If you prefer robustness early, choose:
- missing `f32` → 0.0
- missing `f32vec` → treat as missing; cosSim returns 0.0
…but document it as a strict policy and test it.

### 5.2 Floating point
- Expr IR semantics are **f32**.
- Engines MAY compute internally with f64 for stability, but the final stored value MUST match f32 semantics (with defined tolerance in conformance tests).

### 5.3 `cos(a,b)` = cosine similarity
- Inputs: both must be `f32vec`.
- If missing vector or zero norm: return **0.0**.
- Clamp to `[-1, 1]`.

### 5.4 `penalty`
To avoid ambiguity, freeze a deterministic MVP rule:
- **Option A (recommended):** `penalty("constraints")` reads `penalty.constraints` key (Key Registry), missing → 0.0
- Option B: custom engine-level function based on constraint state (more complex)

### 5.5 Sorting / tie-break
If you output top-N, define stable ordering:
- Sort by `score.final` descending
- Ties break by `cand.candidate_id` ascending

### 5.6 Error model
Define stable error codes (used by all engines and tooling):
- `E_PLAN_INVALID_VERSION`
- `E_PLAN_CYCLE_DETECTED`
- `E_KEY_NOT_FOUND`
- `E_EXPR_DISALLOWED_SYNTAX`
- `E_EXPR_TYPE_MISMATCH`
- `E_NODE_PARAMS_INVALID`
- `E_NJS_WRITE_KEY_NOT_ALLOWED`
- etc.

---

## 6) Conformance Suite (Golden Tests)

To make multi-engine practical, build a conformance suite now.

### 6.1 What it tests
- Plan compile validation (keys exist, DAG acyclic, params valid)
- Expr IR evaluation correctness
- Node behavior for a small standard library of nodes
- Logs/errors stability (at least error codes)

### 6.2 How it works
- `conformance/inputs/*.json`:
  - fixed candidate batches (objects with key_id → value)
  - fixed feature store snapshots (if needed)
  - fixed configs
- `conformance/plans/*.json`:
  - compiled Plan IR (or plan.js + deterministic compile)
- `conformance/expected/*.json`:
  - topN outputs with key_id values
  - (optional) full batch outputs

### 6.3 Running it
Each engine exposes a CLI:
- `engine_cxx run --plan <plan.json> --input <input.json> --output <out.json>`
- `engine_rust run ...`
- `engine_go run ...`

Then a shared harness compares outputs (with tolerances for floats).

**Recommendation:** define float comparison tolerance now:
- abs <= 1e-5 OR rel <= 1e-5 (tune later)

---

## 7) Batch-First Node Interface (Language-Agnostic)

To support parallelism and efficient runtimes, standardize on batch-first nodes.

### 7.1 Core shape
- Input: `CandidateBatch` (list of Obj)
- Output: `CandidateBatch` (same cardinality, new immutable objects)

### 7.2 Reads/Writes (future tightening)
Even if not fully enforced in MVP, define a place for it:
- nodes declare `reads` and `writes` key sets
- compiler can do missing-key detection and governance

### 7.3 Obj operations
Minimum operations across engines:
- `get(KeyId) → Value?`
- `set(KeyId, Value) → Obj` (immutable)
- (optional) `has`, `del`

---

## 8) Observability Contract (Logs + Error Codes)

Multi-engine ops become painful if logs diverge. Freeze a minimal structured log schema.

### 8.1 Node spans
Emit JSON lines:
- `event`: `node_start` | `node_end`
- `plan_name`, `node_id`, `op`
- `duration_ms`, `rows_in`, `rows_out`
- `error_code` (optional)
- `engine`: `cxx|go|rust`
- `build_sha` (optional)

### 8.2 Sample dumps
If sampled, dump:
- `event: sample_dump`
- `dump_keys: [key_id...]`
- `topN: [{key_id: value, ...}, ...]`

---

## 9) njs Runner as a Replaceable Module

Even if C++ uses QuickJS, define a stable boundary so Go/Rust can implement it later.

### 9.1 Stable .njs module contract (freeze)
- exports: `meta` with `reads`, `writes`, `params`, `budget`
- exports: `runBatch(objs, ctx, params)` (preferred) OR `run(obj, ctx, params)`
- `obj.set` MUST enforce writes allowlist
- budgets: step/time/memory as policy

### 9.2 Engine boundary
Each engine provides:
- Obj wrapper: `get/set` with validation
- ctx: logging/counters
- budget enforcement

The .njs file itself does **not** change across engines.

---

## 10) Decisions You Can Execute Now (No Further Debate Needed)

These are low-risk and unblock later engines:

1. **Formal schemas**
   - Add JSON Schema (or Proto) for:
     - Key Registry JSON (`keys.json`)
     - Plan IR (`plan.json`)
     - Expr IR (`expr_ir.json`)
2. **Conformance harness**
   - Create `conformance/` with at least:
     - 2 plans
     - 2 input datasets
     - golden outputs
3. **Batch-first node interface**
   - Ensure core nodes and njs nodes are batch-first in API.
4. **Logs + error codes**
   - Define `error_codes.md` and `logging_schema.md` (or embed in spec)
5. **njs runner modularization**
   - Implement `JsNodeRunner` behind an interface in C++.
   - Do not scatter QuickJS calls outside that module.

---

## 11) Execution Plan (Simple, Practical)

### Phase A — IR Contracts & Conformance (1–2 days)
- [ ] Add JSON Schema files for Plan/Expr/KeyMeta.
- [ ] Add `conformance/` directory and the harness:
  - compare outputs with float tolerance
  - run against C++ engine (once available)
- [ ] Define `error_codes.md` and log fields.

### Phase B — C++ Engine MVP (existing plan)
- [ ] Compile + execute basic plan DAG
- [ ] Implement core nodes: sourcer/merge/features/model/score_formula
- [ ] Implement Expr IR eval (cosSim semantics frozen)
- [ ] Implement njs runner module with writes enforcement + budget

### Phase C — Prepare for Go/Rust engines (low cost groundwork)
- [ ] Keep all canonical artifacts in IR form (JSON/proto), with versioning.
- [ ] Ensure C++ engine CLI can run conformance suite (stable IO contract).
- [ ] Avoid C++-specific “leaks” into Plan IR (no pointers, no ABI assumptions).

### Phase D — Rust/Go engine prototype (future, optional)
- [ ] Implement Plan/Expr IR parsing and conformance runner
- [ ] Re-implement minimal core nodes
- [ ] Pass conformance suite first, then optimize storage/layout

---

## 12) Open Questions (Record, but don’t block Phase 1/2)

- Missing-value semantics (strict error vs default-to-zero): pick and lock early.
- `i64` representation in JS: number vs BigInt. (Engines can be strict; plan authoring just references keys.)
- Columnar runtime: when to migrate from KV maps to Arrow-like batches.

---

## Appendix: Recommended Defaults

- `cosSim`: return 0 on missing/zero norm; clamp [-1,1]
- `penalty(name)`: read `penalty.<name>` key, missing → 0
- float tolerance for conformance: abs<=1e-5 or rel<=1e-5
- tie-break: candidate_id ascending

---
