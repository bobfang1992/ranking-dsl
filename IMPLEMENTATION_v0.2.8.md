# Implementation Summary: Spec v0.2.8 - Namespaced Generated APIs

## Overview

This document summarizes the implementation of spec v0.2.8, which introduces:
- **No more `.node(op: string)` API** - replaced with generated, namespaced methods
- **C++/njs as SSOT** for node metadata → generated TypeScript bindings
- **Experimental → Stable graduation** workflow
- **Plan env (prod/dev/test)** enforcement

---

## ✅ Completed Work

### 1. Spec Update
**File:** `docs/spec.md`
- Updated to v0.2.8 with all new requirements
- Documented namespaced API requirements
- Added experimental graduation workflow
- Defined plan env enforcement rules

### 2. C++ NodeSpec Infrastructure
**Files:**
- `engine/src/nodes/registry.h` (lines 17-112)
- `engine/src/nodes/registry.cpp` (lines 1-56)

**Implemented:**
- `NodeSpec` struct with all required fields:
  - `op`, `namespace_path`, `stability`, `doc`
  - `params_schema_json` (JSON Schema)
  - `reads` (KeyId list)
  - `WritesDescriptor` (static or param-derived)
  - Optional: `budgets_json`, `capabilities_json`
- `Stability` enum (`kStable`, `kExperimental`)
- `WritesDescriptor` with `Kind::{kStatic, kParamDerived}`
- Updated `NodeRegistry`:
  - `Register(op, factory, NodeSpec)`
  - `GetSpec(op) -> NodeSpec*`
  - `GetAllSpecs() -> vector<NodeSpec>` (sorted by namespace_path)
- Updated `REGISTER_NODE_RUNNER` macro to accept NodeSpec

### 3. Core Node Registrations with NodeSpec
**Files updated:**
- `engine/src/nodes/core/features.cpp` (lines 82-113)
- `engine/src/nodes/core/sourcer.cpp` (lines 49-85)
- `engine/src/nodes/core/merge.cpp` (lines 124-155)
- `engine/src/nodes/core/model.cpp` (lines 64-94)
- `engine/src/nodes/core/score_formula.cpp` (lines 63-100)

**Each node now has:**
- Full JSON Schema for params
- Documented reads/writes
- namespace_path (e.g., `core.features`, `core.merge`, `core.score`)
- stability = `kStable`
- Comprehensive docstrings

**Notable implementations:**
- `core:features` uses param-derived writes (`writes.param_name = "keys"`)
- `core:sourcer` has static writes (candidate_id, score.base)
- `core:merge` preserves all columns (no new writes)

### 4. Engine NodeSpec Export Utility
**Files:**
- `engine/src/export_nodes.cpp` (lines 1-109)
- `engine/CMakeLists.txt` (lines 154-156)

**Implemented:**
- Standalone executable `rankdsl_export_nodes`
- Exports all registered NodeSpecs as JSON to stdout
- Includes key name lookups via KeyRegistry
- Converts WritesDescriptor to JSON format
- Added to CMake build

### 5. TypeScript nodes-codegen Package
**Files:**
- `tools/nodes-codegen/package.json`
- `tools/nodes-codegen/src/export.ts` (lines 1-167)
- `tools/nodes-codegen/src/codegen.ts` (lines 1-194)
- `tools/nodes-codegen/src/index.ts`
- `tools/nodes-codegen/tsconfig.json`

**export.ts implements:**
- `exportCoreNodes()` - calls `rankdsl_export_nodes` executable
- `exportNjsNodes()` - scans `njs/` directory, extracts exports.meta
- Computes SHA256 digest for njs pinning (`js:path@version#digest`)
- Validates stability/namespace consistency
- Outputs `nodes/generated/nodes.yaml`

