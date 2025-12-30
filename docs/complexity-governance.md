# Complexity Governance for Plan.js / njs (v0.1)

This document defines **how we measure and enforce plan complexity** to keep ranking pipelines **human-auditable, debuggable, and safe** at large team scale.

The intent is *not* to ban sophisticated logic. The intent is to keep the **top-level plan** as an orchestration layer and push complexity behind **explicit module boundaries** (njs modules or core C++ nodes) where reads/writes/budgets/ownership are clear.

---

## 1. Why this exists

In large organizations, it is common to see:

- top-level graphs with **thousands to tens of thousands of nodes**
- difficult-to-debug “spider web” dependencies
- repeated micro-logic copy/pasted across pipelines
- silent correctness regressions caused by huge dependency surfaces

We therefore enforce **complexity budgets** at compile time.

When a plan is over budget, the correct response is usually:

1) **collapse** a subgraph into one or a few **njs module nodes**, and/or  
2) **promote** stable/commonly-used logic into a **core C++ node** (ROI gate)

---

## 2. Scope and enforcement point

Complexity budgets are enforced by the **engine compiler** during compile, before execution.

- Metrics MUST be computed on the **logical plan DAG** (user-visible graph), *before* adding tracing/instrumentation nodes.
- Enforcement MUST be **fail-closed** for hard limits (compile error).
- Soft limits MAY emit warnings.

Budgets are configured by engine policy (e.g. `configs/complexity_budgets.json`) and can vary across environments (dev vs prod).

---

## 3. Complexity metrics (definitions)

Given a plan DAG `G = (V, E)`:

### 3.1 Node count (N)
`N = |V|` where `V` is the set of plan nodes.

This is the primary “debug surface area” metric.

### 3.2 Edge count (E)
`E = |E|` edges in the DAG.

Useful secondary metric (dense graphs are harder to reason about).

### 3.3 Maximum depth (D)
`D = max_{path} length(path)` over all directed paths, measured in number of nodes.

Deep pipelines are harder to debug because errors propagate far downstream.

### 3.4 Fan-out peak (F_out)
`F_out = max_{v in V} out_degree(v)`.

High fan-out nodes create “blast radius” and make root-cause analysis harder.

### 3.5 Fan-in peak (F_in)
`F_in = max_{v in V} in_degree(v)`.

High fan-in nodes create “dependency spaghetti” and make reasoning about prerequisites hard.

### 3.6 Optional: Complexity score (S)
For reporting and warning thresholds, you MAY compute:

`S = a*N + b*D + c*F_out + d*F_in + e*E`

Recommended default weights (tune per org):  
- `a=1`, `b=5`, `c=2`, `d=2`, `e=0.5`

Hard enforcement SHOULD be driven by individual caps (N/D/F), not only by S.

---

## 4. Budgets (policy)

Budgets are split into:

- **hard caps**: compile MUST fail if exceeded
- **soft caps**: compile MAY warn if exceeded

### 4.1 Recommended starter budgets (example)

These are *example defaults*; tune for your org:

- `N_max_hard = 2000`
- `D_max_hard = 120`
- `F_out_max_hard = 16`
- `F_in_max_hard = 16`
- `E_max_soft = 10000`
- `S_max_soft = 8000`

### 4.2 Configuration format (example)

```json
{
  "hard": { "node_count": 2000, "max_depth": 120, "fanout_peak": 16, "fanin_peak": 16 },
  "soft": { "edge_count": 10000, "complexity_score": 8000 }
}
```

---

## 5. Diagnostics requirements (what the compiler must print)

If a plan violates a **hard cap**, the compiler MUST error with a structured error code (e.g. `PLAN_TOO_COMPLEX`) and include:

1) The computed metrics: `node_count`, `edge_count`, `max_depth`, `fanout_peak`, `fanin_peak`, and optionally `score`.
2) The relevant limits and whether each is hard/soft.
3) **Top offenders**:
   - Top-K nodes by fan-out (node id + op name)
   - Top-K nodes by fan-in (node id + op name)
4) **A longest-path summary** (at least the node ids and op names along the path).
5) A remediation hint:
   - “Collapse a subgraph into njs module node(s), or promote to core C++ node.”

### 5.1 Example error output (illustrative)

```
PLAN_TOO_COMPLEX:
  node_count=12345 (hard_limit=2000)
  max_depth=210 (hard_limit=120)
  fanout_peak=64 (hard_limit=16)
  fanin_peak=42 (hard_limit=16)
Top fanout nodes:
  n_778 core:features fanout=64
  n_921 core:merge fanout=40
Longest path (len=210):
  n_1 core:sourcer -> ... -> n_998 core:score_formula
Hint:
  Collapse repeated logic into 1-3 njs module nodes, or request a core C++ node.
```

---

## 6. Refactoring playbook (how to get back under budget)

### 6.1 Collapse subgraphs into njs module nodes
Use njs nodes to encapsulate repeated or complex business logic while keeping plan orchestration simple.

Rules:
- keep `meta.reads / meta.writes / params / budget` explicit
- ensure tracing spans + logging for the njs node remain strong
- prefer a small number of module nodes (e.g. 1–10) over hundreds of micro-nodes

### 6.2 Promote to core C++ node (ROI gate)
When logic is stable, widely reused, performance-critical, or hard to debug, promote it into a core node.

**This is an intentional cost gate**: consuming infra time is a feature, not a bug. It forces ROI-aware abstraction.

Suggested triggers:
- used by many pipelines (high reuse)
- high latency/CPU in hot path
- frequent incidents or hard-to-debug regressions
- security/compliance requirements (strict sandboxing)
- substantial plan complexity reduction

---

## 7. njs complexity budgets (avoid “black-box monoliths”)

To prevent moving all complexity into a single giant njs file, the engine SHOULD enforce njs budgets, such as:

- `max_source_bytes`
- `max_steps` / `max_time_ms` (via QuickJS interrupt handler)
- `max_memory_bytes`
- existing write budgets: `max_write_bytes`, `max_write_cells`

These budgets are enforced in addition to plan complexity budgets.

---

## 8. “Request a new core node” template (one-page)

When requesting a new C++ node, provide:

- **Problem statement**: what logic is being abstracted?
- **Current pain**: which complexity caps are hit? paste compiler error.
- **Reads/Writes**: intended `meta.reads` / `meta.writes`.
- **Users/Reuse**: which pipelines will use it?
- **Performance goal**: expected latency/CPU/memory improvements.
- **Correctness / Debugging**: incidents or regressions this avoids.
- **Success criteria**: how we decide it was worth it.

---

## 9. Testing requirements

The repository MUST include tests covering:

- complexity metric computation (N/E/D/F)
- compile failure when exceeding hard caps
- diagnostics contain: metrics + limits + top offenders + remediation hint

