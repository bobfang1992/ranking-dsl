# Ranking DSL Implementation Plan

## Overview

This document outlines the implementation plan for an embedded DSL for ranking pipelines, based on `docs/spec.md` (v0.2.4).

**Architecture Summary:**
- **Plan files** (`*.plan.js`) build a Plan (pure JSON data) using a DSL
- **Engine** (C++) compiles and executes the Plan
- **Nodes** are implemented in C++ (core) or JavaScript (njs)
- **Key Registry** provides type-safe key handles (no free-form string keys)
- **Expressions** authored as Expr IR or JS Expr Sugar (restricted JS → Expr IR)

---

## Development Workflow

**All changes must go through Pull Requests:**
1. Create a feature branch from `main`
2. Make changes and commit
3. Push branch and create PR
4. Wait for CI to pass
5. Merge PR to `main`

```bash
# Example workflow
git checkout main && git pull
git checkout -b feature/my-change
# ... make changes ...
git add -A && git commit -m "feat: description"
git push -u origin feature/my-change
gh pr create --fill
# After CI passes and review, merge via GitHub
```

---

## Key Design Decisions

| Decision | Choice |
|----------|--------|
| Tooling language | TypeScript (Node.js) |
| Engine language | C++20 |
| C++ formatting/logging | `fmt` library |
| Tooling tests | Vitest |
| C++ tests | Catch2 |
| Division in MVP | Excluded (reject `/` in translator, `div` reserved but not implemented) |
| JS Expr function calls | **Must use `dsl.fn.*` namespace** (no bare `cos()`, `min()`, etc.) |
| `cos(a,b)` semantics | Cosine similarity: `dot(a,b) / (‖a‖ * ‖b‖)`, returns 0 if norm is zero, clamps to [-1,1] |
| JS Expr token binding | **AST rewrite with spanId injection** (see High-Risk Areas) |
| JS Expr translation | **Compile-time via keys.json** - NOT runtime inspection (see below) |
| Key registry runtime | C++ engine loads `keys.json` at runtime for validation |
| Penalty semantics | Read penalty value from a designated penalty key in registry |
| Obj implementation | **RowView** over `(ColumnBatch, rowIndex)` - immutable semantics via BatchBuilder |
| Internal data model | **Columnar (SoA)**: Typed columns (`F32Column`, `I64Column`, `F32VecColumn`) |
| f32vec storage | **Contiguous N×D** with dimension metadata, NOT per-row vectors |
| njs JS runtime | **QuickJS** (simpler step limits, smaller footprint) |
| Builder finalize method | **`build()`** everywhere (not `finish()`)

---

## High-Risk Areas (Address First)

### 1. JS Expr Sugar: Stable Binding via AST Rewrite

**Problem:** If we map `dsl.expr()` tokens to AST nodes by call order, it breaks when plan.js has branching (if/else), since runtime call order ≠ source order.

**Solution: AST rewrite to inject stable spanId**

```
┌─────────────────────────────────────────────────────────────────┐
│  Original plan.js                                               │
│  ─────────────────                                              │
│  p = p.score(dsl.expr(() => 0.7 * Keys.SCORE_BASE), {...});     │
│                                                                 │
│  ↓ Parse with Babel (preserve loc/range)                        │
│                                                                 │
│  ↓ Find all dsl.expr(() => <expr>) calls                        │
│                                                                 │
│  ↓ Compute spanId = `${start}:${end}` or hash                   │
│                                                                 │
│  ↓ Rewrite to: dsl.expr("42:89", () => 0.7 * Keys.SCORE_BASE)   │
│                                                                 │
│  Rewritten plan.js (in memory)                                  │
│  ─────────────────────────────                                  │
│  p = p.score(dsl.expr("42:89", () => 0.7 * Keys.SCORE_BASE));   │
│                                                                 │
│  ↓ Execute rewritten code → Plan with JSExprToken               │
│                                                                 │
│  JSExprToken = { kind: "js_expr_token", spanId: "42:89" }       │
│                                                                 │
│  ↓ During compile: use spanId to find AST node → translate      │
│                                                                 │
│  Expr IR = { "op": "mul", "args": [...] }                       │
└─────────────────────────────────────────────────────────────────┘
```

**Implementation:**
1. Parse original plan.js with `@babel/parser` (with `ranges: true`, `loc: true`)
2. Walk AST to find all `dsl.expr(ArrowFunctionExpression)` calls
3. For each, compute `spanId` from the ArrowFunction's `start:end` range
4. Use `@babel/generator` to emit rewritten source with spanId injected as first arg
5. Execute rewritten source in VM → Plan contains `JSExprToken` with `spanId`
6. At compile time, use `spanId` to locate exact AST node, translate to Expr IR
7. Errors include precise source spans from original file

**Benefits:**
- Correct mapping across all branching patterns
- Perfect source spans for error reporting
- No user-visible verbosity

