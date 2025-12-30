"use strict";
/**
 * TypeScript bindings generation from node catalog.
 * Implements `rankdsl nodes codegen` (v0.2.8+).
 */
var __createBinding = (this && this.__createBinding) || (Object.create ? (function(o, m, k, k2) {
    if (k2 === undefined) k2 = k;
    var desc = Object.getOwnPropertyDescriptor(m, k);
    if (!desc || ("get" in desc ? !m.__esModule : desc.writable || desc.configurable)) {
      desc = { enumerable: true, get: function() { return m[k]; } };
    }
    Object.defineProperty(o, k2, desc);
}) : (function(o, m, k, k2) {
    if (k2 === undefined) k2 = k;
    o[k2] = m[k];
}));
var __setModuleDefault = (this && this.__setModuleDefault) || (Object.create ? (function(o, v) {
    Object.defineProperty(o, "default", { enumerable: true, value: v });
}) : function(o, v) {
    o["default"] = v;
});
var __importStar = (this && this.__importStar) || (function () {
    var ownKeys = function(o) {
        ownKeys = Object.getOwnPropertyNames || function (o) {
            var ar = [];
            for (var k in o) if (Object.prototype.hasOwnProperty.call(o, k)) ar[ar.length] = k;
            return ar;
        };
        return ownKeys(o);
    };
    return function (mod) {
        if (mod && mod.__esModule) return mod;
        var result = {};
        if (mod != null) for (var k = ownKeys(mod), i = 0; i < k.length; i++) if (k[i] !== "default") __createBinding(result, mod, k[i]);
        __setModuleDefault(result, mod);
        return result;
    };
})();
Object.defineProperty(exports, "__esModule", { value: true });
exports.generateBindings = generateBindings;
exports.codegenNodes = codegenNodes;
const fs = __importStar(require("node:fs"));
const path = __importStar(require("node:path"));
const yaml = __importStar(require("js-yaml"));
/**
 * Build a nested namespace tree from node specs.
 */
function buildNamespaceTree(specs) {
    const tree = new Map();
    for (const spec of specs) {
        const parts = spec.namespace_path.split('.');
        const namespace = parts.slice(0, -1).join('.');
        if (!tree.has(namespace)) {
            tree.set(namespace, []);
        }
        tree.get(namespace).push(spec);
    }
    return tree;
}
/**
 * Generate docstring comment for a node method.
 */
function generateDocstring(spec) {
    const lines = [];
    lines.push('/**');
    lines.push(` * ${spec.doc || 'No description'}`);
    lines.push(' *');
    if (spec.stability === 'experimental') {
        lines.push(' * **WARNING**: This node is EXPERIMENTAL and must not be used in production plans.');
        lines.push(' *');
    }
    // Reads
    if (spec.reads && spec.reads.length > 0) {
        lines.push(' * @reads');
        for (const key of spec.reads) {
            lines.push(` *   - ${key.name || `key_${key.id}`} (${key.id})`);
        }
    }
    // Writes
    if (spec.writes) {
        if (spec.writes.kind === 'static' && spec.writes.keys) {
            if (spec.writes.keys.length > 0) {
                lines.push(' * @writes');
                for (const key of spec.writes.keys) {
                    lines.push(` *   - ${key.name || `key_${key.id}`} (${key.id})`);
                }
            }
        }
        else if (spec.writes.kind === 'param_derived') {
            lines.push(` * @writes Keys specified in params.${spec.writes.param_name}`);
        }
    }
    lines.push(` * @stability ${spec.stability}`);
    lines.push(' */');
    return lines.join('\n');
}
/**
 * Generate TypeScript type for params schema (simplified).
 */
function generateParamsType(spec) {
    // For MVP, generate a simple any type or Record<string, any>
    // Production would use JSON Schema -> TS type conversion
    return 'Record<string, any>';
}
/**
 * Generate method name from namespace_path.
 */
function getMethodName(spec) {
    const parts = spec.namespace_path.split('.');
    return parts[parts.length - 1];
}
/**
 * Generate TypeScript bindings for all nodes.
 */
