/**
 * Plan complexity metrics and budget enforcement.
 *
 * This module computes the same metrics as the C++ engine (engine/src/plan/complexity.cpp)
 * to provide early feedback during plan validation.
 */

import type { Plan, NodeSpec } from './types.js';

/**
 * Complexity metrics computed from a Plan DAG.
 */
export interface ComplexityMetrics {
  /** Number of nodes (N) */
  nodeCount: number;
  /** Number of edges (E) */
  edgeCount: number;
  /** Longest path length (D) */
  maxDepth: number;
  /** Maximum out-degree (fan-out peak) */
  fanoutPeak: number;
  /** Maximum in-degree (fan-in peak) */
  faninPeak: number;
  /** Weighted complexity score */
  complexityScore: number;
  /** Top nodes by fan-out */
  topFanout: NodeInfo[];
  /** Top nodes by fan-in */
  topFanin: NodeInfo[];
  /** Node IDs on longest path */
  longestPath: string[];
}

export interface NodeInfo {
  id: string;
  op: string;
  degree: number;
}

/**
 * Complexity budget limits.
 */
export interface ComplexityBudget {
  hard: {
    nodeCount?: number;
    maxDepth?: number;
    fanoutPeak?: number;
    faninPeak?: number;
  };
  soft: {
    edgeCount?: number;
    complexityScore?: number;
  };
  scoreWeights?: {
    nodeCount?: number;
    maxDepth?: number;
    fanoutPeak?: number;
    faninPeak?: number;
    edgeCount?: number;
  };
}

/**
 * Result of complexity check.
 */
export interface ComplexityCheckResult {
  passed: boolean;
  hasWarnings: boolean;
  errorCode?: string;
  diagnostics?: string;
  metrics: ComplexityMetrics;
  violations: string[];
  warnings: string[];
}

/**
 * Default score weights.
 */
const DEFAULT_WEIGHTS = {
  nodeCount: 1.0,
  maxDepth: 5.0,
  fanoutPeak: 2.0,
  faninPeak: 2.0,
  edgeCount: 0.5,
};

/**
 * Compute complexity metrics for a plan.
 * Algorithm matches engine/src/plan/complexity.cpp ComputeComplexityMetrics().
 */