### 2. JS Expr Sugar: No Bare Function Calls

**Enforcement:** The expression translator must reject any function call that is not namespaced under `dsl.fn.*`.

| Allowed | Rejected |
|---------|----------|
| `dsl.fn.cosSim(a, b)` | `cosSim(a, b)` |
| `dsl.fn.cos(a, b)` | `cos(a, b)` |
| `dsl.fn.min(a, b)` | `min(a, b)` |
| `dsl.fn.max(a, b)` | `max(a, b)` |
| `dsl.fn.clamp(x, lo, hi)` | `clamp(x, lo, hi)` |
| `dsl.fn.penalty("name")` | `penalty("name")` |

**Rationale:** Prevents name shadowing, keeps expression surface deterministic and auditable.

### 2b. JS Expr Translation: Compile-Time via keys.json

**CRITICAL:** Expression translation happens at compile time using only:
1. Source AST (from plan.js)
2. `keys/generated/keys.json` (registry mapping)

**NOT** by inspecting runtime `Keys` object values.

```
┌─────────────────────────────────────────────────────────────────────────────┐
│  CORRECT: Compile-time translation                                           │
│  ─────────────────────────────────                                           │
│  Source: Keys.SCORE_BASE                                                     │
│  ↓ AST node: MemberExpression { object: "Keys", property: "SCORE_BASE" }     │
│  ↓ Lookup "SCORE_BASE" in keys.json → { id: 3001, type: "f32", ... }         │
│  ↓ Emit: { "op": "signal", "key_id": 3001 }                                  │
│                                                                              │
│  WRONG: Runtime inspection                                                   │
│  ────────────────────────────                                                │
│  Execute code → Keys.SCORE_BASE evaluates to 3001 → emit signal(3001)        │
│  ✗ Breaks when plan.js has branching, dead code, or conditional Keys refs    │
└─────────────────────────────────────────────────────────────────────────────┘
```

**Implementation:**
- Translator receives: spanId → AST node
- Translator loads: `keys.json` at startup
- For `MemberExpression` where `object.name === "Keys"`:
  - Extract `property.name` (e.g., "SCORE_BASE")
  - Lookup in keys.json by name → get `id` and `type`
  - Emit `{ "op": "signal", "key_id": <id> }`
- For unknown key names: error with source span

### 3. VM Sandbox Threat Model

**MVP Threat Model:**
- `plan.js` is **trusted code** (code-reviewed, checked into monorepo)
- `njs` modules are **trusted code** (code-reviewed, CODEOWNERS enforced)
- Node `vm` module with frozen globals is **not a security boundary**
- Static gate + code review are the primary controls

**What we do:**
- Freeze all globals (`dsl`, `Keys`, `config`)
- Disable `eval`, `Function` constructor
- Static gate rejects IO, imports, user-defined functions

**What we don't do (MVP):**
- No true sandbox isolation for untrusted input
- No separate process / QuickJS isolate / OS sandbox

**Future (if needed):**
- For untrusted plan.js: use separate process, QuickJS isolate, or OS-level sandbox

### 4. njs Runner with Columnar APIs (v0.2.4)

njs is a core escape hatch for custom logic. MVP must include a working implementation with both row-level and column-level APIs.

**MVP njs Requirements:**
- [ ] Load `.njs` module from `njs/**` path
- [ ] Parse and validate `meta` object (name, version, reads, writes, params, budget)
- [ ] Call `runBatch(objs, ctx, params)` (primary) or `run(obj, ctx, params)` (fallback)
- [ ] **Enforce `meta.writes`**: `obj.set(key, v)` fails if `key` not in `meta.writes`
- [ ] **Enforce budget**: `max_ms_per_1k`, `max_set_per_obj`, `max_write_bytes`, `max_write_cells`
- [ ] Provide columnar APIs via `ctx.batch`
- [ ] Handle return semantics: `Obj[]` vs `undefined/null`

**njs Module Contract (Row-Level):**
```js
export const meta = {
  name: "example",
  version: "1.0.0",
  reads:  [Keys.SCORE_BASE],
  writes: [Keys.SCORE_ADJUSTED],
  params: { alpha: { type: "number", min: 0, max: 1, default: 0.5 } },
  budget: { max_ms_per_1k: 5, max_set_per_obj: 10, max_write_bytes: 1048576, max_write_cells: 100000 }
};

// Row-level API: return modified Obj array
export function runBatch(objs, ctx, params) {
  return objs.map(obj =>
    obj.set(Keys.SCORE_ADJUSTED, obj.get(Keys.SCORE_BASE) * params.alpha)
  );
}
```

