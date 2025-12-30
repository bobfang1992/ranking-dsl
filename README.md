# Ranking DSL

An embedded DSL for building ranking pipelines, with TypeScript tooling and a C++ execution engine.

## Overview

- **Plan files** (`*.plan.js`) define ranking pipelines using a fluent DSL
- **Engine** (C++) compiles and executes plans with high performance
- **Key Registry** provides type-safe key handles (no free-form string keys)
- **Expressions** authored using the builder-based Expr IR API (`dsl.F.*`)

## Implementation Status

| Phase | Status | Description |
|-------|--------|-------------|
| Phase 1 | Complete | Project setup, key registry, Expr IR, core nodes |
| Phase 1.5 | Complete | Columnar data model (TypedColumn, ColumnBatch, COW), BatchContext APIs |
| Phase 2.1 | Complete | Static gate (AST linter for plan.js) |
| Phase 2.2-2.5 | **Not Implemented** | JS expr sugar (`dsl.expr()`), spanId injection, plan compiler |
| Phase 3 | Complete | njs runner with QuickJS, column-level APIs, budget enforcement |
| Phase 3.5 | Complete | njs sandbox with policy-gated host IO (`ctx.io.readCsv`) |
| Phase 3.6 | Complete | Plan complexity governance (two-layer TS/C++ enforcement) |
| Phase 3.7 | Complete | Node-level trace_key and njs trace prefixing |
| Phase 3.8 | Complete | NodeSpec infrastructure & catalog codegen (spec v0.2.8) |

**Note:** The JS expression sugar syntax (`dsl.expr(() => ...)`) and AST rewriting with spanId injection are Phase 2 features and are **not yet implemented**. Use the builder-based `dsl.F.*` API for expressions.

## Quick Start

```bash
# Install dependencies
npm ci

# Build TypeScript tooling
npm run build

# Generate key constants from registry
npm run codegen

# Build C++ engine (from repo root)
cmake -S . -B build-rel -DCMAKE_BUILD_TYPE=RelWithDebInfo
cmake --build build-rel -j

# Run tests
npm test                                    # TypeScript tests (144 tests)
ctest --test-dir build-rel --output-on-failure  # C++ tests (43 test cases)
```

## Project Structure

```
ranking-dsl/
├── configs/
│   └── complexity_budgets.json  # Plan complexity limits (shared TS/C++)
├── keys/
│   ├── registry.yaml          # Key definitions (source of truth)
│   └── generated/             # Generated key constants
│       ├── keys.ts            # TypeScript
│       ├── keys.h             # C++
│       ├── keys.json          # Runtime (for engine)
│       └── keys.njs           # CommonJS (for njs modules)
├── nodes/
│   └── generated/             # Generated node catalog & bindings (v0.2.8+)
│       ├── nodes.yaml         # Node catalog exported from C++/njs
│       └── nodes.ts           # TypeScript bindings (namespaced APIs)
├── tools/
│   ├── shared/                # Common types, Expr IR, complexity
│   ├── codegen/               # Registry → generated files
│   ├── nodes-codegen/         # Node catalog export & codegen (v0.2.8+)
│   ├── lint/                  # plan.js static gate (Phase 2)
│   ├── expr/                  # JS expr → Expr IR (Phase 2)
│   ├── plan-compiler/         # plan.js compiler (Phase 2)
│   └── cli/                   # rankdsl CLI
├── engine/
│   ├── src/
│   │   ├── keys/              # Key registry runtime
│   │   ├── object/            # Value, Obj, CandidateBatch
│   │   ├── expr/              # Expression evaluation
│   │   ├── plan/              # Plan parsing, compilation, complexity
│   │   ├── nodes/             # Node runners (core + js)
│   │   ├── executor/          # Pipeline executor
│   │   └── logging/           # Structured tracing
│   └── tests/                 # Catch2 tests
├── docs/
│   ├── spec.md                # Full specification
│   └── complexity-governance.md  # Complexity budget docs
├── plans/                     # Example plan files
├── njs/                       # JavaScript node modules
└── test-fixtures/             # Test fixture files
```

## CLI Commands