function generateBindings(specs) {
    const lines = [];
    // Header
    lines.push('/**');
    lines.push(' * Generated TypeScript bindings for ranking DSL nodes (v0.2.8+).');
    lines.push(' * DO NOT EDIT - this file is auto-generated from node catalog.');
    lines.push(' * Regenerate with: rankdsl nodes codegen');
    lines.push(' */');
    lines.push('');
    lines.push('import type { Pipeline, NodeOpts } from \'./types.js\';');
    lines.push('');
    // Build namespace tree
    const tree = buildNamespaceTree(specs);
    // Generate namespace interfaces
    const namespaces = new Set();
    for (const spec of specs) {
        const parts = spec.namespace_path.split('.');
        for (let i = 1; i <= parts.length - 1; i++) {
            namespaces.add(parts.slice(0, i).join('.'));
        }
    }
    // Sort namespaces by depth (deepest first for proper ordering)
    const sortedNamespaces = Array.from(namespaces).sort((a, b) => {
        const depthDiff = b.split('.').length - a.split('.').length;
        if (depthDiff !== 0)
            return depthDiff;
        return a.localeCompare(b);
    });
    // Generate interface for each namespace level
    const interfaceMap = new Map();
    for (const ns of sortedNamespaces) {
        const parts = ns.split('.');
        const interfaceName = parts.map(p => p.charAt(0).toUpperCase() + p.slice(1)).join('') + 'Namespace';
        const children = tree.get(ns) || [];
        const members = [];
        // Add methods for nodes in this namespace
        for (const spec of children) {
            const methodName = getMethodName(spec);
            const paramsType = generateParamsType(spec);
            members.push(generateDocstring(spec));
            members.push(`  ${methodName}(params: ${paramsType}, opts?: NodeOpts): Pipeline;`);
            members.push('');
        }
        // Add child namespaces
        for (const childNs of sortedNamespaces) {
            if (childNs.startsWith(ns + '.') && childNs.split('.').length === parts.length + 1) {
                const childParts = childNs.split('.');
                const childName = childParts[childParts.length - 1];
                const childInterface = childParts.map(p => p.charAt(0).toUpperCase() + p.slice(1)).join('') + 'Namespace';
                members.push(`  ${childName}: ${childInterface};`);
            }
        }
        lines.push(`export interface ${interfaceName} {`);
        lines.push(members.join('\n'));
        lines.push('}');
        lines.push('');
        interfaceMap.set(ns, interfaceName);
    }
    // Generate PipelineWithNodes interface that extends Pipeline
    lines.push('/**');
    lines.push(' * Pipeline interface extended with generated node methods.');
    lines.push(' */');
    lines.push('export interface PipelineWithNodes extends Pipeline {');
    // Add top-level namespaces
    const topLevelNamespaces = Array.from(namespaces).filter(ns => !ns.includes('.'));
    for (const ns of topLevelNamespaces.sort()) {
        const interfaceName = interfaceMap.get(ns);
        lines.push(`  ${ns}: ${interfaceName};`);
    }
    lines.push('}');
    lines.push('');
    // Export node registry (for runtime validation)
    lines.push('/**');
    lines.push(' * Node registry for runtime validation.');
    lines.push(' */');
    lines.push('export const NODE_REGISTRY = {');
    for (const spec of specs) {
        lines.push(`  "${spec.op}": ${JSON.stringify(spec, null, 2)},`);
    }
    lines.push('} as const;');
    lines.push('');
    return lines.join('\n');
}
/**
 * Generate TypeScript bindings from nodes.yaml
 */
async function codegenNodes(options) {
    const errors = [];
    try {
        if (!fs.existsSync(options.nodesYamlPath)) {
            errors.push(`Nodes catalog not found: ${options.nodesYamlPath}. Run 'rankdsl nodes export' first.`);
            return { success: false, errors };
        }
        // Load nodes catalog
        const yamlContent = fs.readFileSync(options.nodesYamlPath, 'utf8');
        const specs = yaml.load(yamlContent);
        // Generate TypeScript bindings
        const tsCode = generateBindings(specs);
        // Write output
        fs.mkdirSync(path.dirname(options.outputPath), { recursive: true });
        fs.writeFileSync(options.outputPath, tsCode, 'utf8');
        console.log(`Generated TypeScript bindings: ${options.outputPath}`);
        console.log(`  ${specs.length} nodes registered`);
        return { success: true, errors: [] };
    }
    catch (error) {
        errors.push(error.message);
        return { success: false, errors };
    }
}
//# sourceMappingURL=codegen.js.map