export function computeComplexityMetrics(
  plan: Plan,
  topK: number = 5,
  weights: ComplexityBudget['scoreWeights'] = DEFAULT_WEIGHTS
): ComplexityMetrics {
  const metrics: ComplexityMetrics = {
    nodeCount: plan.nodes.length,
    edgeCount: 0,
    maxDepth: 0,
    fanoutPeak: 0,
    faninPeak: 0,
    complexityScore: 0,
    topFanout: [],
    topFanin: [],
    longestPath: [],
  };

  if (plan.nodes.length === 0) {
    return metrics;
  }

  // Build graph structures
  const nodeMap = new Map<string, NodeSpec>();
  const adj = new Map<string, string[]>(); // node -> dependents
  const outDegree = new Map<string, number>();
  const inDegree = new Map<string, number>();

  for (const node of plan.nodes) {
    nodeMap.set(node.id, node);
    adj.set(node.id, []);
    inDegree.set(node.id, node.inputs.length);
    outDegree.set(node.id, 0);

    for (const input of node.inputs) {
      metrics.edgeCount++;
      const deps = adj.get(input) ?? [];
      deps.push(node.id);
      adj.set(input, deps);
    }
  }

  // Compute out-degrees
  for (const [id, deps] of adj) {
    outDegree.set(id, deps.length);
  }

  // Find fanout and fanin peaks
  for (const node of plan.nodes) {
    metrics.fanoutPeak = Math.max(metrics.fanoutPeak, outDegree.get(node.id) ?? 0);
    metrics.faninPeak = Math.max(metrics.faninPeak, inDegree.get(node.id) ?? 0);
  }

  // Compute max depth using dynamic programming (Kahn's algorithm with depth tracking)
  const depth = new Map<string, number>();
  const predecessor = new Map<string, string>();
  const remainingIn = new Map<string, number>();
  const queue: string[] = [];

  for (const node of plan.nodes) {
    remainingIn.set(node.id, inDegree.get(node.id) ?? 0);
    depth.set(node.id, 1); // Base depth
    if ((inDegree.get(node.id) ?? 0) === 0) {
      queue.push(node.id);
    }
  }

  let deepestNode = '';
  let maxDepth = 0;

  while (queue.length > 0) {
    const current = queue.shift()!;
    const currentDepth = depth.get(current) ?? 1;

    if (currentDepth > maxDepth) {
      maxDepth = currentDepth;
      deepestNode = current;
    }

    for (const dep of adj.get(current) ?? []) {
      const newDepth = currentDepth + 1;
      if (newDepth > (depth.get(dep) ?? 0)) {
        depth.set(dep, newDepth);
        predecessor.set(dep, current);
      }
      remainingIn.set(dep, (remainingIn.get(dep) ?? 1) - 1);
      if ((remainingIn.get(dep) ?? 0) === 0) {
        queue.push(dep);
      }
    }
  }

  metrics.maxDepth = maxDepth;

  // Reconstruct longest path
  if (deepestNode) {
    const path: string[] = [];
    let current: string | undefined = deepestNode;
    while (current) {
      path.push(current);
      current = predecessor.get(current);
    }
    path.reverse();
    metrics.longestPath = path;
  }

  // Collect top-K fanout nodes
  const fanoutNodes: NodeInfo[] = plan.nodes.map((node: NodeSpec) => ({
    id: node.id,
    op: node.op,
    degree: outDegree.get(node.id) ?? 0,
  }));
  fanoutNodes.sort((a, b) => b.degree - a.degree);
  metrics.topFanout = fanoutNodes.slice(0, topK);

  // Collect top-K fanin nodes
  const faninNodes: NodeInfo[] = plan.nodes.map((node: NodeSpec) => ({
    id: node.id,
    op: node.op,
    degree: inDegree.get(node.id) ?? 0,
  }));
  faninNodes.sort((a, b) => b.degree - a.degree);
  metrics.topFanin = faninNodes.slice(0, topK);

  // Compute complexity score
  const w = { ...DEFAULT_WEIGHTS, ...weights };
  metrics.complexityScore = Math.floor(
    w.nodeCount * metrics.nodeCount +
      w.maxDepth * metrics.maxDepth +
      w.fanoutPeak * metrics.fanoutPeak +
      w.faninPeak * metrics.faninPeak +
      w.edgeCount * metrics.edgeCount
  );

  return metrics;
}

/**
 * Check if metrics are within budget.
 */
export function checkComplexityBudget(
  metrics: ComplexityMetrics,
  budget: ComplexityBudget
): ComplexityCheckResult {
  const violations: string[] = [];
  const warnings: string[] = [];

  // Check hard limits
  if (budget.hard.nodeCount && metrics.nodeCount > budget.hard.nodeCount) {
    violations.push(`node_count=${metrics.nodeCount} (hard_limit=${budget.hard.nodeCount})`);
  }
  if (budget.hard.maxDepth && metrics.maxDepth > budget.hard.maxDepth) {
    violations.push(`max_depth=${metrics.maxDepth} (hard_limit=${budget.hard.maxDepth})`);
  }
  if (budget.hard.fanoutPeak && metrics.fanoutPeak > budget.hard.fanoutPeak) {
    violations.push(`fanout_peak=${metrics.fanoutPeak} (hard_limit=${budget.hard.fanoutPeak})`);
  }
  if (budget.hard.faninPeak && metrics.faninPeak > budget.hard.faninPeak) {
    violations.push(`fanin_peak=${metrics.faninPeak} (hard_limit=${budget.hard.faninPeak})`);
  }

  // Check soft limits
  if (budget.soft.edgeCount && metrics.edgeCount > budget.soft.edgeCount) {
    warnings.push(`edge_count=${metrics.edgeCount} (soft_limit=${budget.soft.edgeCount})`);
  }
  if (budget.soft.complexityScore && metrics.complexityScore > budget.soft.complexityScore) {
    warnings.push(
      `complexity_score=${metrics.complexityScore} (soft_limit=${budget.soft.complexityScore})`
    );
  }

  const passed = violations.length === 0;
  const hasWarnings = warnings.length > 0;

  let diagnostics: string | undefined;
  if (!passed) {
    diagnostics = formatDiagnostics(metrics, budget, violations);
  }

  return {
    passed,
    hasWarnings,
    errorCode: passed ? undefined : 'PLAN_TOO_COMPLEX',
    diagnostics,
    metrics,
    violations,
    warnings,
  };
}