**codegen.ts implements:**
- `generateBindings()` - generates TypeScript interfaces from NodeSpecs
- Builds nested namespace tree (e.g., `core.merge.weightedUnion`)
- Generates docstrings with reads/writes/stability metadata
- Creates `PipelineWithNodes` interface
- Exports `NODE_REGISTRY` for runtime validation
- Outputs `nodes/generated/nodes.ts`

### 6. CLI Commands
**File:** `tools/cli/src/index.ts` (lines 145-205)

**Added commands:**
- `rankdsl nodes export` - export node catalog from C++ + njs
  - Options: `--engine-build`, `--njs`, `-o/--output`
- `rankdsl nodes codegen` - generate TS bindings from catalog
  - Options: `-i/--input`, `-o/--output`
- `rankdsl nodes graduate <node>` - graduation workflow (stub)

### 7. Package Configuration
**File:** `package.json` (line 9)
- Added `tools/nodes-codegen` to workspaces

---

## 🚧 Remaining Work

### A. Plan Builder DSL Updates (TypeScript)

**1. Add `dsl.planEnv()` support**

**File to update:** `tools/plan-compiler/src/dsl.ts`

Add:
```typescript
let planEnv: "prod" | "dev" | "test" | null = null;

export function planEnv(env: "prod" | "dev" | "test"): void {
  planEnv = env;
}

// In Pipeline.build():
export class Pipeline {
  build(options?: object): Plan {
    const plan: Plan = {
      version: "0.2.8",
      meta: {
        env: planEnv || detectEnvFromFilename() || "dev",
      },
      ...
    };
    ...
  }
}
```

**2. Remove public `.node()` API**

**File to update:** `tools/plan-compiler/src/dsl.ts`

Change:
```typescript
export class Pipeline {
  // Make this private or remove entirely
  private _node(op: string, params: object, opts?: NodeOpts): Pipeline { ... }

  // Public API should only have generated namespaces
  // (will be added after codegen)
}
```

**3. Integrate generated namespaces**

After running `rankdsl nodes codegen`, the generated `nodes.ts` will contain:
- `PipelineWithNodes` interface with namespaced methods
- Runtime `NODE_REGISTRY` for validation

Update `dsl.ts` to:
```typescript
import type { PipelineWithNodes } from '../../nodes/generated/nodes.js';

export class Pipeline implements PipelineWithNodes {
  // Implement namespaced methods by delegating to internal _node()
  core = {
    features: (params, opts?) => this._node('core:features', params, opts),
    merge: (params, opts?) => this._node('core:merge', params, opts),
    model: (params, opts?) => this._node('core:model', params, opts),
    score: (params, opts?) => this._node('core:score_formula', params, opts),
  };

  // njs, experimental, etc.
}
```

### B. Engine Compiler Enforcement (C++)

**1. Read and enforce `plan.meta.env`**

**File to update:** `engine/src/plan/compiler.cpp`

Add:
```cpp
bool PlanCompiler::Compile(const Plan& plan, CompiledPlan& output, std::string* error) {
  // Read plan env
  std::string env = plan.meta.value("env", "dev");

  // For each node in plan:
  for (const auto& node_json : plan.nodes) {
    std::string op = node_json["op"];
    const NodeSpec* spec = NodeRegistry::Instance().GetSpec(op);

    if (!spec) {
      *error = fmt::format("Unknown node op: {}", op);
      return false;
    }

    // Enforce experimental nodes not in prod
    if (env == "prod" && spec->stability == Stability::kExperimental) {
      *error = fmt::format(
        "Production plans cannot use experimental nodes. "
        "Node '{}' (namespace: {}) has stability=experimental.",
        op, spec->namespace_path
      );
      return false;
    }
  }

  // Continue with existing compilation...
}
```

**2. Validate node existence**

Already implemented via `NodeRegistry::GetSpec()` check above.

### C. Example njs Module with v0.2.8 Metadata

**File to create:** `njs/example/normalize_score.njs`

