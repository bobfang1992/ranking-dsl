# Embedded Ranking DSL + Engine Spec (v0.2.8+) — Generated Namespaces from C++/njs SSOT + Experimental Graduation

## 0. RFC Keywords
The key words **MUST**, **MUST NOT**, **REQUIRED**, **SHOULD**, **SHOULD NOT**, **MAY**, and **OPTIONAL** are to be interpreted as described in RFC 2119.

---

## 1. What changes vs v0.2.7 (summary)
This spec strengthens governance and developer experience at large team scale:

1) **No `.node(op: string, ...)`** in plan authoring.
   All plan nodes MUST be invoked via **generated, namespaced methods** like `p.core.merge.weightedUnion(...)`.

2) **C++ and .njs are the source-of-truth (SSOT)** for node API metadata.
   We export a machine-readable node catalog (YAML/JSON) from implementations, then generate TS bindings with static type check.

3) **njs is similar**: authors write only implementation + meta; TS typing/bindings are auto-generated.

4) **Namespaces** (core / io / math / experimental / …) prevent API explosion.
   Each generated API has docstrings describing behavior + params + reads/writes + types.

5) **Experimental → Stable graduation** workflow via CLI.
   Production plans MUST NOT use experimental nodes. Plans must declare env (prod/dev/test).

---

## 2. Terms
- **Plan**: JSON-serializable DAG + options; pure data.
- **Engine compiler**: C++ compile stage that validates/lowers/instruments; authoritative fail-closed.
- **Node**: operator in DAG; implemented by C++ (core) or JavaScript (njs).
- **Op**: internal node identifier used by engine (e.g. `core:features` or pinned `js:path@ver#digest`).
- **Namespace path**: hierarchical API path used by plan authors (e.g. `core.merge.weightedUnion`).
- **NodeSpec**: machine-readable node metadata: namespace_path, stability, doc, params schema, reads/writes, budgets/capabilities.
- **Stability**: `experimental | stable`.
- **Plan env**: `prod | dev | test`.
- **Key Registry**: same as v0.2.7: keys declared in `keys/registry.yaml`, stable `key_id`, codegen keys.ts/keys.h/keys.json.

---

## 3. Two-phase architecture (unchanged)
### 3.1 Plan builder (TS/JS)
- Plan files (`*.plan.ts` / `*.plan.js`) evaluate to a **Plan JSON** object only.
- Plan files MUST NOT perform ranking execution.
- Limited `if/switch` for config routing is allowed; plan remains orchestration-level.

### 3.2 Engine (C++)
Compile MUST:
- validate DAG (acyclic), params, keys, types, budgets, and policy gates (env, stability, IO).
- resolve keys from `keys.json` (runtime mapping), not from JS objects.
- translate Expr IR and enforce key types.
- attach tracing envelope.

Execute MUST:
- run nodes in topo order;
- enforce budgets, writes allowlists;
- emit tracing/logging.

---

## 4. Key governance (unchanged)
- `keys/registry.yaml` SSOT for keys.
- Generated outputs:
  - `keys/generated/keys.ts`
  - `keys/generated/keys.h`
  - `keys/generated/keys.json`
- Runtime uses KeyId; no string keys.

---

## 5. Node SSOT: C++ and .njs define NodeSpec (REQUIRED)

### 5.1 Core C++ nodes MUST declare NodeSpec
A core node intended to be callable from plan MUST provide NodeSpec alongside registration.

NodeSpec MUST include:
- `op`: e.g. `core:features`
- `namespace_path`: e.g. `core.features` or `experimental.core.myNewNode`
- `stability`: `experimental|stable`
- `doc`: human description
- `params_schema`: JSON Schema (or equivalent)
- `reads`: KeyId[] (or KeyHandle list)
- `writes`: KeyId[] OR a param-derived rule (writes resolver)
- optional: budgets/capabilities

