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
| Phase 2 | **Not Implemented** | JS expr sugar (`dsl.expr()`), AST gate, spanId injection |
| Phase 3 | Complete | njs runner with QuickJS, column-level APIs, budget enforcement |
| Phase 3.5 | Complete | njs sandbox with policy-gated host IO (`ctx.io.readCsv`) |
| Phase 3.6 | Complete | Plan complexity governance (two-layer TS/C++ enforcement) |
| Phase 3.7 | Complete | Node-level trace_key and njs trace prefixing |

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
npm test                                    # TypeScript tests (102 tests)
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
├── tools/
│   ├── shared/                # Common types, Expr IR, complexity
│   ├── codegen/               # Registry → generated files
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