**njs Module Contract (Column-Level):**
```js
export const meta = {
  name: "column_example",
  version: "1.0.0",
  reads:  [Keys.SCORE_BASE, Keys.FEAT_EMBEDDING],
  writes: [Keys.SCORE_ADJUSTED],
  budget: { max_write_bytes: 1048576, max_write_cells: 100000 }
};

// Column-level API: use ctx.batch for efficient vectorized operations
export function runBatch(objs, ctx, params) {
  const n = ctx.batch.rowCount();

  // Read column as Float32Array (read-only view)
  const scoreBase = ctx.batch.f32(Keys.SCORE_BASE);  // Float32Array[n]

  // Allocate writable column backed by BatchBuilder
  const scoreAdj = ctx.batch.writeF32(Keys.SCORE_ADJUSTED);  // Float32Array[n]

  // Vectorized operation
  for (let i = 0; i < n; i++) {
    scoreAdj[i] = scoreBase[i] * params.alpha;
  }

  // Return undefined to signal column writers were used
  // Engine will call builder.build() to finalize
  return undefined;
}
```

**ctx.batch API:**

| Method | Description |
|--------|-------------|
| `rowCount()` | Returns number of rows in batch |
| `f32(key)` | Read-only `Float32Array` view of f32 column |
| `f32vec(key)` | Read-only array of `Float32Array` views (for f32vec column, one per row) |
| `i64(key)` | Read-only `BigInt64Array` view of i64 column |
| `writeF32(key)` | Allocate writable `Float32Array` backed by BatchBuilder |
| `writeF32Vec(key, dim)` | Allocate writable f32vec column with given dimension |
| `writeI64(key)` | Allocate writable `BigInt64Array` backed by BatchBuilder |

**Write Enforcement:**
- All `write*` methods check `key` against `meta.writes` - throws if not allowed
- All `write*` methods check key type against registry - throws on type mismatch
- Budget tracking: `max_write_cells` counts total cells written, `max_write_bytes` counts total bytes

**runBatch Return Semantics:**
```
┌─────────────────────────────────────────────────────────────────────────────┐
│  runBatch returns Obj[]                                                      │
│  ─────────────────────                                                       │
│  → Row-level updates via obj.set()                                           │
│  → Each returned Obj has its own changes                                     │
│  → Engine merges changes row by row                                          │
│                                                                              │
│  runBatch returns undefined/null                                             │
│  ───────────────────────────────                                             │
│  → Column writers (writeF32, etc.) were used                                 │
│  → Engine calls builder.build() directly to finalize                         │
│  → More efficient for batch operations (no per-row overhead)                 │
└─────────────────────────────────────────────────────────────────────────────┘
```

**Budget Enforcement:**
```
budget: {
  max_ms_per_1k: 5,         // Wall-clock time limit per 1000 rows
  max_set_per_obj: 10,      // Max obj.set() calls per row (row-level API)
  max_write_bytes: 1048576, // Max bytes allocated via write* APIs (column-level)
  max_write_cells: 100000   // Max cells (individual values) written via write* APIs
}
```

### 5. Columnar Data Model (SoA) with Typed Columns

**Goal:** Keep public semantics (immutable `Obj.set()` returns new Obj) but internally use typed columnar layout for:
- Cache efficiency
- Column sharing (COW)
- **Zero-copy views for njs** (ctx.batch.f32 → Float32Array)

**CRITICAL: Typed Columns, Not vector<Value>**

To support zero/low-copy `Float32Array` views in JS, columns must use typed storage:

```
┌─────────────────────────────────────────────────────────────────────────────┐
│  TypedColumn (abstract base)                                                │
│  ───────────────────────────                                                │
│  F32Column  { data: vector<float> }        // contiguous f32 storage        │
│  I64Column  { data: vector<int64_t> }      // contiguous i64 storage        │
│  BoolColumn { data: vector<bool> }         // bool storage                  │
│  StrColumn  { data: vector<string> }       // string storage                │
│  F32VecColumn {                                                             │
│    data: vector<float>   // contiguous N×D storage (row-major)              │
│    dim: size_t           // dimension per row                               │
│  }                                                                          │
│                                                                             │
│  ColumnBatch {                                                              │
│    columns: Map<KeyId, shared_ptr<TypedColumn>>  // shared for COW         │
│    row_count: size_t                                                        │
│                                                                             │
│    // Typed accessors (fast path)                                           │
│    GetF32Column(key_id) → F32Column*                                        │
│    GetI64Column(key_id) → I64Column*                                        │
│    GetF32VecColumn(key_id) → F32VecColumn*                                  │
│                                                                             │
│    // Generic accessor (slower, for RowView)                                │
│    GetValue(row_index, key_id) → Value                                      │
│  }                                                                          │
│                                                                             │
│  RowView {                                                                  │
│    batch: const ColumnBatch*                                                │
│    builder: BatchBuilder*   // null for read-only views                     │
│    row_index: size_t                                                        │
│                                                                             │
│    get(key_id) → Value      // reads via typed column                       │
│    set(key_id, val) → RowView  // writes via builder (COW)                  │
│  }                                                                          │
│                                                                             │
│  BatchBuilder {                                                             │
│    source: const ColumnBatch*                                               │
│    modified: Map<KeyId, shared_ptr<TypedColumn>>                            │
│                                                                             │
│    build() → ColumnBatch:                                                   │
│      result.columns = source.columns  // share unmodified                   │
│      for (key, col) in modified:                                            │
│        result.columns[key] = col      // own modified                       │
│      return result                                                          │
│  }                                                                          │
└─────────────────────────────────────────────────────────────────────────────┘
```

