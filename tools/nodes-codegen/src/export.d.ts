/**
 * Node catalog export: collects NodeSpecs from C++ engine and njs modules.
 * Implements `rankdsl nodes export` (v0.2.8+).
 */
export interface NodeSpec {
    op: string;
    namespace_path: string;
    stability: 'stable' | 'experimental';
    doc: string;
    params_schema: object;
    reads: Array<{
        id: number;
        name?: string;
    }>;
    writes: {
        kind: 'static' | 'param_derived';
        keys?: Array<{
            id: number;
            name?: string;
        }>;
        param_name?: string;
    };
    kind: 'core' | 'njs';
    budgets?: object;
    capabilities?: object;
    njs_path?: string;
    version?: string;
    digest?: string;
}
/**
 * Export core C++ NodeSpecs by calling rankdsl-export-nodes executable.
 */
export declare function exportCoreNodes(engineBuildDir: string): NodeSpec[];
/**
 * Export njs NodeSpecs by scanning njs/ directory.
 */
export declare function exportNjsNodes(njsDir: string): NodeSpec[];
/**
 * Export all nodes (core + njs) and write to nodes/generated/nodes.yaml
 */
export declare function exportNodes(options: {
    engineBuildDir: string;
    njsDir: string;
    outputPath: string;
}): Promise<{
    success: boolean;
    errors: string[];
}>;
//# sourceMappingURL=export.d.ts.map