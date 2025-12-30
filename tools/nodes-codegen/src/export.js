"use strict";
/**
 * Node catalog export: collects NodeSpecs from C++ engine and njs modules.
 * Implements `rankdsl nodes export` (v0.2.8+).
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
exports.exportCoreNodes = exportCoreNodes;
exports.exportNjsNodes = exportNjsNodes;
exports.exportNodes = exportNodes;
const fs = __importStar(require("node:fs"));
const path = __importStar(require("node:path"));
const child_process = __importStar(require("node:child_process"));
const crypto = __importStar(require("node:crypto"));
const yaml = __importStar(require("js-yaml"));
/**
 * Export core C++ NodeSpecs by calling rankdsl-export-nodes executable.
 */
function exportCoreNodes(engineBuildDir) {
    const exportBin = path.join(engineBuildDir, 'rankdsl_export_nodes');
    if (!fs.existsSync(exportBin)) {
        throw new Error(`Engine export binary not found: ${exportBin}. Run 'cmake --build engine/build' first.`);
    }
    const output = child_process.execFileSync(exportBin, { encoding: 'utf8' });
    const specs = JSON.parse(output);
    return specs;
}
/**
 * Compute SHA256 digest of a file.
 */
function computeDigest(filePath) {
    const content = fs.readFileSync(filePath);
    return crypto.createHash('sha256').update(content).digest('hex');
}
/**
 * Extract exports.meta from an .njs module (static parse or eval in sandbox).
 * For MVP, we use simple regex parsing. Production should use a proper JS parser.
 */
function extractNjsMeta(filePath) {
    const content = fs.readFileSync(filePath, 'utf8');
    // Simple regex to extract exports.meta = { ... }
    // This is fragile but works for well-formed modules
    const metaMatch = content.match(/exports\.meta\s*=\s*(\{[\s\S]*?\n\})\s*;/);
    if (!metaMatch) {
        throw new Error(`Could not extract exports.meta from ${filePath}`);
    }
    // Use eval in a controlled way (this is a build-time tool, not runtime)
    const meta = eval(`(${metaMatch[1]})`);
    return meta;
}
/**
 * Export njs NodeSpecs by scanning njs/ directory.
 */
function exportNjsNodes(njsDir) {
    const specs = [];
    if (!fs.existsSync(njsDir)) {
        return specs; // No njs directory yet
    }
    // Recursively find all .njs files
    function findNjsFiles(dir) {
        const results = [];
        const entries = fs.readdirSync(dir, { withFileTypes: true });
        for (const entry of entries) {
            const fullPath = path.join(dir, entry.name);
            if (entry.isDirectory()) {
                results.push(...findNjsFiles(fullPath));
            }
            else if (entry.isFile() && entry.name.endsWith('.njs')) {
                results.push(fullPath);
            }
        }
        return results;
    }
    const njsFiles = findNjsFiles(njsDir);
    for (const njsPath of njsFiles) {
        try {
            const meta = extractNjsMeta(njsPath);
            const relativePath = path.relative(njsDir, njsPath);
            const digest = computeDigest(njsPath);
            const spec = {
                op: `js:${relativePath}@${meta.version}#${digest.substring(0, 16)}`,
                namespace_path: meta.namespace_path || `njs.${meta.name}`,
                stability: meta.stability || 'stable',
                doc: meta.doc || '',
                params_schema: meta.params_schema || {},
                reads: (meta.reads || []).map((id) => ({ id })),
                writes: {
                    kind: 'static',
                    keys: (meta.writes || []).map((id) => ({ id })),
                },
                kind: 'njs',
                budgets: meta.budget,
                capabilities: meta.capabilities,
                njs_path: relativePath,
                version: meta.version,
                digest: digest.substring(0, 16),
            };
            specs.push(spec);
        }
        catch (error) {
            console.warn(`Warning: Failed to extract meta from ${njsPath}: ${error.message}`);
        }
    }
    return specs;
}
/**
 * Export all nodes (core + njs) and write to nodes/generated/nodes.yaml
 */
async function exportNodes(options) {
    const errors = [];
    try {
        // Export core nodes
        const coreSpecs = exportCoreNodes(options.engineBuildDir);
        console.log(`Exported ${coreSpecs.length} core nodes`);
        // Export njs nodes
        const njsSpecs = exportNjsNodes(options.njsDir);
        console.log(`Exported ${njsSpecs.length} njs nodes`);
        // Combine and sort by namespace_path
        const allSpecs = [...coreSpecs, ...njsSpecs];
        allSpecs.sort((a, b) => a.namespace_path.localeCompare(b.namespace_path));
        // Validate stability/namespace consistency
        for (const spec of allSpecs) {
            const isExperimentalNamespace = spec.namespace_path.startsWith('experimental.');
            if (spec.stability === 'experimental' && !isExperimentalNamespace) {
                errors.push(`Node ${spec.op}: stability=experimental but namespace_path does not start with 'experimental.'`);
            }
            if (spec.stability === 'stable' && isExperimentalNamespace) {
                errors.push(`Node ${spec.op}: stability=stable but namespace_path starts with 'experimental.'`);
            }
        }
        if (errors.length > 0) {
            return { success: false, errors };
        }
        // Write to YAML
        const yamlContent = yaml.dump(allSpecs, { indent: 2, lineWidth: 120 });
        fs.mkdirSync(path.dirname(options.outputPath), { recursive: true });
        fs.writeFileSync(options.outputPath, yamlContent, 'utf8');
        console.log(`Wrote ${allSpecs.length} node specs to ${options.outputPath}`);
        return { success: true, errors: [] };
    }
    catch (error) {
        errors.push(error.message);
        return { success: false, errors };
    }
}
//# sourceMappingURL=export.js.map