**f32vec Storage: Contiguous N×D**

```
┌─────────────────────────────────────────────────────────────────────────────┐
│  CORRECT: F32VecColumn (contiguous)                                          │
│  ─────────────────────────────────                                           │
│  data: [r0d0, r0d1, r0d2, r1d0, r1d1, r1d2, r2d0, r2d1, r2d2]  // N=3, D=3  │
│  dim: 3                                                                      │
│                                                                              │
│  ctx.batch.f32vec(key) → { data: Float32Array, dim: 3 }                      │
│  Access row i: data.subarray(i * dim, (i + 1) * dim)                         │
│                                                                              │
│  WRONG: Per-row vectors                                                      │
│  ─────────────────────                                                       │
│  data: [Float32Array[3], Float32Array[3], Float32Array[3]]  // N pointers!   │
│  ✗ No zero-copy view possible, requires N allocations                        │
└─────────────────────────────────────────────────────────────────────────────┘
```

**Zero-Copy JS Integration:**

```cpp
// C++ side: expose raw pointer to QuickJS
F32Column* col = batch.GetF32Column(key_id);
float* data = col->Data();  // contiguous
size_t size = col->Size();

// QuickJS side: wrap as Float32Array (no copy)
JSValue arr = JS_NewFloat32Array(ctx, data, size);  // view, not copy
```

**Invariants:**
- `RowView.set()` returns a new `RowView` (same batch, same row_index, writes via builder)
- Original `ColumnBatch` is never mutated
- `BatchBuilder.build()` produces a new `ColumnBatch` that shares unchanged columns
- Core nodes can operate directly on typed columns (vectorized reads/writes)
- njs ctx.batch APIs expose raw typed arrays (zero-copy when possible)

**Column Sharing Example:**
```
Input batch: { columns: { K1: F32Column[a,b,c], K2: I64Column[x,y,z] } }

score_formula writes K3 (new F32 column):
  Output batch: { K1: shared, K2: shared, K3: F32Column[p,q,r] }
  Memory: K1 and K2 are NOT copied.

njs node writes K2[1] via ctx.batch.writeI64:
  Output batch: { K1: shared, K2: I64Column[x,Y,z], K3: shared }
  Memory: K2 is copied (COW), K1 and K3 are shared.
```

**Benefits:**
- Cache-efficient typed columnar layout
- Zero-copy `Float32Array` views for njs
- Minimal memory allocation when adding new columns
- Unchanged columns shared across pipeline stages
- Vectorized SIMD operations possible for core nodes

### 6. Clarifications Locked Down

| Topic | Decision |
|-------|----------|
| **Registry source of truth** | `keys/registry.yaml` → generates `keys.json`, `keys.ts`, `keys.h`. C++ engine loads `keys.json` at runtime for validation. `keys.h` is optional convenience for compile-time constants in core nodes. |
| **Penalty semantics** | MVP: `penalty("name")` reads from a designated penalty key in registry (e.g., `penalty.{name}`). If key missing or value is null, return 0.0. |
| **Obj implementation** | `RowView` over `(ColumnBatch, rowIndex)`. Immutability via `BatchBuilder` COW semantics. |
| **Division** | `/` rejected in translator with `E_EXPR_DISALLOWED_SYNTAX`. `div` op reserved in schema but not implemented in eval. |

---

## Repository Structure