```bash
# Generate keys.ts, keys.h, keys.json, keys.njs from registry.yaml
npm run codegen

# Check if generated files are up to date (for CI)
node tools/cli/dist/index.js codegen --check

# Export node catalog from C++ engine and njs modules (v0.2.8+)
node tools/cli/dist/index.js nodes export
node tools/cli/dist/index.js nodes export --engine-build engine/build --njs njs -o nodes/generated/nodes.yaml

# Generate TypeScript bindings from node catalog (v0.2.8+)
node tools/cli/dist/index.js nodes codegen
node tools/cli/dist/index.js nodes codegen -i nodes/generated/nodes.yaml -o nodes/generated/nodes.ts

# Graduate an experimental node to stable (v0.2.8+)
node tools/cli/dist/index.js nodes graduate <node> --to <namespace>

# Validate a plan against complexity budgets
node tools/cli/dist/index.js validate plan.json
node tools/cli/dist/index.js validate plan.json --budget configs/complexity_budgets.json
node tools/cli/dist/index.js validate plan.json --json  # Output as JSON

# Run the engine on a compiled plan
./build-rel/engine/rankdsl_engine plan.json [OPTIONS]

# Engine options:
#   -k, --keys <file>       Path to keys.json (uses compiled-in keys if not specified)
#   -b, --budget <file>     Path to complexity budget JSON file
#   -n, --dump-top <N>      Number of top results to display
#   -q, --quiet             Suppress output except errors
#   --no-complexity-check   Disable complexity checking
#   -h, --help              Print help message
```

## Key Registry

Keys are defined in `keys/registry.yaml`:

```yaml
version: 1
keys:
  - id: 3001
    name: score.base
    type: f32
    scope: score
    owner: ranking-team
    doc: "Base retrieval score"
```

**Rules:**
- IDs must be globally unique and never reused
- Names are lowercase with dots (e.g., `score.base`)
- Types: `bool`, `i64`, `f32`, `string`, `bytes`, `f32vec`
- Scopes: `candidate`, `feature`, `score`, `debug`, `tmp`, `penalty`

## Plan.js Syntax Rules

Plan files (`*.plan.js`) are validated by a static gate that enforces a restricted JavaScript subset. This keeps plans simple, auditable, and free from side effects.

### Allowed Syntax

| Category | Allowed |
|----------|---------|
| **Declarations** | `const`, `let`, `var` |
| **Literals** | Numbers, strings, booleans, `null`, `undefined`, template literals |
| **Data structures** | Object literals `{}`, array literals `[]` |
| **Operators** | Arithmetic (`+`, `-`, `*`, `/`, `%`), comparison (`==`, `===`, `<`, `>`), logical (`&&`, `\|\|`, `!`) |
| **Control flow** | `if`/`else`, ternary `? :`, `switch`/`case` |
| **Functions** | Function declarations, function expressions (non-async, non-generator) |
| **Expressions** | Property access, method calls, `dsl.*` APIs |
| **Special** | Arrow functions **only inside `dsl.expr()`** |

### Disallowed Syntax

| Category | Disallowed | Error Code |
|----------|------------|------------|
| **Modules** | `import`, `export` | `E_PARSE_ERROR` |
| **Dynamic loading** | `require()`, dynamic `import()` | `E_REQUIRE_CALL`, `E_DYNAMIC_IMPORT` |
| **Code generation** | `eval()`, `Function()`, `new Function()` | `E_EVAL_CALL`, `E_FUNCTION_CONSTRUCTOR` |
| **Loops** | `for`, `while`, `do-while`, `for-in`, `for-of` | `E_LOOP_STATEMENT` |
| **Classes** | `class` declarations/expressions | `E_CLASS_DECLARATION` |
| **Async/Generators** | `async function`, `function*`, `async () =>` | `E_ASYNC_FUNCTION`, `E_GENERATOR_FUNCTION` |
| **Legacy** | `with` statement | `E_WITH_STATEMENT` |
| **Arrow functions** | Outside `dsl.expr()` | `E_ARROW_OUTSIDE_EXPR` |
| **Dangerous globals** | `process`, `fs`, `fetch`, `setTimeout`, `globalThis`, etc. | `E_DISALLOWED_GLOBAL` |

### Complexity Limits

| Limit | Default | Error Code |
|-------|---------|------------|
| File size | 50 KB | `E_FILE_TOO_LARGE` |
| Max nesting depth | 10 | `E_NESTING_TOO_DEEP` |
| Max branches | 50 | `E_TOO_MANY_BRANCHES` |
| Max statements | 200 | `E_TOO_MANY_STATEMENTS` |

### Example Valid Plan

