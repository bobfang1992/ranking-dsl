/**
 * TypeScript bindings generation from node catalog.
 * Implements `rankdsl nodes codegen` (v0.2.8+).
 */
import type { NodeSpec } from './export.js';
/**
 * Generate TypeScript bindings for all nodes.
 */
export declare function generateBindings(specs: NodeSpec[]): string;
/**
 * Generate TypeScript bindings from nodes.yaml
 */
export declare function codegenNodes(options: {
    nodesYamlPath: string;
    outputPath: string;
}): Promise<{
    success: boolean;
    errors: string[];
}>;
//# sourceMappingURL=codegen.d.ts.map