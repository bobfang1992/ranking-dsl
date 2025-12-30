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
npm test                                    # TypeScript tests (73 tests)
ctest --test-dir build-rel --output-on-failure  # C++ tests (27 tests)
```

## Project Structure

```
ranking-dsl/
├── keys/
│   ├── registry.yaml          # Key definitions (source of truth)
│   └── generated/             # Generated key constants
│       ├── keys.ts            # TypeScript
│       ├── keys.h             # C++
│       ├── keys.json          # Runtime (for engine)
│       └── keys.njs           # CommonJS (for njs modules)
├── tools/
│   ├── shared/                # Common types, Expr IR
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
│   │   ├── plan/              # Plan parsing & compilation
│   │   ├── nodes/             # Node runners (core + js)
│   │   ├── executor/          # Pipeline executor
│   │   └── logging/           # Structured tracing
│   └── tests/                 # Catch2 tests
├── plans/                     # Example plan files
└── njs/                       # JavaScript node modules
```

## CLI Commands

```bash
# Generate keys.ts, keys.h, keys.json, keys.njs from registry.yaml
npm run codegen

# Check if generated files are up to date (for CI)
node tools/cli/dist/index.js codegen --check

# Run the engine on a compiled plan
./build-rel/engine/rankdsl_engine plan.json [OPTIONS]

# Engine options:
#   -k, --keys <file>     Path to keys.json (uses compiled-in keys if not specified)
#   -n, --dump-top <N>    Number of top results to display
#   -q, --quiet           Suppress output except errors
#   -h, --help            Print help message
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

**Enforcement:**
- `meta.writes` - Only listed keys can be written
- `meta.budget` - `max_write_cells` and `max_write_bytes` limits enforced

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