```
ranking-dsl/
├── CLAUDE.md                 # This file
├── docs/
│   └── spec.md               # Specification
├── keys/
│   ├── registry.yaml         # Key registry (source of truth)
│   └── generated/
│       ├── keys.ts           # Generated TypeScript constants
│       ├── keys.h            # Generated C++ header
│       └── keys.json         # Generated JSON (engine loads this)
├── plans/
│   └── example.plan.js       # Example plan file
├── njs/
│   └── example/
│       ├── example.njs       # Example JS node
│       └── README.md
├── tools/
│   ├── codegen/              # Key registry → generated files
│   │   ├── src/
│   │   │   ├── index.ts
│   │   │   ├── parser.ts     # YAML parser + validation
│   │   │   ├── generators/
│   │   │   │   ├── typescript.ts
│   │   │   │   ├── cpp.ts
│   │   │   │   └── json.ts
│   │   │   └── schema.ts     # Registry schema validation
│   │   └── tests/
│   ├── lint/                 # plan.js static gate
│   │   ├── src/
│   │   │   ├── index.ts
│   │   │   ├── ast-gate.ts   # AST rules enforcement
│   │   │   └── complexity.ts # Complexity limits
│   │   └── tests/
│   ├── expr/                 # JS Expr Sugar → Expr IR translator
│   │   ├── src/
│   │   │   ├── index.ts
│   │   │   ├── rewriter.ts   # AST rewrite for spanId injection
│   │   │   ├── translator.ts # AST → Expr IR
│   │   │   ├── validator.ts  # Type checking
│   │   │   └── errors.ts     # Error codes + messages
│   │   └── tests/
│   ├── plan-compiler/        # plan.js → plan.json
│   │   ├── src/
│   │   │   ├── index.ts
│   │   │   ├── dsl.ts        # DSL runtime (dsl.*, Pipeline)
│   │   │   ├── sandbox.ts    # VM execution with frozen globals
│   │   │   └── compiler.ts   # Orchestrates gate → rewrite → exec → translate
│   │   └── tests/
│   ├── cli/                  # rankdsl CLI
│   │   ├── src/
│   │   │   ├── index.ts
│   │   │   └── commands/
│   │   │       ├── plan.ts   # Compile plan.js → plan.json
│   │   │       ├── validate.ts
│   │   │       ├── codegen.ts
│   │   │       └── run.ts    # Invoke engine
│   │   └── tests/
│   └── shared/               # Shared utilities
│       ├── src/
│       │   ├── types.ts      # Common TypeScript types
│       │   └── expr-ir.ts    # Expr IR types + builders
│       └── tests/
├── engine/
│   ├── CMakeLists.txt
│   ├── src/
│   │   ├── main.cpp          # Entry point
│   │   ├── plan/
│   │   │   ├── plan.h        # Plan data structures
│   │   │   ├── plan.cpp
│   │   │   ├── compiler.h    # Plan compiler
│   │   │   └── compiler.cpp
│   │   ├── keys/
│   │   │   ├── registry.h    # Key registry runtime (loads keys.json)
│   │   │   └── registry.cpp
│   │   ├── object/
│   │   │   ├── value.h       # Value variant type
│   │   │   ├── value.cpp
│   │   │   ├── typed_column.h   # TypedColumn base + F32Column, I64Column, F32VecColumn
│   │   │   ├── typed_column.cpp
│   │   │   ├── column_batch.h   # ColumnBatch with shared_ptr<TypedColumn>
│   │   │   ├── column_batch.cpp
│   │   │   ├── batch_builder.h  # COW BatchBuilder
│   │   │   ├── batch_builder.cpp
│   │   │   ├── row_view.h       # RowView over (batch, rowIndex)
│   │   │   └── row_view.cpp
│   │   ├── expr/
│   │   │   ├── expr.h        # Expr IR evaluation
│   │   │   ├── expr.cpp
│   │   │   └── ops/          # Individual op implementations
│   │   │       ├── const.cpp
│   │   │       ├── signal.cpp
│   │   │       ├── arithmetic.cpp
│   │   │       └── cos_sim.cpp
│   │   ├── nodes/
│   │   │   ├── node_runner.h # Base interface
│   │   │   ├── registry.h    # Node registry
│   │   │   ├── core/
│   │   │   │   ├── sourcer.cpp
│   │   │   │   ├── merge.cpp
│   │   │   │   ├── features.cpp
│   │   │   │   ├── model.cpp
│   │   │   │   └── score_formula.cpp
│   │   │   └── js/
│   │   │       ├── njs_runner.h
│   │   │       ├── njs_runner.cpp    # Load + execute njs modules
│   │   │       ├── batch_context.h   # BatchContext with zero-copy APIs
│   │   │       ├── batch_context.cpp
│   │   │       └── njs_sandbox.cpp   # JS runtime with budget enforcement
│   │   ├── executor/
│   │   │   ├── executor.h
│   │   │   └── executor.cpp
│   │   └── logging/
│   │       ├── trace.h
│   │       └── trace.cpp
│   ├── tests/
│   │   ├── test_main.cpp
│   │   ├── plan_compiler_test.cpp
│   │   ├── expr_eval_test.cpp
│   │   ├── column_batch_test.cpp     # TypedColumn, ColumnBatch, COW sharing
│   │   ├── row_view_test.cpp         # RowView get/set, type enforcement
│   │   ├── columnar_eval_test.cpp    # Expression eval on typed columns
│   │   ├── key_enforcement_test.cpp
│   │   ├── njs_runner_test.cpp       # BatchContext APIs, writes/budget enforcement
│   │   └── e2e_pipeline_test.cpp
│   └── third_party/
│       └── (fmt, nlohmann_json, catch2, quickjs or v8)
├── package.json              # Root package.json (workspaces)
├── tsconfig.json
├── vitest.config.ts
└── .github/
    └── workflows/
        └── ci.yml
```