```javascript
// plan.js - configuration-driven pipeline
const alpha = config.useNewModel ? 0.8 : 0.7;

let p = dsl.sourcer("main", { limit: 1000 });

if (config.enableFeatures) {
  p = p.features("base", { keys: [Keys.FEAT_A, Keys.FEAT_B] });
}

p = p.model("ranking_v2", { threshold: 0.5 });

// Arrow function allowed inside dsl.expr()
p = p.score(dsl.expr(() => alpha * Keys.SCORE_BASE + (1 - alpha) * Keys.SCORE_ML), {
  output_key_id: Keys.SCORE_FINAL.id
});

return p.build();
```

## Expressions

Expressions can be built using the `dsl.F` builder:

```javascript
const F = dsl.F;
const expr = F.add(
  F.mul(F.const(0.7), F.signal(Keys.SCORE_BASE)),
  F.mul(F.const(0.3), F.signal(Keys.SCORE_ML))
);
```

Or using JS expression syntax (Phase 2):

```javascript
p = p.score(
  dsl.expr(() => 0.7 * Keys.SCORE_BASE + 0.3 * Keys.SCORE_ML),
  { output_key_id: Keys.SCORE_FINAL.id }
);
```

**Supported operations:**
| Op | Description |
|----|-------------|
| `const` | Constant value |
| `signal` | Read key from object |
| `add` | Addition (variadic) |
| `mul` | Multiplication (variadic) |
| `min` | Minimum (variadic) |
| `max` | Maximum (variadic) |
| `clamp` | Clamp to range |
| `cos` | Cosine similarity (f32vec) |
| `penalty` | Read penalty key |

## Core Nodes

| Node | Description |
|------|-------------|
| `core:sourcer` | Generate candidate objects |
| `core:merge` | Deduplicate candidates |
| `core:features` | Populate feature keys |
| `core:model` | Run ML model, write score |
| `core:score_formula` | Evaluate expression, write result |
| `njs` | Execute JavaScript module (QuickJS) |

## Tracing

Nodes support optional `trace_key` for stable identification in logs and spans:

```javascript
// In plan.js (Phase 2)
p = p.model("vm_v1", { /* params */ }, { trace_key: "final_vm" })
     .score(expr, {}, { trace_key: "final_score" });
```

**trace_key constraints:**
- Length: 1-64 characters
- Charset: `[A-Za-z0-9._/-]`
- Appears in node JSON envelope (NOT inside params)

**Span naming:**
- With trace_key: `core:model(final_vm)`
- Without trace_key: `core:model`

**njs trace prefixing:**
- Filename stem becomes `trace_prefix` (e.g., `rank_vm.njs` → `rank_vm`)
- Nested calls: `{trace_prefix}::{child_trace_key}`

## Complexity Governance

Plans are validated against complexity budgets to keep pipelines auditable and debuggable. Budgets are defined in `configs/complexity_budgets.json`:

```json
{
  "hard": { "node_count": 2000, "max_depth": 120, "fanout_peak": 16, "fanin_peak": 16 },
  "soft": { "edge_count": 10000, "complexity_score": 8000 }
}
```

**Metrics:**
| Metric | Description |
|--------|-------------|
| `node_count` | Total nodes in plan DAG |
| `edge_count` | Total edges in plan DAG |
| `max_depth` | Longest path length |
| `fanout_peak` | Maximum out-degree |
| `fanin_peak` | Maximum in-degree |
| `complexity_score` | Weighted combination |

- **Hard limits**: Compilation fails if exceeded
- **Soft limits**: Warnings emitted

See `docs/complexity-governance.md` for full details.

## njs Modules

Custom ranking logic can be implemented in JavaScript using `.njs` modules:

```javascript
// example.njs - Uses Keys.* identifiers (injected globally)
exports.meta = {
  name: "example",
  version: "1.0.0",
  reads: [Keys.SCORE_BASE],
  writes: [Keys.SCORE_ML],
  budget: {
    max_write_bytes: 1048576,
    max_write_cells: 100000
  }
};

exports.runBatch = function(objs, ctx, params) {
  var n = ctx.batch.rowCount();
  var baseScores = ctx.batch.f32(Keys.SCORE_BASE);  // Read column
  var mlScores = ctx.batch.writeF32(Keys.SCORE_ML); // Write column

  for (var i = 0; i < n; i++) {
    mlScores[i] = baseScores[i] * params.alpha;
  }
  return undefined;  // Signal column writes used
};
```

