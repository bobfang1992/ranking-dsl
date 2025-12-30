#!/usr/bin/env node
/**
 * @ranking-dsl/cli - rankdsl CLI (v0.2.8+).
 */

import { Command } from 'commander';
import * as fs from 'node:fs';
import * as path from 'node:path';
import { runCodegen } from '@ranking-dsl/codegen';
import {
  computeComplexityMetrics,
  checkComplexityBudget,
  parseBudgetFromJson,
  defaultBudget,
  type Plan,
} from '@ranking-dsl/shared';
import { exportNodes, codegenNodes } from '@ranking-dsl/nodes-codegen';

const program = new Command();

program
  .name('rankdsl')
  .description('Ranking DSL tooling')
  .version('0.1.0');

program
  .command('codegen')
  .description('Generate keys.ts, keys.h, keys.json from registry.yaml')
  .option('-r, --registry <path>', 'Path to registry.yaml', 'keys/registry.yaml')
  .option('-o, --output <dir>', 'Output directory', 'keys/generated')
  .option('--check', 'Check if generated files are up to date (no write)')
  .action((options) => {
    const result = runCodegen({
      registryPath: path.resolve(options.registry),
      outputDir: path.resolve(options.output),
      checkOnly: options.check,
    });

    if (result.success) {
      if (options.check) {
        console.log('All generated files are up to date.');
      } else {
        console.log('Generated files:');
        for (const file of result.files) {
          console.log(`  - ${file}`);
        }
      }
      process.exit(0);
    } else {
      console.error('Code generation failed:');
      for (const error of result.errors) {
        console.error(`  ${error}`);
      }
      process.exit(1);
    }
  });

program
  .command('validate')
  .description('Validate a plan.json file against complexity budgets')
  .argument('<plan>', 'Path to plan.json file')
  .option('-b, --budget <path>', 'Path to complexity budget JSON file', 'configs/complexity_budgets.json')
  .option('--json', 'Output metrics as JSON')
  .action((planPath, options) => {
    try {
      // Load plan
      const planFile = path.resolve(planPath);
      if (!fs.existsSync(planFile)) {
        console.error(`Error: Plan file not found: ${planFile}`);
        process.exit(1);
      }
      const plan: Plan = JSON.parse(fs.readFileSync(planFile, 'utf-8'));

      // Load budget
      let budget = defaultBudget();
      const budgetFile = path.resolve(options.budget);
      if (fs.existsSync(budgetFile)) {
        const budgetJson = fs.readFileSync(budgetFile, 'utf-8');
        budget = parseBudgetFromJson(budgetJson);
      } else if (options.budget !== 'configs/complexity_budgets.json') {
        console.error(`Error: Budget file not found: ${budgetFile}`);
        process.exit(1);
      }

      // Compute metrics (use budget's score weights if provided)
      const metrics = computeComplexityMetrics(plan, 5, budget.scoreWeights);
      const result = checkComplexityBudget(metrics, budget);

      if (options.json) {
        console.log(JSON.stringify({
          passed: result.passed,
          hasWarnings: result.hasWarnings,
          metrics: {
            nodeCount: metrics.nodeCount,
            edgeCount: metrics.edgeCount,
            maxDepth: metrics.maxDepth,
            fanoutPeak: metrics.fanoutPeak,
            faninPeak: metrics.faninPeak,
            complexityScore: metrics.complexityScore,
          },
          violations: result.violations,
          warnings: result.warnings,
        }, null, 2));
      } else {
        console.log(`Plan: ${planPath}`);
        console.log(`Metrics:`);
        console.log(`  node_count:       ${metrics.nodeCount}`);
        console.log(`  edge_count:       ${metrics.edgeCount}`);
        console.log(`  max_depth:        ${metrics.maxDepth}`);
        console.log(`  fanout_peak:      ${metrics.fanoutPeak}`);
        console.log(`  fanin_peak:       ${metrics.faninPeak}`);
        console.log(`  complexity_score: ${metrics.complexityScore}`);

        if (result.violations.length > 0) {
          console.log(`\nViolations (hard limit exceeded):`);
          for (const v of result.violations) {
            console.log(`  - ${v}`);
          }
        }

        if (result.warnings.length > 0) {
          console.log(`\nWarnings (soft limit exceeded):`);
          for (const w of result.warnings) {
            console.log(`  - ${w}`);
          }
        }

        if (result.passed) {
          console.log(`\nResult: PASSED`);
        } else {
          console.log(`\nResult: FAILED`);
          if (result.diagnostics) {
            console.log(`\n${result.diagnostics}`);
          }
        }
      }

      process.exit(result.passed ? 0 : 1);
    } catch (err) {
      console.error(`Error: ${err instanceof Error ? err.message : String(err)}`);
      process.exit(1);
    }
  });

// nodes command group (v0.2.8+)
const nodesCmd = program
  .command('nodes')
  .description('Node catalog management and code generation');

nodesCmd
  .command('export')
  .description('Export node catalog from C++ engine and njs modules')
  .option('--engine-build <dir>', 'Path to engine build directory', 'engine/build')
  .option('--njs <dir>', 'Path to njs modules directory', 'njs')
  .option('-o, --output <path>', 'Output YAML path', 'nodes/generated/nodes.yaml')
  .action(async (options) => {
    const result = await exportNodes({
      engineBuildDir: path.resolve(options.engineBuild),
      njsDir: path.resolve(options.njs),
      outputPath: path.resolve(options.output),
    });

    if (result.success) {
      process.exit(0);
    } else {
      console.error('Node export failed:');
      for (const error of result.errors) {
        console.error(`  ${error}`);
      }
      process.exit(1);
    }
  });

nodesCmd
  .command('codegen')
  .description('Generate TypeScript bindings from node catalog')
  .option('-i, --input <path>', 'Input nodes.yaml path', 'nodes/generated/nodes.yaml')
  .option('-o, --output <path>', 'Output TypeScript path', 'nodes/generated/nodes.ts')
  .action(async (options) => {
    const result = await codegenNodes({
      nodesYamlPath: path.resolve(options.input),
      outputPath: path.resolve(options.output),
    });

    if (result.success) {
      process.exit(0);
    } else {
      console.error('Node codegen failed:');
      for (const error of result.errors) {
        console.error(`  ${error}`);
      }
      process.exit(1);
    }
  });

nodesCmd
  .command('graduate <node>')
  .description('Graduate an experimental node to stable')
  .option('--to <namespace>', 'New namespace path (e.g., core.merge.weightedUnion)')
  .action((node, options) => {
    // TODO: Implement graduation workflow
    console.log(`Graduate node ${node} to ${options.to}`);
    console.log('Graduation workflow: Update NodeSpec source, regenerate catalog + bindings');
    process.exit(1); // Not implemented yet
  });

program.parse();