---

## Implementation Phases

### Phase 1: Foundation ✅ COMPLETED

**1.1 Project Setup**
- [x] Initialize npm workspace with TypeScript
- [x] Configure Vitest
- [x] Set up C++ project with CMake
- [x] Add dependencies: `fmt`, `nlohmann/json`, Catch2
- [x] Create shared TypeScript types (`tools/shared/`)

**1.2 Key Registry**
- [x] Define registry.yaml schema (Zod)
- [x] Implement YAML parser with validation (`tools/codegen/src/parser.ts`)
- [x] Implement TypeScript generator (`keys.ts`)
- [x] Implement C++ header generator (`keys.h`)
- [x] Implement JSON generator (`keys.json`)
- [x] Create example `keys/registry.yaml` with spec keys (including penalty keys)
- [x] Write tests for codegen

**1.3 Expr IR Types**
- [x] Define Expr IR TypeScript types (`tools/shared/src/expr-ir.ts`)
- [x] Implement `dsl.F` builder API
- [x] Write tests for Expr IR builder

**1.4 C++ Engine Foundation**
- [x] Implement `Value` variant type
- [x] Implement `Obj` (copying map - pre-columnar)
- [x] Implement `KeyRegistry` (loads keys.json)
- [x] Implement Expr IR evaluation (const, signal, add, mul, min, max, clamp, cos, penalty)
- [x] Implement Plan parsing and compilation
- [x] Implement core node runners (sourcer, merge, features, model, score_formula)
- [x] Implement executor with tracing
- [x] Write C++ tests (11 tests passing)

---

### Phase 1.5: Columnar Data Model Refactor ✅ COMPLETED

**Goal:** Refactor internal data model from row-oriented (`vector<Obj>`) to column-oriented (`ColumnBatch`) while preserving public API semantics.

**1.5.1 Typed Columns and ColumnBatch**
- [x] Implement `TypedColumn` abstract base with typed storage (`F32Column`, `I64Column`, `F32VecColumn`, etc.)
- [x] Implement `ColumnBatch` with `Map<KeyId, shared_ptr<TypedColumn>>`
- [x] Add typed accessors: `GetF32Column()`, `GetI64Column()`, `GetF32VecColumn()`
- [x] Implement `F32VecColumn` with contiguous N×D storage (row-major)
- [x] Write unit tests for TypedColumn operations and ColumnBatch

**1.5.2 BatchBuilder (COW Semantics)**
- [x] Implement `BatchBuilder` that wraps a source `ColumnBatch`
- [x] Implement `Set(row_index, key_id, value)` with copy-on-write
- [x] Implement `Build()` → new `ColumnBatch` sharing unmodified columns
- [x] Write unit tests for COW behavior (verify sharing via `use_count()`)

**1.5.3 RowView (Refactored Obj)**
- [x] Implement `RowView` over `(ColumnBatch*, row_index)`
- [x] `Get(key_id)` reads from typed column
- [x] `Set(key_id, val)` writes via `BatchBuilder`, returns new `RowView`
- [x] Type enforcement via `KeyRegistry`
- [x] Write unit tests for RowView get/set semantics

**1.5.4 Update Core Nodes**
- [x] Update `NodeRunner` interface to operate on `ColumnBatch` input/output
- [x] `core:sourcer` - create initial `ColumnBatch` with typed columns
- [x] `core:merge` - merge batches using typed columns
- [x] `core:features` - add feature columns (including `F32VecColumn` for embeddings)
- [x] `core:model` - add score column using typed accessors
- [x] `core:score_formula` - evaluate expr on typed columns, write output column

**1.5.5 Update Executor**
- [x] Executor passes `ColumnBatch` between nodes
- [x] Tracing reports column counts and row counts
- [x] Integration tests pass with columnar batches

**1.5.6 BatchContext Zero-Copy APIs (for njs)**
- [x] `GetF32Raw(key_id)` → `pair<const float*, size_t>` (zero-copy read)
- [x] `GetF32VecRaw(key_id)` → `F32VecView{data, data_size, dim, row_count}` (contiguous N×D)
- [x] `GetI64Raw(key_id)` → `pair<const int64_t*, size_t>` (zero-copy read)
- [x] `AllocateF32(key_id)` → `float*` (direct write access)
- [x] `AllocateF32Vec(key_id, dim)` → `float*` (contiguous N×D write)
- [x] `AllocateI64(key_id)` → `int64_t*` (direct write access)
- [x] `meta.writes` enforcement on all Allocate* calls
- [x] Budget enforcement (`max_write_bytes`, `max_write_cells`)
- [x] Type mismatch enforcement via KeyRegistry

