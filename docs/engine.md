# Ranking DSL Engine (C++)

This document describes the C++ engine implementation for the Ranking DSL.

## Overview

The engine executes compiled ranking plans against candidate batches. It uses a **columnar (Structure-of-Arrays) data model** with typed columns for efficient memory access and zero-copy JS integration.

## Architecture

```
┌─────────────────────────────────────────────────────────────────────┐
│                           Executor                                   │
│  ┌─────────────┐   ┌─────────────┐   ┌─────────────┐               │
│  │   sourcer   │ → │   merge     │ → │  features   │ → ...         │
│  └─────────────┘   └─────────────┘   └─────────────┘               │
│         │                 │                 │                       │
│         ▼                 ▼                 ▼                       │
│  ┌─────────────────────────────────────────────────────────────┐   │
│  │                     ColumnBatch                              │   │
│  │  ┌──────────┬──────────┬──────────┬──────────┐              │   │
│  │  │F32Column │I64Column │F32VecCol │ ...      │              │   │
│  │  │[0.5,0.6] │[100,200] │[N×D data]│          │              │   │
│  │  └──────────┴──────────┴──────────┴──────────┘              │   │
│  └─────────────────────────────────────────────────────────────┘   │
└─────────────────────────────────────────────────────────────────────┘
```

## Core Components

### 1. Typed Columns (`object/typed_column.h`)

The engine uses typed columns for contiguous storage:

| Column Type | Storage | Use Case |
|-------------|---------|----------|
| `F32Column` | `vector<float>` | Scores, numeric features |
| `I64Column` | `vector<int64_t>` | IDs, counts |
| `BoolColumn` | `vector<bool>` | Flags |
| `StringColumn` | `vector<string>` | Text data |
| `F32VecColumn` | `vector<float>` (N×D) | Embeddings |
| `BytesColumn` | `vector<vector<uint8_t>>` | Binary data |

**F32VecColumn Contiguous Storage:**

```cpp
// N rows, D dimensions - stored row-major
F32VecColumn col(3, 4);  // 3 rows, 4 dims

// Data layout: [r0d0, r0d1, r0d2, r0d3, r1d0, r1d1, ...]
col.Set(0, {1.0f, 2.0f, 3.0f, 4.0f});
col.Set(1, {5.0f, 6.0f, 7.0f, 8.0f});

// Zero-copy access
const float* data = col.Data();      // All N*D floats
const float* row1 = col.GetRow(1);   // Pointer to row 1
```

**Usage:**

```cpp
#include "object/typed_column.h"

// Create typed columns
auto score_col = std::make_shared<F32Column>(100);
score_col->Set(0, 0.95f);
float val = score_col->Get(0);  // Fast typed access

// Zero-copy pointer access
float* data = score_col->Data();
for (size_t i = 0; i < 100; i++) {
    data[i] = compute_score(i);  // Direct writes
}
```

### 2. ColumnBatch (`object/column_batch.h`)

Container for typed columns keyed by registry key IDs:

```cpp
#include "object/column_batch.h"

ColumnBatch batch(100);  // 100 rows

// Add columns
auto ids = std::make_shared<I64Column>(100);
auto scores = std::make_shared<F32Column>(100);

batch.SetColumn(keys::id::CAND_CANDIDATE_ID, ids);
batch.SetColumn(keys::id::SCORE_BASE, scores);

// Typed accessors (fast path)
F32Column* score_col = batch.GetF32Column(keys::id::SCORE_BASE);
if (score_col) {
    float first = score_col->Get(0);
}

// Generic access (slower)
Value val = batch.GetValue(0, keys::id::SCORE_BASE);
```

### 3. BatchBuilder (`object/batch_builder.h`)

Copy-on-write builder for creating modified batches:

```cpp
#include "object/batch_builder.h"

// Start from existing batch
BatchBuilder builder(source_batch);

// Add new column (original columns shared)
auto output = std::make_shared<F32Column>(source_batch.RowCount());
// ... fill output ...
builder.AddF32Column(keys::id::SCORE_FINAL, output);

// Build result - unchanged columns are shared (no copy)
ColumnBatch result = builder.Build();

// COW: modify existing column triggers copy
builder.Set(0, keys::id::SCORE_BASE, 0.99f);  // Copies SCORE_BASE
ColumnBatch result2 = builder.Build();
```

### 4. RowView (`object/row_view.h`)

Row-level access over a ColumnBatch:

```cpp
#include "object/row_view.h"

ColumnBatch batch(3);
// ... add columns ...

// Read-only view
RowView view(&batch, 1);  // Row index 1
auto val = view.Get(keys::id::SCORE_BASE);  // Returns optional<Value>

// Writable view (with BatchBuilder)
BatchBuilder builder(batch);
RowView writable(&batch, 1, &builder);
RowView new_view = writable.Set(keys::id::SCORE_FINAL, 0.99f);
```

### 5. Expression Evaluation (`expr/expr.h`)

Expr IR evaluation against columns:

```cpp
#include "expr/expr.h"

// Parse from JSON
auto json = nlohmann::json::parse(R"({
    "op": "mul",
    "args": [
        {"op": "const", "value": 0.7},
        {"op": "signal", "key_id": 3001}
    ]
})");

std::string error;
ExprNode expr = ParseExpr(json, &error);

// Evaluate at row index
float result = EvalExpr(expr, batch, row_index, &registry);
```

**Supported Ops:**

| Op | JSON | Description |
|----|------|-------------|
| `const` | `{"op":"const","value":0.7}` | Constant value |
| `signal` | `{"op":"signal","key_id":3001}` | Read column value |
| `add` | `{"op":"add","args":[...]}` | Sum of args |
| `mul` | `{"op":"mul","args":[...]}` | Product of args |
| `min` | `{"op":"min","args":[...]}` | Minimum |
| `max` | `{"op":"max","args":[...]}` | Maximum |
| `clamp` | `{"op":"clamp","x":...,"lo":...,"hi":...}` | Clamp to range |
| `cos` | `{"op":"cos","a":...,"b":...}` | Cosine similarity |
| `penalty` | `{"op":"penalty","name":"..."}` | Read penalty key |

### 6. BatchContext (`nodes/js/batch_context.h`)

Zero-copy API for njs module integration:

```cpp
#include "nodes/js/batch_context.h"

// Read APIs (zero-copy where possible)
auto [data, size] = ctx.GetF32Raw(keys::id::SCORE_BASE);
if (data) {
    for (size_t i = 0; i < size; i++) {
        float val = data[i];  // Direct access
    }
}

// F32Vec read (contiguous N*D)
F32VecView view = ctx.GetF32VecRaw(keys::id::FEAT_EMBEDDING);
// view.data = pointer to N*D floats
// view.dim = dimension per row
// view.row_count = number of rows
const float* row2 = view.GetRow(2);  // Pointer to row 2

// Write APIs (allocate then direct-write)
float* output = ctx.AllocateF32(keys::id::SCORE_FINAL);
for (size_t i = 0; i < ctx.RowCount(); i++) {
    output[i] = compute(i);
}

// F32Vec write
float* embed_out = ctx.AllocateF32Vec(keys::id::FEAT_OUTPUT, 128);
// Write N*128 floats directly

ctx.Commit();  // Finalize to builder
```

**Enforcement:**

```cpp
// meta.writes enforcement
std::set<int32_t> allowed = {keys::id::SCORE_FINAL};
BatchContext ctx(batch, builder, &registry, allowed, budget);

ctx.AllocateF32(keys::id::SCORE_FINAL);  // OK
ctx.AllocateF32(keys::id::SCORE_BASE);   // Throws: not in meta.writes

// Type enforcement
ctx.AllocateI64(keys::id::SCORE_FINAL);  // Throws: SCORE_FINAL is f32

// Budget enforcement
NjsBudget budget;
budget.max_write_bytes = 1000;
budget.max_write_cells = 50;
// Throws if exceeded
```