```javascript
exports.meta = {
  name: "normalize_score",
  version: "1.0.0",
  namespace_path: "njs.normalize.score",
  stability: "stable",
  doc: "Normalizes scores to [0, 1] range using min-max normalization",
  reads: [3005],  // score.final
  writes: [3006], // score.normalized (add to keys/registry.yaml)
  params_schema: {
    type: "object",
    properties: {
      min: { type: "number", default: 0 },
      max: { type: "number", default: 1 }
    }
  },
  budget: {
    max_ms_per_1k: 5,
    max_set_per_obj: 1
  }
};

exports.runBatch = function(objs, ctx, params) {
  const scores = objs.map(obj => obj.get(3005));
  const minScore = Math.min(...scores);
  const maxScore = Math.max(...scores);
  const range = maxScore - minScore;

  return objs.map((obj, i) => {
    const score = scores[i];
    const normalized = range > 0 ? (score - minScore) / range : 0.5;
    return obj.set(3006, normalized);
  });
};
```

### D. Tests

**1. TS typecheck test**

**File:** `plans/example.prod.plan.ts`

```typescript
import { dsl, Keys } from '../tools/plan-compiler/dist/dsl.js';

dsl.planEnv("prod");

const plan = dsl.candidates([dsl.sourcer("test", { k: 10 })])
  .core.features({ keys: [Keys.feat_freshness] })
  .core.model({ name: "vm_v1" })
  .core.score({ expr: dsl.F.signal(Keys.score_ml) })
  .build();

export default plan;
```

Run: `tsc --noEmit plans/example.prod.plan.ts`

**2. Engine enforcement test**

**File:** `engine/tests/plan_env_test.cpp`

```cpp
TEST_CASE("PlanCompiler rejects experimental nodes in prod", "[plan_env]") {
  // Create a plan with env=prod and an experimental node
  // Expect compile to fail with clear error message
}

TEST_CASE("PlanCompiler allows experimental nodes in dev", "[plan_env]") {
  // Create a plan with env=dev and an experimental node
  // Expect compile to succeed
}
```

**3. Export/codegen determinism test**

**File:** `tools/nodes-codegen/tests/determinism.test.ts`

```typescript
test('export produces stable YAML', () => {
  const result1 = exportNodes(...);
  const result2 = exportNodes(...);
  expect(result1.yaml).toBe(result2.yaml);
});

test('codegen produces stable TypeScript', () => {
  const result1 = codegenNodes(...);
  const result2 = codegenNodes(...);
  expect(result1.ts).toBe(result2.ts);
});
```

### E. Graduation Map File (Interim Solution)

Since rewriting C++ source is complex, use a mapping file for graduation:

**File:** `nodes/graduations.yaml`

```yaml
graduations:
  - from_namespace: experimental.core.myNewNode
    to_namespace: core.myNewNode
    stability: stable
```

Update `export.ts` to apply graduations when loading specs.

Update `rankdsl nodes graduate` to:
1. Validate node exists and is experimental
2. Check target namespace doesn't conflict
3. Update graduations.yaml
4. Re-run export + codegen

---

## 📋 Build & Test Instructions

### 1. Build C++ Engine

```bash
cd engine
cmake -S . -B build -DCMAKE_BUILD_TYPE=Release
cmake --build build
```

This builds:
- `build/rankdsl_engine` - main engine
- `build/rankdsl_export_nodes` - node export utility
- `build/ranking_dsl_tests` - test suite

### 2. Install TypeScript Dependencies

```bash
npm ci
```

### 3. Build TypeScript Tooling

```bash
npm run build
```

### 4. Export Node Catalog

```bash
npm run rankdsl nodes export
```

Output: `nodes/generated/nodes.yaml`

### 5. Generate TypeScript Bindings

```bash
npm run rankdsl nodes codegen
```

Output: `nodes/generated/nodes.ts`

### 6. Run Tests

```bash
# TypeScript tests
npm test

# C++ tests
cd engine/build
ctest --output-on-failure
```