**1.5.7 Tests (24 tests passing)**
- [x] `column_batch_test.cpp`: TypedColumn operations, ColumnBatch, COW semantics
- [x] `row_view_test.cpp`: RowView read/write, type enforcement
- [x] `columnar_eval_test.cpp`: Expression evaluation on typed columns
- [x] `njs_runner_test.cpp`: BatchContext read/write APIs, enforcement, budget limits

---

### Phase 2: Plan.js Tooling

**2.1 Static Gate (Linter)**
- [ ] Implement AST parser using `@babel/parser`
- [ ] Implement allowed statement subset checker
- [ ] Implement disallowed construct detection (IO, eval, loops, etc.)
- [ ] Special case: allow arrow functions only in `dsl.expr()`
- [ ] Implement complexity limits (nesting, branches, file size)
- [ ] Error reporting with file/line/col
- [ ] Write comprehensive tests

**2.2 AST Rewriter (spanId injection)**
- [ ] Parse plan.js with `@babel/parser` (ranges + loc)
- [ ] Walk AST to find `dsl.expr(ArrowFunctionExpression)` calls
- [ ] Compute stable spanId from `start:end` range
- [ ] Inject spanId as first argument: `dsl.expr("start:end", () => ...)`
- [ ] Generate rewritten source with `@babel/generator`
- [ ] Store AST node map keyed by spanId for later translation
- [ ] Write tests (including branching scenarios)

**2.3 JS Expr Sugar Translator**
- [ ] Given spanId, retrieve AST node from map
- [ ] Implement translation rules:
  - NumericLiteral → `const`
  - `Keys.X` → `signal` (resolve via Keys object)
  - `+`, `-`, `*` → `add`, `mul`
  - Unary `-` → `mul(-1, x)`
  - `dsl.fn.cosSim(a,b)` / `dsl.fn.cos(a,b)` → `cos`
  - `dsl.fn.min(...)` / `dsl.fn.max(...)` → `min` / `max`
  - `dsl.fn.clamp(x,lo,hi)` → `clamp`
  - `dsl.fn.penalty(name)` → `penalty`
- [ ] **Reject bare function calls** (not under `dsl.fn.*`)
- [ ] Reject `/` (division) with `E_EXPR_DISALLOWED_SYNTAX`
- [ ] Reject disallowed constructs (ternary, logical ops, unknown identifiers)
- [ ] Implement type validation using key registry metadata
- [ ] Error codes with source spans
- [ ] Write comprehensive tests (valid + invalid expressions + branching)

**2.4 Plan Builder DSL**
- [ ] Implement `dsl.sourcer()`, `dsl.candidates()`
- [ ] Implement `Pipeline` class (immutable, returns new Pipeline)
- [ ] Implement `Pipeline.features()`, `.model()`, `.node()`, `.score()`, `.build()`
- [ ] Implement `dsl.expr(spanId, fn)` returning `JSExprToken { kind, spanId }`
- [ ] Implement `dsl.fn.*` namespace (cosSim, cos, min, max, clamp, penalty)
- [ ] Implement `dsl.F.*` builder API (for explicit Expr IR authoring)
- [ ] Plan JSON serialization
- [ ] Write tests

**2.5 Plan Compiler (TypeScript side)**
- [ ] Orchestrate: static gate → AST rewrite → VM execute → translate tokens
- [ ] Load plan.js, run static gate
- [ ] Rewrite AST with spanId injection
- [ ] Execute in VM with frozen globals (`dsl`, `Keys`, `config`)
- [ ] Collect JSExprTokens from resulting Plan
- [ ] Translate each token to Expr IR using spanId → AST lookup
- [ ] Validate plan structure (unique node ids, DAG acyclic, ops resolvable)
- [ ] Validate key references against registry
- [ ] Output compiled plan JSON
- [ ] Write tests

---

### Phase 3: C++ Engine (njs Runner)

> **Note:** Core data structures, expr eval, plan compiler, core nodes, executor, and key enforcement were completed in Phase 1.4. Only njs runner remains.

**3.1 njs Runner (MVP)**
- [ ] Embed JS runtime (QuickJS recommended for simplicity, or V8)
- [ ] Load `.njs` module from path
- [ ] Parse `meta` object, validate schema
- [ ] Create sandboxed JS context with `RowView` bindings (get/set/has/del)
- [ ] **Enforce `meta.writes`**: intercept `obj.set()`, reject if key not in writes
- [ ] **Enforce budget**: step limit (QuickJS interrupt) or wall-clock timeout
- [ ] Call `runBatch(objs, ctx, params)` or fallback to `run(obj, ctx, params)`
  - Each `obj` is a `RowView` - writes go through BatchBuilder
- [ ] Return modified batch to pipeline
- [ ] Write tests: load module, writes enforcement, budget exceeded

---

### Phase 4: CLI & Integration

**4.1 CLI**
- [ ] Implement `rankdsl codegen` (regenerate keys.* from registry.yaml)
- [ ] Implement `rankdsl validate <plan.js>` (static gate only)
- [ ] Implement `rankdsl plan --in <file> --out <file> --config <file>`
- [ ] Implement `rankdsl run --plan <plan.json> --dump-top N` (invokes engine)
- [ ] Write CLI tests