**ctx.batch API:**
| Method | Description |
|--------|-------------|
| `rowCount()` | Number of rows in batch |
| `f32(key)` | Read f32 column as array |
| `i64(key)` | Read i64 column as array |
| `writeF32(key)` | Allocate writable f32 column |
| `writeI64(key)` | Allocate writable i64 column |

**ctx.io API (Host IO):**
| Method | Description |
|--------|-------------|
| `readCsv(path)` | Read CSV file, returns array of objects |

Host IO requires `meta.capabilities: ["io"]` and paths must be in policy allowlist.

**Enforcement:**
- `meta.writes` - Only listed keys can be written
- `meta.capabilities` - Must declare `["io"]` to use `ctx.io.*`
- `meta.budget` - `max_write_cells`, `max_write_bytes`, `max_io_bytes_read` limits enforced

## Node Catalog & Generated Bindings (v0.2.8+)

The ranking DSL uses C++ and .njs modules as the **single source of truth** for node metadata. Node metadata includes:

- **op** - Unique operation identifier (e.g., `core:sourcer`, `js:example.njs@1.0.0#abc123`)
- **namespace_path** - Namespace for generated API (e.g., `core.sourcer`, `experimental.myNode`)
- **stability** - `stable` or `experimental`
- **doc** - Human-readable description
- **params_schema** - JSON Schema for parameter validation
- **reads** - List of keys the node reads from
- **writes** - Either `static` (fixed key list) or `param_derived` (determined by params)

### NodeSpec in C++

Core nodes define their metadata using the `NodeSpec` struct:

```cpp
static NodeSpec CreateScoreFormulaNodeSpec() {
  NodeSpec spec;
  spec.op = "core:score_formula";
  spec.namespace_path = "core.score";
  spec.stability = Stability::kStable;
  spec.doc = "Evaluates an expression and writes the result";
  spec.params_schema_json = R"({"type": "object", ...})";
  spec.reads = {};
  spec.writes.kind = WritesDescriptor::Kind::kParamDerived;
  spec.writes.param_name = "output_key_id";
  return spec;
}

REGISTER_NODE_RUNNER("core:score_formula", ScoreFormulaNode, CreateScoreFormulaNodeSpec());
```

### NodeSpec in njs

njs modules define metadata in `exports.meta`:

```javascript
exports.meta = {
  name: "example",
  version: "1.0.0",
  namespace_path: "experimental.example",
  stability: "experimental",
  doc: "Example custom node",
  params_schema: { type: "object", properties: {...} },
  reads: [Keys.SCORE_BASE],
  writes: [Keys.SCORE_ML]
};
```

### Catalog Export & Codegen Workflow

```bash
# 1. Export node catalog from C++/njs sources
npm run build
node tools/cli/dist/index.js nodes export

# 2. Generate TypeScript bindings from catalog
node tools/cli/dist/index.js nodes codegen

# 3. Use generated namespaced APIs in plan.js (Phase 2+)
# let p = dsl.sourcer("main", { limit: 1000 });
# p = p.core.merge.weightedUnion({ strategy: "max" });
# p = p.core.score({ expr: ... });
```

**Generated bindings provide:**
- Type-safe method signatures derived from JSON Schema
- Nested namespace structure (e.g., `p.core.merge.weightedUnion()`)
- IntelliSense/autocomplete in IDEs
- Compile-time checks for node parameters

**Stability enforcement:**
- `experimental.*` nodes can only be used in `dev`/`test` plan environments
- `stable` nodes (any namespace except `experimental.*`) can be used in all environments
- Graduation workflow: Update `namespace_path` in source, re-export, re-codegen

## Development

```bash
# TypeScript
npm run build          # Build all packages
npm test               # Run Vitest tests
npm run typecheck      # Type check without emitting

# C++ (from repo root)
cmake --build build-rel -j           # Incremental build
ctest --test-dir build-rel           # Run tests
ctest --test-dir build-rel -V        # Verbose test output
```

## Architecture

```
┌─────────────┐     ┌─────────────┐     ┌─────────────┐
│  plan.js    │────▶│  Compiler   │────▶│  plan.json  │
│  (DSL)      │     │  (TS)       │     │  (IR)       │
└─────────────┘     └─────────────┘     └─────────────┘
                                               │
                                               ▼
┌─────────────┐     ┌─────────────┐     ┌─────────────┐
│  Results    │◀────│  Executor   │◀────│  C++ Engine │
│             │     │  (C++)      │     │  Compile    │
└─────────────┘     └─────────────┘     └─────────────┘
```

## License

MIT