---

## 🔍 Key File Locations

### C++ NodeSpec Infrastructure
- **NodeSpec definition:** `engine/src/nodes/registry.h:17-112`
- **NodeRegistry implementation:** `engine/src/nodes/registry.cpp:1-56`
- **Export utility:** `engine/src/export_nodes.cpp:1-109`

### Core Node Registrations
- **features:** `engine/src/nodes/core/features.cpp:82-113`
- **sourcer:** `engine/src/nodes/core/sourcer.cpp:49-85`
- **merge:** `engine/src/nodes/core/merge.cpp:124-155`
- **model:** `engine/src/nodes/core/model.cpp:64-94`
- **score_formula:** `engine/src/nodes/core/score_formula.cpp:63-100`

### TypeScript Tooling
- **Export implementation:** `tools/nodes-codegen/src/export.ts:1-167`
- **Codegen implementation:** `tools/nodes-codegen/src/codegen.ts:1-194`
- **CLI commands:** `tools/cli/src/index.ts:145-205`

### Generated Outputs
- **Node catalog:** `nodes/generated/nodes.yaml` (generated)
- **TS bindings:** `nodes/generated/nodes.ts` (generated)

---

## 🎯 Next Steps

1. **Complete Plan Builder DSL** (Section A)
   - Add `dsl.planEnv()`
   - Remove public `.node()` API
   - Integrate generated namespaces

2. **Add Engine Enforcement** (Section B)
   - Read `plan.meta.env`
   - Reject experimental nodes in prod
   - Validate node existence

3. **Create Example njs Module** (Section C)
   - Demonstrate v0.2.8 metadata format

4. **Write Tests** (Section D)
   - TS typecheck test
   - Engine enforcement tests
   - Determinism tests

5. **Implement Graduation** (Section E)
   - Create graduations.yaml
   - Implement `rankdsl nodes graduate`

6. **CI Integration**
   - Add `rankdsl nodes export --check` to CI
   - Add `rankdsl nodes codegen --check` to CI
   - Fail if generated files are stale

---

## 💡 Design Decisions

### Why separate export utility?
- C++ engine exports its own NodeSpecs as authoritative source
- TypeScript CLI combines C++ + njs exports
- Clean separation of concerns

### Why YAML for node catalog?
- Human-readable for code review
- Easy diffs in version control
- Standard format for structured data

### Why namespace tree structure?
- Prevents API explosion (100s of nodes)
- Logical grouping (core.*, njs.*, experimental.*)
- Clear experimental boundary

### Why param-derived writes?
- Correctness: `features({keys:[...]})` writes depend on runtime params
- Engine needs this to validate writes enforcement
- Future: could extract from params at compile time

---

## 🐛 Known Limitations (MVP)

1. **Param schema → TS type generation**
   - Currently uses `Record<string, any>`
   - Production should use JSON Schema → TS codegen

2. **Reads analysis for expressions**
   - `core:score_formula` reads depend on expression content
   - Currently marked as empty reads
   - Could be improved with static expression analysis

3. **njs meta extraction**
   - Uses simple regex parsing
   - Production should use proper JS parser (e.g., Babel)

4. **Graduation workflow**
   - Currently a stub
   - Interim solution uses graduations.yaml mapping file
   - Full solution would rewrite C++ source files

5. **No experimental namespace enforcement in njs**
   - njs modules should validate their own namespace_path
   - Could add linter to enforce experimental.* naming

---

## ✨ Summary

This implementation provides a **foundation for namespaced, type-safe node APIs** with:
- ✅ C++/njs as source-of-truth for node metadata
- ✅ Automated TypeScript binding generation
- ✅ Clear experimental/stable boundary
- ✅ Deterministic catalog export

**Completion: ~70%**

Remaining work focuses on:
- Plan builder DSL integration
- Engine enforcement
- Testing
- Graduation workflow

All core infrastructure is in place and ready for integration.