**4.2 End-to-End Test**
- [ ] Create example plan using core nodes + njs node + expression
- [ ] Compile plan.js → plan.json via CLI
- [ ] Execute plan.json via engine
- [ ] Verify output (scores, logs, njs writes)

**4.3 CI Pipeline**
- [ ] Registry validation + codegen freshness check
- [ ] TypeScript lint + type check
- [ ] Vitest run (TS)
- [ ] C++ build + tests
- [ ] E2E test

---

### Phase 5: Polish & Documentation

- [ ] Error message improvements (include suggestions for typos)
- [ ] README with usage examples
- [ ] Example plan files (simple, with branching, with njs)
- [ ] Example njs module

---

## Expr IR Quick Reference

| Op | JSON | Notes |
|----|------|-------|
| const | `{"op":"const","value":0.7}` | |
| signal | `{"op":"signal","key_id":3001}` | |
| add | `{"op":"add","args":[...]}` | |
| mul | `{"op":"mul","args":[...]}` | |
| cos | `{"op":"cos","a":<expr>,"b":<expr>}` | Cosine similarity, f32vec inputs |
| min | `{"op":"min","args":[...]}` | |
| max | `{"op":"max","args":[...]}` | |
| clamp | `{"op":"clamp","x":<expr>,"lo":<expr>,"hi":<expr>}` | |
| penalty | `{"op":"penalty","name":"constraints"}` | Reads `penalty.{name}` key |
| div | `{"op":"div","a":<expr>,"b":<expr>}` | **Reserved, not in MVP** |

---

## JS Expr Sugar → Expr IR Translation

| JS Sugar | Expr IR |
|----------|---------|
| `42` | `{"op":"const","value":42}` |
| `Keys.SCORE_BASE` | `{"op":"signal","key_id":<id>}` |
| `a + b` | `{"op":"add","args":[T(a),T(b)]}` |
| `a - b` | `{"op":"add","args":[T(a),{"op":"mul","args":[{"op":"const","value":-1},T(b)]}]}` |
| `a * b` | `{"op":"mul","args":[T(a),T(b)]}` |
| `-x` | `{"op":"mul","args":[{"op":"const","value":-1},T(x)]}` |
| `dsl.fn.cosSim(a,b)` | `{"op":"cos","a":T(a),"b":T(b)}` |
| `dsl.fn.min(a,b,c)` | `{"op":"min","args":[T(a),T(b),T(c)]}` |
| `dsl.fn.max(a,b,c)` | `{"op":"max","args":[T(a),T(b),T(c)]}` |
| `dsl.fn.clamp(x,lo,hi)` | `{"op":"clamp","x":T(x),"lo":T(lo),"hi":T(hi)}` |
| `dsl.fn.penalty("name")` | `{"op":"penalty","name":"name"}` |

**Rejected in MVP:**
- `/` (division) → `E_EXPR_DISALLOWED_SYNTAX`
- `?:` (ternary) → `E_EXPR_DISALLOWED_SYNTAX`
- `&&`, `||` (logical) → `E_EXPR_DISALLOWED_SYNTAX`
- Bare function calls (`cos()` without `dsl.fn.`) → `E_EXPR_UNKNOWN_IDENTIFIER`
- Any identifier other than `Keys` and `dsl.fn` → `E_EXPR_UNKNOWN_IDENTIFIER`

---

## Testing Strategy

### TypeScript (Vitest)
- Unit tests for each module
- Snapshot tests for codegen output
- Error case coverage for static gate and expr translator
- **Branching scenarios** for spanId binding (if/else in plan.js)

### C++ (Catch2)
- `column_batch_test.cpp`: Column creation, ColumnBatch construction, column sharing verification
- `row_view_test.cpp`: RowView get/set, immutability semantics, type enforcement
- `expr_eval_test.cpp`: Each op, type errors, edge cases, cos normalization
- `plan_compiler_test.cpp`: Validation, cycle detection, key validation
- `key_enforcement_test.cpp`: Type mismatch rejection
- `njs_runner_test.cpp`: Module load, meta validation, writes enforcement, budget limits
- `e2e_pipeline_test.cpp`: Full pipeline with core + njs nodes, columnar batches

---

## Dependencies

### TypeScript
- `@babel/parser` - AST parsing
- `@babel/generator` - AST → source (for rewrite)
- `@babel/traverse` - AST walking
- `yaml` - YAML parsing
- `zod` - Schema validation
- `commander` - CLI framework
- `vitest` - Testing

### C++
- `fmt` - Formatting/logging
- `nlohmann/json` - JSON parsing
- `catch2` - Testing
- `quickjs` - JS runtime for njs (or V8 if more features needed)
