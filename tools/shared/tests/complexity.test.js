import { describe, it, expect } from 'vitest';
import * as fs from 'node:fs';
import * as path from 'node:path';
import { computeComplexityMetrics, checkComplexityBudget, parseBudgetFromJson, defaultBudget, } from '../src/index.js';
describe('complexity metrics computation', () => {
    it('computes metrics for empty plan', () => {
        const plan = { name: 'empty', version: 1, nodes: [] };
        const metrics = computeComplexityMetrics(plan);
        expect(metrics.nodeCount).toBe(0);
        expect(metrics.edgeCount).toBe(0);
        expect(metrics.maxDepth).toBe(0);
        expect(metrics.fanoutPeak).toBe(0);
        expect(metrics.faninPeak).toBe(0);
    });
    it('computes metrics for single node', () => {
        const plan = {
            name: 'single',
            version: 1,
            nodes: [{ id: 'n1', op: 'core:sourcer', inputs: [], params: {} }],
        };
        const metrics = computeComplexityMetrics(plan);
        expect(metrics.nodeCount).toBe(1);
        expect(metrics.edgeCount).toBe(0);
        expect(metrics.maxDepth).toBe(1);
        expect(metrics.fanoutPeak).toBe(0);
        expect(metrics.faninPeak).toBe(0);
    });
    it('computes metrics for linear chain', () => {
        const plan = {
            name: 'linear',
            version: 1,
            nodes: [
                { id: 'n0', op: 'core:sourcer', inputs: [], params: {} },
                { id: 'n1', op: 'core:features', inputs: ['n0'], params: {} },
                { id: 'n2', op: 'core:features', inputs: ['n1'], params: {} },
                { id: 'n3', op: 'core:features', inputs: ['n2'], params: {} },
                { id: 'n4', op: 'core:features', inputs: ['n3'], params: {} },
            ],
        };
        const metrics = computeComplexityMetrics(plan);
        expect(metrics.nodeCount).toBe(5);
        expect(metrics.edgeCount).toBe(4);
        expect(metrics.maxDepth).toBe(5);
        expect(metrics.fanoutPeak).toBe(1);
        expect(metrics.faninPeak).toBe(1);
        expect(metrics.longestPath).toEqual(['n0', 'n1', 'n2', 'n3', 'n4']);
    });
    it('computes metrics for fan-out pattern', () => {
        const plan = {
            name: 'fanout',
            version: 1,
            nodes: [
                { id: 'root', op: 'core:sourcer', inputs: [], params: {} },
                { id: 'c1', op: 'core:features', inputs: ['root'], params: {} },
                { id: 'c2', op: 'core:features', inputs: ['root'], params: {} },
                { id: 'c3', op: 'core:features', inputs: ['root'], params: {} },
            ],
        };
        const metrics = computeComplexityMetrics(plan);
        expect(metrics.nodeCount).toBe(4);
        expect(metrics.edgeCount).toBe(3);
        expect(metrics.maxDepth).toBe(2);
        expect(metrics.fanoutPeak).toBe(3);
        expect(metrics.faninPeak).toBe(1);
        expect(metrics.topFanout[0]?.id).toBe('root');
        expect(metrics.topFanout[0]?.degree).toBe(3);
    });
    it('computes metrics for fan-in pattern', () => {
        const plan = {
            name: 'fanin',
            version: 1,
            nodes: [
                { id: 's1', op: 'core:sourcer', inputs: [], params: {} },
                { id: 's2', op: 'core:sourcer', inputs: [], params: {} },
                { id: 's3', op: 'core:sourcer', inputs: [], params: {} },
                { id: 'merge', op: 'core:merge', inputs: ['s1', 's2', 's3'], params: {} },
            ],
        };
        const metrics = computeComplexityMetrics(plan);
        expect(metrics.nodeCount).toBe(4);
        expect(metrics.edgeCount).toBe(3);
        expect(metrics.maxDepth).toBe(2);
        expect(metrics.fanoutPeak).toBe(1);
        expect(metrics.faninPeak).toBe(3);
        expect(metrics.topFanin[0]?.id).toBe('merge');
        expect(metrics.topFanin[0]?.degree).toBe(3);
    });
    it('computes complexity score with default weights', () => {
        // Linear plan with 10 nodes: N=10, D=10, E=9, fanout=1, fanin=1
        // Score = 10*1 + 10*5 + 1*2 + 1*2 + 9*0.5 = 10 + 50 + 2 + 2 + 4.5 = 68 (floored)
        const plan = {
            name: 'linear10',
            version: 1,
            nodes: Array.from({ length: 10 }, (_, i) => ({
                id: `n${i}`,
                op: 'core:features',
                inputs: i > 0 ? [`n${i - 1}`] : [],
                params: {},
            })),
        };
        const metrics = computeComplexityMetrics(plan);
        expect(metrics.nodeCount).toBe(10);
        expect(metrics.edgeCount).toBe(9);
        expect(metrics.maxDepth).toBe(10);
        expect(metrics.complexityScore).toBe(68);
    });
});
describe('complexity budget checking', () => {
    it('passes when within budget', () => {
        const metrics = {
            nodeCount: 10,
            edgeCount: 20,
            maxDepth: 5,
            fanoutPeak: 3,
            faninPeak: 3,
            complexityScore: 100,
            topFanout: [],
            topFanin: [],
            longestPath: [],
        };
        const budget = defaultBudget();
        const result = checkComplexityBudget(metrics, budget);
        expect(result.passed).toBe(true);
        expect(result.violations).toHaveLength(0);
    });
    it('fails on node count violation', () => {
        const metrics = {
            nodeCount: 100,
            edgeCount: 20,
            maxDepth: 5,
            fanoutPeak: 3,
            faninPeak: 3,
            complexityScore: 100,
            topFanout: [],
            topFanin: [],
            longestPath: [],
        };
        const budget = {
            hard: { nodeCount: 50 },
            soft: {},
        };
        const result = checkComplexityBudget(metrics, budget);
        expect(result.passed).toBe(false);
        expect(result.violations).toContain('node_count=100 (hard_limit=50)');
    });
    it('fails on fanout violation', () => {
        const metrics = {
            nodeCount: 10,
            edgeCount: 20,
            maxDepth: 5,
            fanoutPeak: 20,
            faninPeak: 3,
            complexityScore: 100,
            topFanout: [],
            topFanin: [],
            longestPath: [],
        };
        const budget = {
            hard: { fanoutPeak: 10 },
            soft: {},
        };
        const result = checkComplexityBudget(metrics, budget);
        expect(result.passed).toBe(false);
        expect(result.violations).toContain('fanout_peak=20 (hard_limit=10)');
    });
    it('warns on soft limit violation', () => {
        const metrics = {
            nodeCount: 10,
            edgeCount: 200,
            maxDepth: 5,
            fanoutPeak: 3,
            faninPeak: 3,
            complexityScore: 100,
            topFanout: [],
            topFanin: [],
            longestPath: [],
        };
        const budget = {
            hard: {},
            soft: { edgeCount: 100 },
        };
        const result = checkComplexityBudget(metrics, budget);
        expect(result.passed).toBe(true);
        expect(result.hasWarnings).toBe(true);
        expect(result.warnings).toContain('edge_count=200 (soft_limit=100)');
    });
});
describe('budget parsing', () => {
    it('parses budget from JSON', () => {
        const json = `{
      "hard": { "node_count": 100, "max_depth": 50 },
      "soft": { "edge_count": 500 }
    }`;
        const budget = parseBudgetFromJson(json);
        expect(budget.hard.nodeCount).toBe(100);
        expect(budget.hard.maxDepth).toBe(50);
        expect(budget.soft.edgeCount).toBe(500);
    });
    it('returns default budget values', () => {
        const budget = defaultBudget();
        expect(budget.hard.nodeCount).toBe(2000);
        expect(budget.hard.maxDepth).toBe(120);
        expect(budget.hard.fanoutPeak).toBe(16);
        expect(budget.hard.faninPeak).toBe(16);
        expect(budget.soft.edgeCount).toBe(10000);
        expect(budget.soft.complexityScore).toBe(8000);
    });
});
describe('cross-check with C++ engine', () => {
    it('produces same metrics as C++ for fixture plan', () => {
        // Load the fixture plan
        const fixturePath = path.resolve(__dirname, '../../../test-fixtures/complexity-fixture.plan.json');
        const planJson = fs.readFileSync(fixturePath, 'utf-8');
        const plan = JSON.parse(planJson);
        const metrics = computeComplexityMetrics(plan);
        // Expected values (verified by C++ test below):
        // - node_count: 8 nodes total
        // - edge_count: 9 edges (sourcer->feat1/2/3 = 3, feat1->model1 = 1, feat2->model2 = 1,
        //                        model1/model2/feat3->merge = 3, merge->final = 1)
        // - max_depth: 5 (sourcer:1 -> feat1:2 -> model1:3 -> merge:4 -> final:5)
        // - fanout_peak: 3 (sourcer has 3 outgoing edges)
        // - fanin_peak: 3 (merge has 3 incoming edges)
        expect(metrics.nodeCount).toBe(8);
        expect(metrics.edgeCount).toBe(9);
        expect(metrics.maxDepth).toBe(5);
        expect(metrics.fanoutPeak).toBe(3);
        expect(metrics.faninPeak).toBe(3);
        // Verify longest path
        expect(metrics.longestPath).toHaveLength(5);
        expect(metrics.longestPath[0]).toBe('sourcer');
        expect(metrics.longestPath[metrics.longestPath.length - 1]).toBe('final');
        // Verify top fanout/fanin
        expect(metrics.topFanout[0]?.id).toBe('sourcer');
        expect(metrics.topFanout[0]?.degree).toBe(3);
        expect(metrics.topFanin[0]?.id).toBe('merge');
        expect(metrics.topFanin[0]?.degree).toBe(3);
        // Compute score with default weights: N*1 + D*5 + F_out*2 + F_in*2 + E*0.5
        // 8*1 + 5*5 + 3*2 + 3*2 + 9*0.5 = 8 + 25 + 6 + 6 + 4.5 = 49 (floored)
        expect(metrics.complexityScore).toBe(49);
    });
});
//# sourceMappingURL=complexity.test.js.map