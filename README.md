# Ranking DSL

An embedded DSL for building ranking pipelines, with TypeScript tooling and a C++ execution engine.

## Overview

- **Plan files** (`*.plan.js`) define ranking pipelines using a fluent DSL
- **Engine** (C++) compiles and executes plans with high performance
- **Key Registry** provides type-safe key handles (no free-form string keys)
- **Expressions** can be authored as Expr IR or JS expression syntax

## Quick Start

```bash
# Install dependencies
npm install

# Build TypeScript tooling
npm run build

# Generate key constants from registry
node tools/cli/dist/index.js codegen

# Build C++ engine
cd engine
mkdir -p build && cd build
cmake ..
make -j

# Run tests
npm test              # TypeScript tests (73 tests)
ctest                 # C++ tests (11 tests)
```

## Project Structure

```
ranking-dsl/
├── keys/
│   ├── registry.yaml          # Key definitions (source of truth)
│   └── generated/             # Generated key constants
│       ├── keys.ts            # TypeScript
│       ├── keys.h             # C++
│       └── keys.json          # Runtime (for engine)
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
# Generate keys.ts, keys.h, keys.json from registry.yaml
rankdsl codegen

# Check if generated files are up to date (for CI)
rankdsl codegen --check

# Run the engine on a compiled plan
./engine/build/rankdsl_engine plan.json --dump-top 20
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

## Development

```bash
# TypeScript
npm run build          # Build all packages
npm test               # Run Vitest tests
npm run typecheck      # Type check without emitting

# C++
cd engine/build
make                   # Incremental build
ctest                  # Run tests
ctest -V               # Verbose test output
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
