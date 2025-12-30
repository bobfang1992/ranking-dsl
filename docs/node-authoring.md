# Node Authoring Guide

This guide explains how to author new nodes for the ranking DSL pipeline.

## Overview

There are two types of nodes:

| Type | Language | Author | Use Case |
|------|----------|--------|----------|
| **njs** | JavaScript | Ranking Engineers | Custom scoring logic, experimentation |
| **core** | C++ | Infra Engineers | High-performance primitives |

## Available Nodes

| Node | Description |
|------|-------------|
| `core:sourcer` | Generates initial candidate batch with IDs and base scores |
| `core:merge` | Merges and deduplicates candidates (strategies: `first`, `max_base`) |
| `core:features` | Populates feature columns (freshness, embeddings, etc.) |
| `core:model` | Runs ML model scoring on candidates |
| `core:score_formula` | Evaluates Expr IR and writes result to output key |
| `njs` | Runs custom JavaScript modules for extensible logic |

---

## For Ranking Engineers: njs Nodes

njs modules are JavaScript nodes that run in a sandboxed QuickJS runtime. They provide a safe way to implement custom ranking logic.

### Quick Start

Create a file in `njs/<module_name>/<module_name>.njs`:

```javascript
exports.meta = {
  name: "my_scorer",
  version: "1.0.0",
  reads: [Keys.SCORE_BASE, Keys.FEAT_FRESHNESS],
  writes: [Keys.SCORE_FINAL],
  budget: {
    max_write_bytes: 1048576,
    max_write_cells: 100000
  }
};

exports.runBatch = function(objs, ctx, params) {
  var n = ctx.batch.rowCount();

  // Read inputs (zero-copy Float32Array views)
  var baseScores = ctx.batch.f32(Keys.SCORE_BASE);
  var freshness = ctx.batch.f32(Keys.FEAT_FRESHNESS);

  // Allocate output column
  var output = ctx.batch.writeF32(Keys.SCORE_FINAL);

  // Vectorized processing
  for (var i = 0; i < n; i++) {
    output[i] = baseScores[i] * params.alpha + freshness[i] * params.beta;
  }

  return undefined;  // Signal column-level API was used
};
```

### The `meta` Object

Every njs module must export a `meta` object:

| Field | Type | Required | Description |
|-------|------|----------|-------------|
| `name` | string | Yes | Stable module identifier |
| `version` | string | Yes | Semantic version |
| `reads` | Key[] | Yes | Keys this module reads |
| `writes` | Key[] | Yes | Keys this module can write (enforced at runtime) |
| `params` | object | No | Parameter schema with types/defaults |
| `budget` | object | No | Execution limits |
| `capabilities` | object | No | Host capabilities (e.g., `io.csv_read`) |

#### Budget Options

```javascript
budget: {
  max_write_bytes: 1048576,   // Max bytes written (default: 1MB)
  max_write_cells: 100000,    // Max cells written (default: 100k)
  max_set_per_obj: 10,        // Max obj.set() calls per row
  max_io_read_bytes: 1048576, // For IO capability
  max_io_read_rows: 1000      // For IO capability
}
```

### ctx.batch API (Column-Level)

The recommended approach for performance. Provides zero-copy access to typed columns.

#### Read Operations

```javascript
// Get row count
var n = ctx.batch.rowCount();

// Read f32 column as Float32Array (read-only)
var scores = ctx.batch.f32(Keys.SCORE_BASE);

// Read f32vec column (embeddings)
var embedView = ctx.batch.f32vec(Keys.FEAT_EMBEDDING);
// Returns: { data: Float32Array(N*D), dim: D, row_count: N }

// Read i64 column as BigInt64Array
var ids = ctx.batch.i64(Keys.CAND_CANDIDATE_ID);
```

#### Write Operations

```javascript
// Allocate writable f32 column
var output = ctx.batch.writeF32(Keys.SCORE_FINAL);
for (var i = 0; i < n; i++) {
  output[i] = compute_score(i);
}

// Allocate writable f32vec column with dimension
var embeddings = ctx.batch.writeF32Vec(Keys.FEAT_OUTPUT, 128);

// Allocate writable i64 column
var output_ids = ctx.batch.writeI64(Keys.NEW_ID_KEY);
```

### Return Semantics

- Return `undefined` or `null` when using column-level `ctx.batch.write*` APIs
- Return `RowView[]` when using row-level `obj.set()` API

### Enforcement

1. **Write protection**: `writeF32(key)` throws if `key` not in `meta.writes`
2. **Type checking**: Writing wrong type throws (e.g., `writeI64` on f32 key)
3. **Budget limits**: Allocations exceeding limits throw

### Available Keys

Keys are generated from `keys/registry.yaml` into `keys/generated/keys.njs`:

```javascript
Keys.SCORE_BASE           // f32: base retrieval score
Keys.SCORE_ML             // f32: ML model score
Keys.SCORE_FINAL          // f32: final ranking score
Keys.FEAT_EMBEDDING       // f32vec: embedding vector
Keys.FEAT_FRESHNESS       // f32: freshness score
```