**Param-derived writes (REQUIRED for correctness):**
If writes depend on params (common), NodeSpec MUST define a writes resolver, e.g.:
- `writes = params["keys"]` for `features({keys:[...]})`.

The engine compiler MUST use this to enforce writes.

### 5.2 njs nodes MUST export NodeSpec-like meta
Each `.njs` module MUST export:

- `exports.meta = {`
  - `name`, `version`
  - `namespace_path` (e.g. `njs.normalize.score` or `experimental.njs.foo.bar`)
  - `stability` (`experimental|stable`)
  - `doc`
  - `reads`, `writes`
  - `params_schema` (JSON Schema)
  - optional `budget`, `capabilities`
- `}`
- `exports.runBatch(objs, ctx, params)`

njs identity MUST be pinned for execution:
- recommended internal op: `js:<path>@<version>#<digest>`

---

## 6. Generated plan authoring DSL (REQUIRED)

### 6.1 Top-level
- `dsl.planEnv(env: "prod"|"dev"|"test")` sets `plan.meta.env`
- `dsl.sourcer(name: string, params?: object) -> SourceRef`
- `dsl.candidates(sources: SourceRef[]) -> Pipeline`

### 6.2 Pipeline methods: NO `.node`
Plan authors MUST call nodes via generated namespaces based on NodeSpec `namespace_path`.

Examples:
- `p.core.merge.weightedUnion({ weights:[0.7,0.3], ... }, { trace_key:"merge" })`
- `p.core.features({ keys:[Keys.feat_freshness, ...] }, { trace_key:"feat" })`
- `p.io.csv.read({ resource:"sample.csv", ... })` (policy-gated IO example)
- `p.experimental.core.myNewNode({ ... })` (non-prod only)

**Hard requirement:**
- The plan authoring API MUST NOT expose `.node(op: string, ...)`.

### 6.3 Static typing + runtime validation (REQUIRED)
- Generated TS bindings MUST provide static typing for params derived from `params_schema`.
- Plan builder MUST also validate params at runtime (fail fast).

### 6.4 Docstrings / metadata surfacing (REQUIRED)
Every generated API method MUST include docstrings that surface:
- purpose / behavior
- params (types + meaning)
- reads/writes keys (Key names + ids)
- stability (experimental/stable)
- budgets/capabilities (if any)

---

## 7. Namespaces (REQUIRED)
Namespaces are hierarchical to prevent API explosion. NodeSpec provides `namespace_path` which drives generation.

Recommended top-level namespaces:
- `core.*`: stable C++ nodes
- `njs.*`: stable njs nodes
- `io.*`: IO-related nodes/capabilities (policy gated)
- `math.*`: expression helpers (for Expr sugar/builder)
- `experimental.*`: experimental nodes (both C++ and njs)
  - `experimental.core.*`
  - `experimental.njs.*`
  - `experimental.io.*` (optional)

Rule:
- `stability=experimental` MUST appear only under `experimental.*` namespace paths.
- `stability=stable` MUST NOT appear under `experimental.*`.

---

## 8. Node catalog export + TS binding generation (REQUIRED)

### 8.1 Export node catalog from SSOT
Provide a command:

- `rankdsl nodes export`

It MUST:
- extract NodeSpecs from C++ registry (or a compiled dump),
- scan `.njs` modules for `exports.meta`,
- compute pinned njs identity: `path@version#digest`,
- output machine-readable catalog:
  - `nodes/generated/nodes.yaml` (or `.json`)
  - each entry contains: op, namespace_path, stability, doc, params_schema, reads/writes, budgets/capabilities.

### 8.2 Generate TS bindings
Provide a command:

- `rankdsl nodes codegen`

It MUST:
- read `nodes/generated/nodes.yaml`,
- generate TS bindings that attach namespaces and typed methods onto Pipeline:
  - e.g. `nodes/generated/nodes.ts`
- embed docstrings and read/write metadata in TS.
- be deterministic.