/**
 * Format diagnostics message (matches C++ format).
 */
function formatDiagnostics(
  metrics: ComplexityMetrics,
  budget: ComplexityBudget,
  violations: string[]
): string {
  const lines: string[] = ['PLAN_TOO_COMPLEX:'];

  // All metrics
  lines.push(
    `  node_count=${metrics.nodeCount}` +
      (budget.hard.nodeCount ? ` (hard_limit=${budget.hard.nodeCount})` : '')
  );
  lines.push(
    `  edge_count=${metrics.edgeCount}` +
      (budget.soft.edgeCount ? ` (soft_limit=${budget.soft.edgeCount})` : '')
  );
  lines.push(
    `  max_depth=${metrics.maxDepth}` +
      (budget.hard.maxDepth ? ` (hard_limit=${budget.hard.maxDepth})` : '')
  );
  lines.push(
    `  fanout_peak=${metrics.fanoutPeak}` +
      (budget.hard.fanoutPeak ? ` (hard_limit=${budget.hard.fanoutPeak})` : '')
  );
  lines.push(
    `  fanin_peak=${metrics.faninPeak}` +
      (budget.hard.faninPeak ? ` (hard_limit=${budget.hard.faninPeak})` : '')
  );

  // Top fanout nodes
  if (metrics.topFanout.some((n) => n.degree > 0)) {
    lines.push('Top fanout nodes:');
    for (const node of metrics.topFanout) {
      if (node.degree > 0) {
        lines.push(`  ${node.id} ${node.op} fanout=${node.degree}`);
      }
    }
  }

  // Top fanin nodes
  if (metrics.topFanin.some((n) => n.degree > 0)) {
    lines.push('Top fanin nodes:');
    for (const node of metrics.topFanin) {
      if (node.degree > 0) {
        lines.push(`  ${node.id} ${node.op} fanin=${node.degree}`);
      }
    }
  }

  // Longest path
  if (metrics.longestPath.length > 0) {
    lines.push(`Longest path (len=${metrics.longestPath.length}):`);
    if (metrics.longestPath.length <= 8) {
      lines.push(`  ${metrics.longestPath.join(' -> ')}`);
    } else {
      const first = metrics.longestPath.slice(0, 3).join(' -> ');
      const last = metrics.longestPath.slice(-2).join(' -> ');
      lines.push(`  ${first} -> ... -> ${last}`);
    }
  }

  // Remediation hint
  lines.push('Hint:');
  lines.push('  Collapse repeated logic into 1-3 njs module nodes, or request a core C++ node.');
  lines.push('  See docs/complexity-governance.md for guidance.');

  return lines.join('\n');
}

/**
 * Load complexity budget from JSON file.
 */
export function parseBudgetFromJson(json: string): ComplexityBudget {
  const data = JSON.parse(json);
  return {
    hard: {
      nodeCount: data.hard?.node_count,
      maxDepth: data.hard?.max_depth,
      fanoutPeak: data.hard?.fanout_peak,
      faninPeak: data.hard?.fanin_peak,
    },
    soft: {
      edgeCount: data.soft?.edge_count,
      complexityScore: data.soft?.complexity_score,
    },
    scoreWeights: data.score_weights
      ? {
          nodeCount: data.score_weights.node_count,
          maxDepth: data.score_weights.max_depth,
          fanoutPeak: data.score_weights.fanout_peak,
          faninPeak: data.score_weights.fanin_peak,
          edgeCount: data.score_weights.edge_count,
        }
      : undefined,
  };
}

/**
 * Default budget values (matches C++ ComplexityBudget::Default()).
 */
export function defaultBudget(): ComplexityBudget {
  return {
    hard: {
      nodeCount: 2000,
      maxDepth: 120,
      fanoutPeak: 16,
      faninPeak: 16,
    },
    soft: {
      edgeCount: 10000,
      complexityScore: 8000,
    },
    scoreWeights: DEFAULT_WEIGHTS,
  };
}