See `keys/generated/keys.njs` for the full list.

### Optional: IO Capability

For reading external data files:

```javascript
exports.meta = {
  name: "csv_reader",
  version: "1.0.0",
  reads: [Keys.SCORE_BASE],
  writes: [Keys.SCORE_ML],
  capabilities: {
    io: { csv_read: true }
  },
  budget: {
    max_write_bytes: 1048576,
    max_write_cells: 100000,
    max_io_read_bytes: 1048576,
    max_io_read_rows: 1000
  }
};

exports.runBatch = function(objs, ctx, params) {
  var csv = ctx.io.readCsv(params.csv_file);
  // ... use csv data
};
```

**Note**: IO capability requires engine policy approval.

---

## For Infra Engineers: Core C++ Nodes

Core nodes are compiled into the ranking engine for maximum performance.

### Quick Start

Create a file at `engine/src/nodes/core/my_node.cpp`:

```cpp
#include "nodes/node_runner.h"
#include "nodes/registry.h"
#include "keys.h"
#include "object/batch_builder.h"
#include "object/typed_column.h"
#include <nlohmann/json.hpp>

namespace ranking_dsl {

class MyNode : public NodeRunner {
 public:
  CandidateBatch Run(const ExecContext& ctx,
                     const CandidateBatch& input,
                     const nlohmann::json& params) override {
    // 1. Extract params from JSON
    float alpha = params.value("alpha", 1.0f);
    int32_t output_key = params.value("output_key_id", keys::id::SCORE_FINAL);

    // 2. Read input columns (typed fast path)
    auto* score_col = input.GetF32Column(keys::id::SCORE_BASE);
    size_t row_count = input.RowCount();

    // 3. Create output column
    auto out_col = std::make_shared<F32Column>(row_count);
    for (size_t i = 0; i < row_count; ++i) {
      float val = score_col ? score_col->Get(i) : 0.0f;
      out_col->Set(i, val * alpha);
    }

    // 4. Use BatchBuilder for COW semantics
    BatchBuilder builder(input);
    builder.AddF32Column(output_key, out_col);
    return builder.Build();  // Shares unmodified columns
  }

  std::string TypeName() const override { return "core:my_node"; }
};

REGISTER_NODE_RUNNER("core:my_node", MyNode);

}  // namespace ranking_dsl
```

### NodeRunner Interface

```cpp
class NodeRunner {
 public:
  virtual ~NodeRunner() = default;

  virtual CandidateBatch Run(const ExecContext& ctx,
                             const CandidateBatch& input,
                             const nlohmann::json& params) = 0;

  virtual std::string TypeName() const = 0;
};

struct ExecContext {
  const KeyRegistry* registry = nullptr;  // For type validation
};
```

### Typed Column APIs

#### Reading Columns

```cpp
F32Column* GetF32Column(int32_t key_id);       // float values
I64Column* GetI64Column(int32_t key_id);       // int64 values
F32VecColumn* GetF32VecColumn(int32_t key_id); // N×D embeddings
```

#### Creating Columns

```cpp
auto col = std::make_shared<F32Column>(row_count);
col->Set(i, value);
float* raw = col->Data();  // Direct buffer access

auto vec_col = std::make_shared<F32VecColumn>(row_count, dim);
vec_col->Set(i, std::vector<float>{...});
```

### BatchBuilder (Copy-on-Write)

```cpp
BatchBuilder builder(input);               // Wrap source batch
builder.AddF32Column(key_id, col);         // Add/replace column
ColumnBatch output = builder.Build();      // Shares unchanged columns
```

### Plan JSON Node Envelope

```json
{
  "id": "my_step",
  "op": "core:my_node",
  "inputs": ["previous_node"],
  "params": {"alpha": 0.5, "output_key_id": 3002},
  "trace_key": "my_step_trace"
}
```

| Field | Description |
|-------|-------------|
| `id` | Unique node identifier in the plan |
| `op` | Operation type (matches `REGISTER_NODE_RUNNER`) |
| `inputs` | List of input node IDs (DAG edges) |
| `params` | JSON object passed to `Run()` |
| `trace_key` | Optional stable identifier for tracing (1-64 chars) |

### Key Constants

Use generated constants from `keys.h`:

```cpp
keys::id::SCORE_BASE        // 3001
keys::id::SCORE_ML          // 3002
keys::id::FEAT_EMBEDDING    // 1002
```

### Type Validation

```cpp
const auto* info = ctx.registry->GetById(key_id);
if (info && info->type == keys::KeyType::F32) {
  // Valid F32 key
}
```

### Build Integration

Add your `.cpp` file to `engine/CMakeLists.txt`:

```cmake
add_library(ranking_engine
  # ... existing sources ...
  src/nodes/core/my_node.cpp
)
```

---

## Code Review Requirements

All node changes require code review:

- **njs modules** (`njs/**`): Ranking Infra team approval via CODEOWNERS
- **core nodes** (`engine/src/nodes/**`): Engine team approval

Follow the standard PR workflow described in `CLAUDE.md`.
