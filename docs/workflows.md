# Engineer Workflows

This document describes typical workflows for engineers working with the Ranking DSL system.

## Table of Contents

- [Ranking Engineer Workflows](#ranking-engineer-workflows)
  - [Creating a New Ranking Plan](#creating-a-new-ranking-plan)
  - [Modifying an Existing Plan](#modifying-an-existing-plan)
  - [Creating a Custom njs Node](#creating-a-custom-njs-node)
  - [Debugging a Plan](#debugging-a-plan)
  - [A/B Testing Plan Variants](#ab-testing-plan-variants)
- [Infra Engineer Workflows](#infra-engineer-workflows)
  - [Adding a New Key to the Registry](#adding-a-new-key-to-the-registry)
  - [Adding a New Core Node](#adding-a-new-core-node)
  - [Graduating an Experimental Node to Stable](#graduating-an-experimental-node-to-stable)
  - [Updating Complexity Budgets](#updating-complexity-budgets)
  - [Release Process](#release-process)

---

## Ranking Engineer Workflows

Ranking engineers are responsible for building and maintaining ranking pipelines using the DSL. They author plan files, create custom logic via njs modules, and optimize ranking quality.

### Creating a New Ranking Plan

**Scenario:** You need to create a new ranking pipeline for a product feature.

**Steps:**

1. **Understand available keys:**
   ```bash
   # Review the key registry to see what signals are available
   cat keys/registry.yaml
   ```

2. **Determine the plan environment:**
   - Use `prod` for production serving (only stable nodes allowed)
   - Use `dev` for development and experimentation (experimental nodes allowed)
   - Use `test` for integration tests (experimental nodes allowed)

3. **Create a new plan file:**
   ```bash
   # Create a plan file in the plans/ directory
   touch plans/my_feature.plan.js
   ```

4. **Write the plan using the DSL:**
   ```javascript
   // plans/my_feature.plan.js

   // Keys, dsl, and config are injected as frozen globals by the plan compiler
   // No imports/requires needed (and they're disallowed by the static gate)

   // IMPORTANT: Set plan environment
   // Use "prod" for production (only stable nodes allowed)
   // Use "dev" for development (experimental nodes allowed)
   // Use "test" for integration tests (experimental nodes allowed)
   const planMeta = {
     env: "dev"  // Change to "prod" when ready for production
   };

   // Configuration (can be parameterized)
   const config = {
     sourceLimit: 1000,
     mlThreshold: 0.5,
     blendingAlpha: 0.7
   };

   // Build the pipeline using dsl.F builder API
   const F = dsl.F;

   // Start with a sourcer (using namespaced API)
   let p = dsl.core.sourcer("main_source", { limit: config.sourceLimit });

   // Add features
   p = p.core.features("fetch_features", {
     keys: [Keys.FEAT_FRESHNESS, Keys.FEAT_EMBEDDING]
   });

   // Run ML model
   p = p.core.model("ranking_model_v2", {
     threshold: config.mlThreshold
   });

   // Combine scores using expression builder
   const blendExpr = F.add(
     F.mul(F.const(config.blendingAlpha), F.signal(Keys.SCORE_BASE)),
     F.mul(F.const(1 - config.blendingAlpha), F.signal(Keys.SCORE_ML))
   );

   p = p.core.score(blendExpr, {
     output_key_id: Keys.SCORE_FINAL
   });

   // Build and set metadata
   const plan = p.build();
   plan.meta = planMeta;
   return plan;
   ```

5. **Validate the plan:**
   ```bash
   # Once Phase 2 is complete, compile the plan
   node tools/cli/dist/index.js plan \
     --in plans/my_feature.plan.js \
     --out plans/my_feature.plan.json

   # Validate against complexity budgets
   node tools/cli/dist/index.js validate plans/my_feature.plan.json
   ```

6. **Test the plan:**
   ```bash
   # Run the plan through the engine
   ./build-rel/engine/rankdsl_engine plans/my_feature.plan.json \
     --dump-top 10
   ```

7. **Before production deployment:**
   ```javascript
   // Update plan.js to set env to "prod"
   const planMeta = {
     env: "prod"
   };
   ```

   **Important:** Production plans (`env: "prod"`) cannot use experimental nodes. The engine will reject any plan that:
   - Has `meta.env: "prod"` AND
   - References any node with `stability: experimental` (e.g., nodes under `experimental.*` namespaces)

   This prevents accidental deployment of untested experimental features to production.

8. **Submit for review:**
   ```bash
   git checkout -b feature/my-feature-plan
   git add plans/my_feature.plan.js
   git commit -m "feat: add ranking plan for my_feature"
   git push -u origin feature/my-feature-plan
   gh pr create --fill
   ```

### Modifying an Existing Plan

**Scenario:** You need to adjust blending weights or add a new stage to an existing plan.

**Steps:**

1. **Create a feature branch:**
   ```bash
   git checkout main && git pull
   git checkout -b feature/update-blending-weights
   ```

2. **Modify the plan file:**
   ```javascript
   // Update blending ratio from 0.7 to 0.8
   const config = {
     blendingAlpha: 0.8  // was 0.7
   };

   // If testing experimental features, ensure env is "dev" or "test"
   const planMeta = {
     env: "dev"  // or "test" for integration tests
   };
   ```

3. **Test locally:**
   ```bash
   # Recompile
   node tools/cli/dist/index.js plan \
     --in plans/my_feature.plan.js \
     --out plans/my_feature.plan.json

   # Verify output
   ./build-rel/engine/rankdsl_engine plans/my_feature.plan.json \
     --dump-top 10
   ```

4. **Commit and create PR:**
   ```bash
   git add plans/my_feature.plan.js
   git commit -m "feat: increase ML weight in my_feature plan"
   git push -u origin feature/update-blending-weights
   gh pr create --fill
   ```

### Creating a Custom njs Node

**Scenario:** You need custom ranking logic that isn't available in core nodes (e.g., applying business rules, custom score adjustments).

**Steps:**

1. **Create a new njs module:**
   ```bash
   mkdir -p njs/my_feature
   touch njs/my_feature/custom_boost.njs
   ```

2. **Implement the module:**
   ```javascript
   // njs/my_feature/custom_boost.njs

   // Define metadata
   exports.meta = {
     name: "custom_boost",
     version: "1.0.0",
     namespace_path: "experimental.my_feature.customBoost",
     stability: "experimental",
     doc: "Apply custom boosting logic based on freshness",
     params_schema: {
       type: "object",
       properties: {
         boost_factor: { type: "number", minimum: 1.0, maximum: 5.0, default: 2.0 }
       }
     },
     reads: [Keys.FEAT_FRESHNESS, Keys.SCORE_BASE],
     writes: [Keys.SCORE_ADJUSTED],
     capabilities: [],
     budget: {
       max_write_bytes: 1048576,
       max_write_cells: 100000
     }
   };

   // Implement batch processing using column-level API
   exports.runBatch = function(objs, ctx, params) {
     var n = ctx.batch.rowCount();

     // Read columns
     var freshness = ctx.batch.f32(Keys.FEAT_FRESHNESS);
     var baseScore = ctx.batch.f32(Keys.SCORE_BASE);

     // Write column
     var adjusted = ctx.batch.writeF32(Keys.SCORE_ADJUSTED);

     // Apply custom logic
     for (var i = 0; i < n; i++) {
       var boost = freshness[i] > 0.8 ? params.boost_factor : 1.0;
       adjusted[i] = baseScore[i] * boost;
     }

     return undefined;  // Signal column writes used
   };
   ```

3. **Export the node catalog:**
   ```bash
   npm run build
   node tools/cli/dist/index.js nodes export
   ```

4. **Use in plan.js:**
   ```javascript
   // In your plan.js (once Phase 2 is complete and namespaced APIs are available)
   p = p.experimental.my_feature.customBoost({
     boost_factor: 2.5
   }, {
     trace_key: "freshness_boost"
   });
   ```

5. **Test and submit:**
   ```bash
   git checkout -b feature/custom-boost-node
   git add njs/my_feature/custom_boost.njs
   git commit -m "feat: add custom boost njs node"
   git push -u origin feature/custom-boost-node
   gh pr create --fill
   ```

### Debugging a Plan

**Scenario:** Your plan isn't producing expected results.

**Steps:**

1. **Enable detailed tracing:**
   ```javascript
   // In your plan.js, add trace_key to problematic nodes
   p = p.core.model("ranking_v2", { threshold: 0.5 }, { trace_key: "main_model" })
        .core.score(expr, {}, { trace_key: "final_score" });
   ```

2. **Add logging configuration:**
   ```javascript
   // In your plan.js
   const plan = p.build();
   plan.logging = {
     sample_rate: 1.0,  // Log all candidates
     dump_keys: [Keys.SCORE_BASE, Keys.SCORE_ML, Keys.SCORE_FINAL]
   };
   return plan;
   ```

3. **Run with verbose output:**
   ```bash
   ./build-rel/engine/rankdsl_engine plans/my_feature.plan.json \
     --dump-top 20 \
     > debug_output.log 2>&1
   ```

4. **Analyze trace spans:**
   ```bash
   # Look for node execution times and trace_key identifiers
   grep "core:model(main_model)" debug_output.log
   ```

5. **Check key values:**
   ```bash
   # Dump shows values for keys specified in logging.dump_keys
   grep "score.ml" debug_output.log
   ```

### A/B Testing Plan Variants

**Scenario:** You want to compare two different ranking strategies.

**Steps:**

1. **Create variant plans:**
   ```bash
   # Copy baseline plan
   cp plans/baseline.plan.js plans/variant_a.plan.js
   cp plans/baseline.plan.js plans/variant_b.plan.js
   ```

2. **Modify variants:**
   ```javascript
   // plans/variant_a.plan.js - higher ML weight
   const config = { blendingAlpha: 0.3 };  // More ML

   // plans/variant_b.plan.js - add penalty
   const F = dsl.F;
   const expr = F.add(
     baseBlendExpr,
     F.penalty("diversity")
   );
   ```

3. **Compile all variants:**
   ```bash
   for plan in baseline variant_a variant_b; do
     node tools/cli/dist/index.js plan \
       --in plans/${plan}.plan.js \
       --out plans/${plan}.plan.json
   done
   ```

4. **Run experiments:**
   ```bash
   # Run each variant on the same input data
   for plan in baseline variant_a variant_b; do
     ./build-rel/engine/rankdsl_engine plans/${plan}.plan.json \
       --dump-top 100 > results/${plan}_output.txt
   done
   ```

5. **Commit experiment plans:**
   ```bash
   git checkout -b experiment/ranking-strategies
   git add plans/{variant_a,variant_b}.plan.js
   git commit -m "experiment: add ranking strategy variants"
   git push -u origin experiment/ranking-strategies
   gh pr create --fill
   ```

---

## Infra Engineer Workflows

Infra engineers maintain the Ranking DSL infrastructure, including the key registry, core nodes, engine, and tooling. They ensure the platform is robust, performant, and easy to use.

### Adding a New Key to the Registry

**Scenario:** A new signal (feature or score) needs to be made available to ranking engineers.

**Steps:**

1. **Create a feature branch:**
   ```bash
   git checkout main && git pull
   git checkout -b feature/add-relevance-score-key
   ```

2. **Add the key to registry.yaml:**
   ```yaml
   # keys/registry.yaml
   keys:
     # ... existing keys ...

     - id: 3004
       name: score.relevance
       type: f32
       scope: score
       owner: relevance-team
       doc: "Semantic relevance score from relevance model"
   ```

   **Important:**
   - Choose a unique ID that will NEVER be reused
   - Follow naming convention: `{scope}.{descriptor}` (lowercase with dots)
   - Document the key's purpose and owner

3. **Regenerate key constants:**
   ```bash
   npm run codegen
   ```

   This generates:
   - `keys/generated/keys.ts` - TypeScript constants
   - `keys/generated/keys.h` - C++ constants
   - `keys/generated/keys.json` - Runtime registry for engine
   - `keys/generated/keys.njs` - CommonJS for njs modules

4. **Verify generated files:**
   ```bash
   # Check that keys.ts has the new constant
   grep "SCORE_RELEVANCE" keys/generated/keys.ts

   # Check that codegen is fresh (for CI)
   node tools/cli/dist/index.js codegen --check
   ```

5. **Update documentation if needed:**
   ```bash
   # If this is a new scope or pattern, update docs
   vim docs/spec.md
   ```

6. **Commit and create PR:**
   ```bash
   git add keys/registry.yaml keys/generated/
   git commit -m "feat: add score.relevance key to registry"
   git push -u origin feature/add-relevance-score-key
   gh pr create --fill
   ```

7. **Review checklist:**
   - [ ] ID is globally unique
   - [ ] Name follows convention
   - [ ] Type is correct (f32, i64, f32vec, etc.)
   - [ ] Owner is documented
   - [ ] Generated files are committed
   - [ ] CI passes (codegen freshness check)

### Adding a New Core Node

**Scenario:** You need to add a new core node type to the engine (e.g., a deduplication node, a filter node).

**Steps:**

1. **Create a feature branch:**
   ```bash
   git checkout main && git pull
   git checkout -b feature/add-filter-node
   ```

2. **Create the node implementation:**
   ```cpp
   // engine/src/nodes/core/filter.cpp
   #include "nodes/node_runner.h"
   #include "nodes/registry.h"
   #include "expr/expr.h"

   namespace ranking_dsl {

   class FilterNode : public NodeRunner {
   public:
     bool Init(const nlohmann::json& params, std::string* error) override {
       // Parse filter expression from params
       if (!params.contains("condition")) {
         *error = "filter node requires 'condition' param";
         return false;
       }
       condition_ = params["condition"];
       return true;
     }

     ColumnBatch Run(const std::vector<ColumnBatch>& inputs,
                     const KeyRegistry& registry,
                     TraceContext trace_ctx) override {
       if (inputs.size() != 1) {
         throw std::runtime_error("filter expects exactly 1 input");
       }

       const auto& input = inputs[0];
       // Evaluate condition expression, filter rows
       // ... implementation ...

       return filtered_batch;
     }

   private:
     nlohmann::json condition_;
   };

   // Define NodeSpec metadata
   static NodeSpec CreateFilterNodeSpec() {
     NodeSpec spec;
     spec.op = "core:filter";
     spec.namespace_path = "core.filter";
     spec.stability = Stability::kStable;
     spec.doc = "Filters candidates based on a boolean expression";
     spec.params_schema_json = R"({
       "type": "object",
       "properties": {
         "condition": {
           "type": "object",
           "description": "Boolean expression for filtering"
         }
       },
       "required": ["condition"]
     })";
     spec.reads = {};  // Derived from expression
     spec.writes.kind = WritesDescriptor::Kind::kStatic;
     spec.writes.static_keys = {};  // No new keys, just filters
     return spec;
   }

   // Register the node
   REGISTER_NODE_RUNNER("core:filter",
                        []() { return std::make_unique<FilterNode>(); },
                        CreateFilterNodeSpec());

   }  // namespace ranking_dsl
   ```

3. **Add to CMakeLists.txt:**
   ```cmake
   # engine/src/nodes/core/CMakeLists.txt (or main CMakeLists.txt)
   target_sources(rankdsl_engine PRIVATE
     # ... existing sources ...
     nodes/core/filter.cpp
   )
   ```

4. **Write tests:**
   ```cpp
   // engine/tests/filter_node_test.cpp
   #include <catch2/catch_test_macros.hpp>
   #include "nodes/registry.h"

   TEST_CASE("Filter node", "[nodes][filter]") {
     // Test filter logic
     // ...
   }
   ```

5. **Build and test:**
   ```bash
   cmake --build build-rel -j
   ctest --test-dir build-rel -R filter
   ```

6. **Export node catalog:**
   ```bash
   npm run build
   node tools/cli/dist/index.js nodes export
   ```

   This updates `nodes/generated/nodes.yaml` with the new core node metadata.

7. **Generate TypeScript bindings:**
   ```bash
   node tools/cli/dist/index.js nodes codegen
   ```

   This generates namespaced TypeScript APIs in `nodes/generated/nodes.ts`.

8. **Update documentation:**
   ```markdown
   # In README.md or docs/spec.md

   ## Core Nodes

   | Node | Description |
   |------|-------------|
   | `core:filter` | Filters candidates based on expression |
   ```

9. **Commit and create PR:**
   ```bash
   git add engine/src/nodes/core/filter.cpp \
           engine/tests/filter_node_test.cpp \
           CMakeLists.txt \
           nodes/generated/ \
           README.md
   git commit -m "feat: add core:filter node"
   git push -u origin feature/add-filter-node
   gh pr create --fill
   ```

10. **Review checklist:**
    - [ ] Node implements NodeRunner interface
    - [ ] NodeSpec metadata is complete (op, namespace_path, stability, params_schema, reads, writes)
    - [ ] Node is registered with REGISTER_NODE_RUNNER macro
    - [ ] Unit tests cover happy path and error cases
    - [ ] Catalog export includes new node
    - [ ] TypeScript bindings are generated
    - [ ] Documentation is updated
    - [ ] CI passes (C++ tests + TypeScript type checks)

### Graduating an Experimental Node to Stable

**Scenario:** An experimental njs node has been battle-tested and is ready for production use.

**Steps:**

1. **Verify the node is ready:**
   - [ ] Used in production plans for >30 days
   - [ ] No critical bugs reported
   - [ ] Performance is acceptable
   - [ ] API is stable (no breaking changes needed)

2. **Create a feature branch:**
   ```bash
   git checkout main && git pull
   git checkout -b feature/graduate-custom-boost
   ```

3. **Update the node metadata:**
   ```javascript
   // njs/my_feature/custom_boost.njs

   exports.meta = {
     name: "custom_boost",
     version: "1.0.0",
     namespace_path: "ranking.boost",  // Changed from experimental.my_feature.customBoost
     stability: "stable",               // Changed from experimental
     // ... rest unchanged ...
   };
   ```

4. **Re-export catalog and codegen:**
   ```bash
   npm run build
   node tools/cli/dist/index.js nodes export
   node tools/cli/dist/index.js nodes codegen
   ```

5. **Update existing plan.js files:**
   ```bash
   # Find all uses of the old namespace
   grep -r "experimental.my_feature.customBoost" plans/

   # Update to new namespace
   # p.experimental.my_feature.customBoost(...)
   #   →
   # p.ranking.boost(...)
   ```

6. **Commit and create PR:**
   ```bash
   git add njs/my_feature/custom_boost.njs \
           nodes/generated/ \
           plans/
   git commit -m "feat: graduate custom_boost node to stable (ranking.boost)"
   git push -u origin feature/graduate-custom-boost
   gh pr create --fill
   ```

7. **Announce the graduation:**
   - Update internal documentation
   - Send announcement to ranking-eng mailing list
   - Add entry to CHANGELOG.md

### Updating Complexity Budgets

**Scenario:** Plans are hitting complexity limits, or you want to tighten budgets to keep plans maintainable.

**Steps:**

1. **Analyze current plan complexity:**
   ```bash
   # Validate all plans and collect metrics
   for plan in plans/*.plan.json; do
     echo "=== $plan ==="
     node tools/cli/dist/index.js validate "$plan" --json \
       | jq '.metrics'
   done > complexity_report.json
   ```

2. **Determine new budgets:**
   ```bash
   # Find current maximums
   jq -s '[.[].metrics] | {
     max_node_count: map(.node_count) | max,
     max_depth: map(.max_depth) | max,
     max_complexity_score: map(.complexity_score) | max
   }' complexity_report.json
   ```

3. **Create a feature branch:**
   ```bash
   git checkout main && git pull
   git checkout -b feature/update-complexity-budgets
   ```

4. **Update budgets:**
   ```json
   // configs/complexity_budgets.json
   {
     "hard": {
       "node_count": 2500,      // Increased from 2000
       "max_depth": 150,        // Increased from 120
       "fanout_peak": 16,
       "fanin_peak": 16
     },
     "soft": {
       "edge_count": 12000,     // Increased from 10000
       "complexity_score": 10000  // Increased from 8000
     }
   }
   ```

5. **Test against existing plans:**
   ```bash
   # Ensure existing plans still pass
   for plan in plans/*.plan.json; do
     node tools/cli/dist/index.js validate "$plan" \
       --budget configs/complexity_budgets.json
   done
   ```

6. **Update documentation:**
   ```markdown
   # docs/complexity-governance.md

   ## Current Budgets

   As of 2025-12-30:
   - Hard limit: node_count=2500, max_depth=150
   - Soft limit: complexity_score=10000

   Rationale: Increased to support multi-source ranking pipelines...
   ```

7. **Commit and create PR:**
   ```bash
   git add configs/complexity_budgets.json docs/complexity-governance.md
   git commit -m "feat: increase complexity budgets for multi-source pipelines"
   git push -u origin feature/update-complexity-budgets
   gh pr create --fill
   ```

8. **Review checklist:**
   - [ ] All existing plans pass validation with new budgets
   - [ ] Rationale for change is documented
   - [ ] Team is notified (for limit decreases, give migration period)

### Release Process

**Scenario:** You're ready to cut a new release of the Ranking DSL.

**Steps:**

1. **Verify all changes are merged:**
   ```bash
   git checkout main && git pull
   git log --oneline origin/main ^$(git describe --tags --abbrev=0)
   ```

2. **Run full test suite:**
   ```bash
   # TypeScript tests
   npm ci
   npm run build
   npm test

   # C++ tests
   cmake --build build-rel -j
   ctest --test-dir build-rel --output-on-failure

   # Codegen freshness
   node tools/cli/dist/index.js codegen --check
   ```

3. **Update version numbers:**
   ```json
   // package.json
   {
     "version": "0.3.0"  // Increment based on semver
   }
   ```

4. **Update CHANGELOG.md:**
   ```markdown
   # Changelog

   ## [0.3.0] - 2025-12-30

   ### Added
   - New `core:filter` node for candidate filtering
   - `score.relevance` key for semantic relevance scores

   ### Changed
   - Increased complexity budgets for multi-source pipelines

   ### Deprecated
   - `experimental.old_node` will be removed in v0.4.0

   ### Fixed
   - Fixed njs column write budget enforcement edge case
   ```

5. **Create release branch:**
   ```bash
   git checkout -b release/v0.3.0
   git add package.json CHANGELOG.md
   git commit -m "chore: bump version to 0.3.0"
   git push -u origin release/v0.3.0
   ```

6. **Create release PR:**
   ```bash
   gh pr create \
     --title "Release v0.3.0" \
     --body "$(cat CHANGELOG.md | sed -n '/## \[0.3.0\]/,/## \[/p' | head -n -1)"
   ```

7. **After PR approval and merge, tag the release:**
   ```bash
   git checkout main && git pull
   git tag -a v0.3.0 -m "Release v0.3.0"
   git push origin v0.3.0
   ```

8. **Build release artifacts:**
   ```bash
   # Build optimized engine
   cmake -S . -B build-release -DCMAKE_BUILD_TYPE=Release
   cmake --build build-release -j

   # Package for distribution
   tar czf rankdsl-v0.3.0-linux-x64.tar.gz \
     -C build-release/engine rankdsl_engine rankdsl_export_nodes
   ```

9. **Create GitHub release:**
   ```bash
   gh release create v0.3.0 \
     --title "Ranking DSL v0.3.0" \
     --notes "$(cat CHANGELOG.md | sed -n '/## \[0.3.0\]/,/## \[/p' | head -n -1)" \
     rankdsl-v0.3.0-linux-x64.tar.gz
   ```

10. **Announce the release:**
    - Send email to ranking-eng and infra mailing lists
    - Update internal wiki with migration notes
    - Post in team Slack channels

---

## Best Practices

### For Ranking Engineers

1. **Use trace_key for all production nodes:**
   ```javascript
   // Good: enables debugging
   p = p.core.model("v2", {}, { trace_key: "main_ranker" })

   // Avoid: hard to identify in logs
   p = p.core.model("v2", {})
   ```

2. **Keep plans simple and readable:**
   - Prefer smaller, composed plans over monolithic pipelines
   - Use meaningful variable names
   - Add comments for non-obvious logic

3. **Test locally before submitting:**
   - Always run validation and engine locally
   - Check complexity metrics
   - Verify trace output

4. **Version your njs modules:**
   - Increment version on breaking changes
   - Use semantic versioning

### For Infra Engineers

1. **Never reuse key IDs:**
   - Deleted keys stay tombstoned forever
   - Use sequential ID ranges by scope

2. **Design for stability:**
   - Core nodes should have minimal required params
   - Use sensible defaults
   - Avoid breaking API changes

3. **Document everything:**
   - NodeSpec metadata should be comprehensive
   - Key registry entries need clear ownership
   - Complexity budget changes need rationale

4. **Test thoroughly:**
   - Unit tests for every node
   - Integration tests for common pipelines
   - Performance benchmarks for critical paths

---

## Common Patterns

### Blending Multiple Scores

```javascript
const F = dsl.F;

// Linear blend
const blend = F.add(
  F.mul(F.const(0.7), F.signal(Keys.SCORE_BASE)),
  F.mul(F.const(0.3), F.signal(Keys.SCORE_ML))
);

// With penalty
const withPenalty = F.add(blend, F.penalty("diversity"));

// Clamped to valid range
const clamped = F.clamp(withPenalty, F.const(0), F.const(1));
```

### Feature-based Filtering (Future)

```javascript
// Once filter node is available
p = p.core.filter({
  condition: F.gt(F.signal(Keys.FEAT_FRESHNESS), F.const(0.5))
});
```

### Multi-stage Ranking

```javascript
// Stage 1: Fast retrieval
let p = dsl.core.sourcer("fast_index", { limit: 10000 })
         .core.features("lightweight_feats", { keys: [Keys.FEAT_FRESHNESS] })
         .core.model("fast_ranker", {})
         .core.topK({ k: 1000, score_key: Keys.SCORE_ML });

// Stage 2: Expensive re-ranking
p = p.core.features("full_feats", { keys: [Keys.FEAT_EMBEDDING] })
     .core.model("heavy_ranker", {})
     .core.score(blendExpr, { output_key_id: Keys.SCORE_FINAL })
     .core.topK({ k: 100, score_key: Keys.SCORE_FINAL });

return p.build();
```

---

## Troubleshooting

### Plan validation fails with complexity error

**Solution:** Review the plan structure. Consider:
- Breaking into multiple smaller plans
- Reducing fanout (avoid Cartesian products)
- Simplifying conditional branches

### njs node exceeds budget

**Solution:**
- Profile the node to find hotspots
- Use column-level API for batch operations
- Increase budget if justified (requires approval)

### Key not found errors

**Solution:**
- Ensure key is defined in `keys/registry.yaml`
- Run `npm run codegen` to regenerate constants
- Verify node writes the key before reading it

### Compilation fails with "Unknown op"

**Solution:**
- Run `node tools/cli/dist/index.js nodes export` to update catalog
- Verify node is registered (C++) or exports.meta exists (njs)
- Check namespace_path is correct

---

## Additional Resources

- **Specification:** `docs/spec.md` - Full DSL specification
- **Complexity Governance:** `docs/complexity-governance.md` - Complexity budget details
- **Key Registry:** `keys/registry.yaml` - All available keys
- **Node Catalog:** `nodes/generated/nodes.yaml` - All available nodes
- **Example Plans:** `plans/` - Reference implementations
