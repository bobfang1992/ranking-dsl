#!/usr/bin/env node
/**
 * @ranking-dsl/cli - rankdsl CLI.
 *
 * TODO: Implement fully in Phase 4.
 */

import { Command } from 'commander';
import * as path from 'node:path';
import { runCodegen } from '@ranking-dsl/codegen';

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

program.parse();