### 8.3 CI requirements
CI MUST:
- run `rankdsl nodes export` and `rankdsl nodes codegen`,
- fail if generated files are stale,
- fail if a node is callable but missing required NodeSpec fields.

---

## 9. Plan env (prod/dev/test) and enforcement (REQUIRED)

### 9.1 How to mark a plan as prod/dev/test
Plans MUST declare environment either:

**Option A (recommended): in plan file**
- `dsl.planEnv("prod"|"dev"|"test")` → `plan.meta.env`

**Option B: file suffix**
- `*.prod.plan.js`, `*.dev.plan.js`, `*.test.plan.js`

If both exist, explicit declaration wins.

### 9.2 Enforcement rules
Engine compiler MUST enforce:
- if `env == "prod"`:
  - reject any node with `stability=experimental`
  - reject any IO capability not explicitly allowed by prod policy
- if `env in {"dev","test"}`:
  - experimental nodes MAY be allowed (policy-controlled)

---

## 10. Experimental workflow + graduation (REQUIRED)

### 10.1 Creating a new C++ node (infra)
Infra engineers can introduce new nodes as:
- `namespace_path = experimental.core.<name>`
- `stability = experimental`

They can write plans/tests using:
- `p.experimental.core.<name>(...)`

### 10.2 Graduation to stable
Provide a command:

- `rankdsl nodes graduate <node_id|namespace_path> --to <new_namespace_path>`

Graduation MUST:
- update source-of-truth metadata (C++ NodeSpec or `.njs` meta):
  - `stability: experimental → stable`
  - move `namespace_path` from `experimental.*` → stable namespace (e.g. `core.*`)
- regenerate node catalog + TS bindings.

---

## 11. Tracing: trace_key (REQUIRED)
Plan nodes MAY attach `trace_key` (NodeOpts).
Engine MUST:
- include `trace_key` in node_start/node_end logs
- attach as span attribute
- include in span name: `op(trace_key)`.

For njs nested native calls (if host supports calling native ops from njs):
- prefix nested trace_key with `.njs` filename stem or namespace_path prefix.

---

## 12. Complexity budgets (REQUIRED)
To avoid "10k-node graphs" that are impossible to debug:
- plan-side tooling SHOULD warn early (DX).
- engine compiler MUST enforce fail-closed budgets.

Minimum metrics:
- node_count, edge_count, max_depth, fanout_peak, fanin_peak

If over budget:
- error MUST include actionable diagnostics (top offenders + longest path summary)
- remediation hint: collapse logic into fewer nodes (njs modules) or graduate/promote into stable core nodes.

---

## 13. njs sandbox + policy-gated IO (REQUIRED)
Default: no filesystem/network/process/shell and no QuickJS std/os modules exposed.

Optional: host-provided IO only when:
- capability declared in meta (e.g. `capabilities.io.csv_read=true`)
- allowlisted by policy
- budgets enforced
- path traversal forbidden

IO can be surfaced either as:
- `ctx.io.*` (capability object), and/or
- explicit `io.*` nodes (namespaced API), policy gated.

---

## 14. Testing requirements (REQUIRED)
Repo MUST include:
1) Deterministic generation tests (`rankdsl nodes export/codegen` stable output).
2) TS typecheck tests (plan fixtures compile with `tsc --noEmit`).
3) Engine policy tests:
   - prod plan rejects experimental nodes
   - dev/test plan can allow experimental nodes (policy)
4) Docstring presence tests (or snapshots) to ensure reads/writes appear in generated bindings.

---

## Appendix: Example plan (illustrative)
```ts
dsl.planEnv("prod");

const plan = dsl.candidates([dsl.sourcer("following", { max: 2000 })])
  .core.merge.weightedUnion({ weights: [0.7, 0.3] }, { trace_key: "merge" })
  .core.features({ keys: [Keys.feat_freshness] }, { trace_key: "feat" })
  .core.model.vm({ name: "vm_v1" }, { trace_key: "vm" })
  .build();
```