### 7. Node Runners (`nodes/`)

Core nodes operate on ColumnBatch:

```cpp
// Node interface
class NodeRunner {
public:
    virtual CandidateBatch Run(
        const ExecContext& ctx,
        const std::vector<CandidateBatch>& inputs,
        const nlohmann::json& params) = 0;
};

// Available core nodes:
// - core:sourcer - Generate initial candidates
// - core:merge - Merge and deduplicate batches
// - core:features - Add feature columns
// - core:model - Run model inference (stub)
// - core:score_formula - Evaluate Expr IR, write output column
```

### 8. Executor (`executor/executor.h`)

Runs compiled plans:

```cpp
#include "executor/executor.h"

KeyRegistry registry;
registry.LoadFromCompiled();

Executor executor(registry);

std::string error;
CandidateBatch result = executor.Execute(compiled_plan, &error);

// Access results
for (size_t i = 0; i < result.RowCount(); i++) {
    auto* score_col = result.GetF32Column(keys::id::SCORE_FINAL);
    float score = score_col ? score_col->Get(i) : 0.0f;
}
```

## Plan Compilation and Validation

The engine compiler performs several validation passes on plans:

### 1. Plan Environment Validation

The compiler validates `plan.meta.env` and enforces stability requirements:

```cpp
#include "plan/compiler.h"

PlanCompiler compiler(registry);
std::string error;

// ParsePlan validates meta.env is "prod", "dev", or "test" (exact match)
Plan plan;
if (!ParsePlan(plan_json, plan, &error)) {
  // Invalid env value rejected here
}

// ValidatePlanEnv enforces experimental node restrictions
if (!compiler.ValidatePlanEnv(plan, &error)) {
  // Production plan using experimental node rejected here
}
```

**Environment enforcement:**
- Plans with `meta.env: "prod"` cannot use nodes with `stability: experimental`
- Plans with `meta.env: "dev"` or `meta.env: "test"` can use experimental nodes
- Invalid env values (e.g., `"Prod"`, `"production"`) are rejected at parse time
- Default env is `"dev"` if not specified

### 2. Other Validation Passes

- DAG acyclic validation
- Node op resolution
- Key existence validation
- Complexity budget enforcement

## Key Registry

Keys are defined in `keys/registry.yaml` and generated to:
- `keys/generated/keys.json` - Loaded by engine at runtime
- `keys/generated/keys.h` - C++ compile-time constants

```cpp
#include "keys.h"  // Generated

// Use key IDs
batch.SetColumn(keys::id::SCORE_BASE, col);
batch.GetF32Column(keys::id::SCORE_FINAL);
```

## Building

```bash
cd engine
mkdir build && cd build
cmake ..
make -j4

# Run tests
ctest --output-on-failure

# Run engine
./rankdsl_engine ../path/to/plan.json
```

## Tests

| Test File | Coverage |
|-----------|----------|
| `column_batch_test.cpp` | TypedColumn, ColumnBatch, COW semantics |
| `row_view_test.cpp` | RowView read/write, type enforcement |
| `columnar_eval_test.cpp` | Expression evaluation on typed columns |
| `njs_runner_test.cpp` | BatchContext APIs, enforcement, budget |
| `expr_eval_test.cpp` | Expr IR ops, edge cases |
| `plan_compiler_test.cpp` | Plan validation, compilation |
| `key_enforcement_test.cpp` | Type mismatch rejection |

Run all tests:
```bash
cd engine/build
ctest --output-on-failure
```

## Performance Considerations

1. **Use typed accessors** - `GetF32Column()` is faster than `GetValue()`
2. **Zero-copy reads** - `GetF32Raw()` returns pointer, no allocation
3. **Contiguous writes** - `AllocateF32()` returns pointer for bulk writes
4. **Column sharing** - BatchBuilder shares unchanged columns (COW)
5. **F32VecColumn** - Contiguous N×D enables SIMD and zero-copy JS views
