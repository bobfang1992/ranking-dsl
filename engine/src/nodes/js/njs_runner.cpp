#include "nodes/js/njs_runner.h"

#include <cctype>
#include <fstream>
#include <sstream>
#include <stdexcept>

extern "C" {
#include "quickjs.h"
}

#include "keys/registry.h"
#include "nodes/registry.h"

namespace ranking_dsl {

// Parse NjsMeta from JSON
NjsMeta NjsMeta::Parse(const nlohmann::json& j) {
  NjsMeta meta;

  if (j.contains("name")) {
    meta.name = j["name"].get<std::string>();
  }
  if (j.contains("version")) {
    meta.version = j["version"].get<std::string>();
  }

  if (j.contains("reads") && j["reads"].is_array()) {
    for (const auto& key : j["reads"]) {
      // Handle both integer and float (JS numbers are all doubles)
      if (key.is_number()) {
        meta.reads.insert(static_cast<int32_t>(key.get<double>()));
      }
    }
  }

  if (j.contains("writes") && j["writes"].is_array()) {
    for (const auto& key : j["writes"]) {
      // Handle both integer and float (JS numbers are all doubles)
      if (key.is_number()) {
        meta.writes.insert(static_cast<int32_t>(key.get<double>()));
      }
    }
  }

  if (j.contains("params")) {
    meta.params_schema = j["params"];
  }

  if (j.contains("budget")) {
    const auto& budget = j["budget"];
    if (budget.contains("max_write_bytes")) {
      meta.budget.max_write_bytes = budget["max_write_bytes"].get<int64_t>();
    }
    if (budget.contains("max_write_cells")) {
      meta.budget.max_write_cells = budget["max_write_cells"].get<int64_t>();
    }
    if (budget.contains("max_set_per_obj")) {
      meta.budget.max_set_per_obj = budget["max_set_per_obj"].get<int64_t>();
    }
    if (budget.contains("max_io_read_bytes")) {
      meta.budget.max_io_read_bytes = budget["max_io_read_bytes"].get<int64_t>();
    }
    if (budget.contains("max_io_read_rows")) {
      meta.budget.max_io_read_rows = budget["max_io_read_rows"].get<int64_t>();
    }
  }

  // Parse capabilities
  if (j.contains("capabilities")) {
    const auto& caps = j["capabilities"];
    if (caps.contains("io") && caps["io"].is_object()) {
      const auto& io = caps["io"];
      if (io.contains("csv_read") && io["csv_read"].is_boolean()) {
        meta.capabilities.io.csv_read = io["csv_read"].get<bool>();
      }
    }
  }

  return meta;
}

// NjsPolicy implementation
bool NjsPolicy::LoadFromFile(const std::string& path, std::string* error_out) {
  std::ifstream file(path);
  if (!file.is_open()) {
    if (error_out) *error_out = "Failed to open policy file: " + path;
    return false;
  }
  std::stringstream buffer;
  buffer << file.rdbuf();
  return LoadFromJson(buffer.str(), error_out);
}

bool NjsPolicy::LoadFromJson(const std::string& json_str, std::string* error_out) {
  try {
    auto j = nlohmann::json::parse(json_str);

    if (j.contains("csv_assets_dir")) {
      csv_assets_dir_ = j["csv_assets_dir"].get<std::string>();
    }

    if (j.contains("modules") && j["modules"].is_array()) {
      for (const auto& mod : j["modules"]) {
        NjsPolicyEntry entry;
        if (mod.contains("name")) {
          entry.name = mod["name"].get<std::string>();
        }
        if (mod.contains("version")) {
          entry.version = mod["version"].get<std::string>();
        }
        if (mod.contains("allow_io_csv_read")) {
          entry.allow_io_csv_read = mod["allow_io_csv_read"].get<bool>();
        }
        entries_.push_back(entry);
      }
    }
    return true;
  } catch (const std::exception& e) {
    if (error_out) *error_out = std::string("JSON parse error: ") + e.what();
    return false;
  }
}

bool NjsPolicy::IsIoCsvReadAllowed(const std::string& name, const std::string& version) const {
  for (const auto& entry : entries_) {
    // Match by name, and optionally version (empty version = any)
    if (entry.name == name) {
      if (entry.version.empty() || entry.version == version) {
        return entry.allow_io_csv_read;
      }
    }
  }
  return false;  // Default deny
}

// Tracked write array for committing data back
struct TrackedWriteArray {
  int32_t key_id;
  JSValue js_array;
  void* data_ptr;
  size_t count;
  keys::KeyType type;
};

// Context passed to JS functions
struct JsContext {
  BatchContext* batch_ctx;
  const nlohmann::json* params;
  const KeyRegistry* registry;
  int64_t instruction_count;
  int64_t max_instructions;
  bool interrupted;
  std::vector<TrackedWriteArray> tracked_writes;

  // IO context
  bool io_enabled;
  std::string csv_assets_dir;
  NjsBudget* budget;  // For IO budget tracking
};

// Get string from JS value (forward declaration for use in IO functions)
static std::string JsGetString(JSContext* ctx, JSValueConst val);

// Convert nlohmann::json to JS value (forward declaration)
static JSValue JsonToJs(JSContext* ctx, const nlohmann::json& j);

// Helper: validate CSV resource path (no traversal, no absolute)
static bool ValidateCsvPath(const std::string& resource, std::string* error_out) {
  // Reject absolute paths
  if (!resource.empty() && (resource[0] == '/' || resource[0] == '\\')) {
    if (error_out) *error_out = "Absolute paths not allowed: " + resource;
    return false;
  }
  // Reject path traversal
  if (resource.find("..") != std::string::npos) {
    if (error_out) *error_out = "Path traversal not allowed: " + resource;
    return false;
  }
  // Reject backslash (Windows-style)
  if (resource.find('\\') != std::string::npos) {
    if (error_out) *error_out = "Backslash not allowed in path: " + resource;
    return false;
  }
  return true;
}

// Helper: parse CSV file and return { columns: { col: [...] }, rowCount: N }
static nlohmann::json ParseCsvFile(const std::string& path, NjsBudget& budget,
                                    std::string* error_out) {
  // Enforce "0 = no IO allowed" semantics
  if (budget.max_io_read_bytes == 0 || budget.max_io_read_rows == 0) {
    if (error_out) *error_out = "IO budget not configured (max_io_read_bytes/rows = 0)";
    return nullptr;
  }

  std::ifstream file(path);
  if (!file.is_open()) {
    if (error_out) *error_out = "Failed to open CSV file: " + path;
    return nullptr;
  }

  nlohmann::json result;
  result["columns"] = nlohmann::json::object();
  result["rowCount"] = 0;

  std::string line;
  std::vector<std::string> headers;
  std::vector<std::vector<std::string>> columns;
  int64_t row_count = 0;
  int64_t bytes_read = 0;

  // Read header
  if (std::getline(file, line)) {
    bytes_read += line.size() + 1;

    // Simple CSV parsing (comma-separated, no quotes handling for MVP)
    std::stringstream ss(line);
    std::string cell;
    while (std::getline(ss, cell, ',')) {
      // Trim whitespace
      size_t start = cell.find_first_not_of(" \t\r\n");
      size_t end = cell.find_last_not_of(" \t\r\n");
      if (start != std::string::npos && end != std::string::npos) {
        cell = cell.substr(start, end - start + 1);
      }
      headers.push_back(cell);
      columns.push_back({});
    }
  }

  // Read data rows
  while (std::getline(file, line)) {
    // Check IO budget (cumulative across all readCsv calls)
    bytes_read += line.size() + 1;
    int64_t total_bytes = budget.io_bytes_read + bytes_read;
    int64_t total_rows = budget.io_rows_read + row_count + 1;
    if (total_bytes > budget.max_io_read_bytes) {
      if (error_out) *error_out = "IO budget exceeded: max_io_read_bytes";
      return nullptr;
    }
    if (total_rows > budget.max_io_read_rows) {
      if (error_out) *error_out = "IO budget exceeded: max_io_read_rows";
      return nullptr;
    }

    std::stringstream ss(line);
    std::string cell;
    size_t col_idx = 0;
    while (std::getline(ss, cell, ',') && col_idx < columns.size()) {
      // Trim whitespace
      size_t start = cell.find_first_not_of(" \t\r\n");
      size_t end = cell.find_last_not_of(" \t\r\n");
      if (start != std::string::npos && end != std::string::npos) {
        cell = cell.substr(start, end - start + 1);
      } else {
        cell = "";
      }
      columns[col_idx].push_back(cell);
      col_idx++;
    }
    // Fill missing columns
    while (col_idx < columns.size()) {
      columns[col_idx].push_back("");
      col_idx++;
    }
    row_count++;
  }

  // Update budget tracking
  budget.io_bytes_read += bytes_read;
  budget.io_rows_read += row_count;

  // Build result object
  for (size_t i = 0; i < headers.size(); i++) {
    result["columns"][headers[i]] = columns[i];
  }
  result["rowCount"] = row_count;

  return result;
}

// ctx.io.readCsv(resource, opts?)
static JSValue JsIoReadCsv(JSContext* ctx, JSValueConst this_val,
                           int argc, JSValueConst* argv, int magic, JSValue* func_data) {
  auto* js_ctx = static_cast<JsContext*>(JS_GetContextOpaque(ctx));

  // Check if IO is enabled
  if (!js_ctx->io_enabled) {
    return JS_ThrowTypeError(ctx, "IO capability not enabled for this module");
  }

  if (argc < 1) {
    return JS_ThrowTypeError(ctx, "readCsv requires resource argument");
  }

  std::string resource = JsGetString(ctx, argv[0]);

  // Validate path
  std::string error;
  if (!ValidateCsvPath(resource, &error)) {
    return JS_ThrowTypeError(ctx, "%s", error.c_str());
  }

  // Resolve full path under assets directory
  std::string full_path = js_ctx->csv_assets_dir + "/" + resource;

  // Parse CSV
  nlohmann::json csv_data = ParseCsvFile(full_path, *js_ctx->budget, &error);
  if (csv_data.is_null()) {
    return JS_ThrowTypeError(ctx, "%s", error.c_str());
  }

  // Convert to JS object
  return JsonToJs(ctx, csv_data);
}

// Get string from JS value
static std::string JsGetString(JSContext* ctx, JSValueConst val) {
  const char* str = JS_ToCString(ctx, val);
  if (!str) return "";
  std::string result(str);
  JS_FreeCString(ctx, str);
  return result;
}

// Convert JS value to nlohmann::json
static nlohmann::json JsToJson(JSContext* ctx, JSValueConst val) {
  if (JS_IsNull(val) || JS_IsUndefined(val)) {
    return nullptr;
  }
  if (JS_IsBool(val)) {
    return JS_ToBool(ctx, val) != 0;
  }
  if (JS_IsNumber(val)) {
    double d;
    JS_ToFloat64(ctx, &d, val);
    return d;
  }
  if (JS_IsString(val)) {
    return JsGetString(ctx, val);
  }
  if (JS_IsArray(ctx, val)) {
    nlohmann::json arr = nlohmann::json::array();
    JSValue length_val = JS_GetPropertyStr(ctx, val, "length");
    int64_t length;
    JS_ToInt64(ctx, &length, length_val);
    JS_FreeValue(ctx, length_val);
    for (int64_t i = 0; i < length; i++) {
      JSValue elem = JS_GetPropertyUint32(ctx, val, i);
      arr.push_back(JsToJson(ctx, elem));
      JS_FreeValue(ctx, elem);
    }
    return arr;
  }
  if (JS_IsObject(val)) {
    nlohmann::json obj = nlohmann::json::object();
    JSPropertyEnum* props;
    uint32_t prop_count;
    if (JS_GetOwnPropertyNames(ctx, &props, &prop_count, val, JS_GPN_STRING_MASK | JS_GPN_ENUM_ONLY) == 0) {
      for (uint32_t i = 0; i < prop_count; i++) {
        const char* key = JS_AtomToCString(ctx, props[i].atom);
        if (key) {
          JSValue prop_val = JS_GetProperty(ctx, val, props[i].atom);
          obj[key] = JsToJson(ctx, prop_val);
          JS_FreeValue(ctx, prop_val);
          JS_FreeCString(ctx, key);
        }
        JS_FreeAtom(ctx, props[i].atom);
      }
      js_free(ctx, props);
    }
    return obj;
  }
  return nullptr;
}

// Convert nlohmann::json to JS value
static JSValue JsonToJs(JSContext* ctx, const nlohmann::json& j) {
  if (j.is_null()) {
    return JS_NULL;
  }
  if (j.is_boolean()) {
    return JS_NewBool(ctx, j.get<bool>());
  }
  if (j.is_number_integer()) {
    return JS_NewInt64(ctx, j.get<int64_t>());
  }
  if (j.is_number_float()) {
    return JS_NewFloat64(ctx, j.get<double>());
  }
  if (j.is_string()) {
    return JS_NewString(ctx, j.get<std::string>().c_str());
  }
  if (j.is_array()) {
    JSValue arr = JS_NewArray(ctx);
    for (size_t i = 0; i < j.size(); i++) {
      JS_SetPropertyUint32(ctx, arr, i, JsonToJs(ctx, j[i]));
    }
    return arr;
  }
  if (j.is_object()) {
    JSValue obj = JS_NewObject(ctx);
    for (auto& [key, val] : j.items()) {
      JS_SetPropertyStr(ctx, obj, key.c_str(), JsonToJs(ctx, val));
    }
    return obj;
  }
  return JS_UNDEFINED;
}

// Interrupt handler for instruction counting
static int JsInterruptHandler(JSRuntime* rt, void* opaque) {
  auto* js_ctx = static_cast<JsContext*>(opaque);
  js_ctx->instruction_count++;
  if (js_ctx->instruction_count >= js_ctx->max_instructions) {
    js_ctx->interrupted = true;
    return 1;  // Signal interrupt
  }
  return 0;
}

// ctx.batch.rowCount()
static JSValue JsBatchRowCount(JSContext* ctx, JSValueConst this_val,
                                int argc, JSValueConst* argv, int magic, JSValue* func_data) {
  auto* js_ctx = static_cast<JsContext*>(JS_GetContextOpaque(ctx));
  return JS_NewInt64(ctx, js_ctx->batch_ctx->RowCount());
}

// ctx.batch.f32(keyId)
static JSValue JsBatchGetF32(JSContext* ctx, JSValueConst this_val,
                              int argc, JSValueConst* argv, int magic, JSValue* func_data) {
  if (argc < 1) return JS_ThrowTypeError(ctx, "f32 requires key_id argument");

  auto* js_ctx = static_cast<JsContext*>(JS_GetContextOpaque(ctx));
  int32_t key_id;
  JS_ToInt32(ctx, &key_id, argv[0]);

  auto [data, size] = js_ctx->batch_ctx->GetF32Raw(key_id);
  if (!data) {
    return JS_NULL;
  }

  // Create Float32Array view (copies data for safety)
  JSValue arr = JS_NewArray(ctx);
  for (size_t i = 0; i < size; i++) {
    JS_SetPropertyUint32(ctx, arr, i, JS_NewFloat64(ctx, data[i]));
  }
  return arr;
}

// ctx.batch.i64(keyId)
static JSValue JsBatchGetI64(JSContext* ctx, JSValueConst this_val,
                              int argc, JSValueConst* argv, int magic, JSValue* func_data) {
  if (argc < 1) return JS_ThrowTypeError(ctx, "i64 requires key_id argument");

  auto* js_ctx = static_cast<JsContext*>(JS_GetContextOpaque(ctx));
  int32_t key_id;
  JS_ToInt32(ctx, &key_id, argv[0]);

  auto [data, size] = js_ctx->batch_ctx->GetI64Raw(key_id);
  if (!data) {
    return JS_NULL;
  }

  JSValue arr = JS_NewArray(ctx);
  for (size_t i = 0; i < size; i++) {
    JS_SetPropertyUint32(ctx, arr, i, JS_NewInt64(ctx, data[i]));
  }
  return arr;
}

// ctx.batch.writeF32(keyId)
static JSValue JsBatchWriteF32(JSContext* ctx, JSValueConst this_val,
                                int argc, JSValueConst* argv, int magic, JSValue* func_data) {
  if (argc < 1) return JS_ThrowTypeError(ctx, "writeF32 requires key_id argument");

  auto* js_ctx = static_cast<JsContext*>(JS_GetContextOpaque(ctx));
  int32_t key_id;
  JS_ToInt32(ctx, &key_id, argv[0]);

  try {
    float* data = js_ctx->batch_ctx->AllocateF32(key_id);
    size_t size = js_ctx->batch_ctx->RowCount();

    // Create JS array for the user to write to
    JSValue arr = JS_NewArray(ctx);
    for (size_t i = 0; i < size; i++) {
      JS_SetPropertyUint32(ctx, arr, i, JS_NewFloat64(ctx, 0.0));
    }

    // Track this array so we can copy data back later
    TrackedWriteArray tracked;
    tracked.key_id = key_id;
    tracked.js_array = JS_DupValue(ctx, arr);  // Keep a reference
    tracked.data_ptr = data;
    tracked.count = size;
    tracked.type = keys::KeyType::F32;
    js_ctx->tracked_writes.push_back(tracked);

    return arr;
  } catch (const std::exception& e) {
    return JS_ThrowTypeError(ctx, "%s", e.what());
  }
}

// ctx.batch.writeI64(keyId)
static JSValue JsBatchWriteI64(JSContext* ctx, JSValueConst this_val,
                                int argc, JSValueConst* argv, int magic, JSValue* func_data) {
  if (argc < 1) return JS_ThrowTypeError(ctx, "writeI64 requires key_id argument");

  auto* js_ctx = static_cast<JsContext*>(JS_GetContextOpaque(ctx));
  int32_t key_id;
  JS_ToInt32(ctx, &key_id, argv[0]);

  try {
    int64_t* data = js_ctx->batch_ctx->AllocateI64(key_id);
    size_t size = js_ctx->batch_ctx->RowCount();

    JSValue arr = JS_NewArray(ctx);
    for (size_t i = 0; i < size; i++) {
      JS_SetPropertyUint32(ctx, arr, i, JS_NewInt64(ctx, 0));
    }

    // Track this array so we can copy data back later
    TrackedWriteArray tracked;
    tracked.key_id = key_id;
    tracked.js_array = JS_DupValue(ctx, arr);
    tracked.data_ptr = data;
    tracked.count = size;
    tracked.type = keys::KeyType::I64;
    js_ctx->tracked_writes.push_back(tracked);

    return arr;
  } catch (const std::exception& e) {
    return JS_ThrowTypeError(ctx, "%s", e.what());
  }
}

// Commit tracked write arrays back to their C++ storage
static void CommitTrackedWrites(JSContext* ctx, JsContext* js_ctx) {
  for (auto& tracked : js_ctx->tracked_writes) {
    if (tracked.type == keys::KeyType::F32) {
      float* data = static_cast<float*>(tracked.data_ptr);
      for (size_t i = 0; i < tracked.count; i++) {
        JSValue elem = JS_GetPropertyUint32(ctx, tracked.js_array, i);
        double val;
        JS_ToFloat64(ctx, &val, elem);
        data[i] = static_cast<float>(val);
        JS_FreeValue(ctx, elem);
      }
    } else if (tracked.type == keys::KeyType::I64) {
      int64_t* data = static_cast<int64_t*>(tracked.data_ptr);
      for (size_t i = 0; i < tracked.count; i++) {
        JSValue elem = JS_GetPropertyUint32(ctx, tracked.js_array, i);
        int64_t val;
        JS_ToInt64(ctx, &val, elem);
        data[i] = val;
        JS_FreeValue(ctx, elem);
      }
    }
    // Free the duplicated JS value
    JS_FreeValue(ctx, tracked.js_array);
  }
  js_ctx->tracked_writes.clear();
}

// Implementation class
class NjsRunner::Impl {
 public:
  JSRuntime* rt = nullptr;
  JSContext* ctx = nullptr;
  JsContext js_ctx;

  Impl() {
    rt = JS_NewRuntime();
    // Note: We don't create ctx here - we create fresh contexts per execution
    // to ensure clean sandbox state. This also ensures std/os modules are never exposed.
  }

  ~Impl() {
    if (ctx) JS_FreeContext(ctx);
    if (rt) JS_FreeRuntime(rt);
  }

  void SetupBatchAPI(JSContext* js_ctx_handle, JSValueConst batch_obj) {
    JS_SetPropertyStr(js_ctx_handle, batch_obj, "rowCount",
      JS_NewCFunctionData(js_ctx_handle, JsBatchRowCount, 0, 0, 0, nullptr));
    JS_SetPropertyStr(js_ctx_handle, batch_obj, "f32",
      JS_NewCFunctionData(js_ctx_handle, JsBatchGetF32, 1, 0, 0, nullptr));
    JS_SetPropertyStr(js_ctx_handle, batch_obj, "i64",
      JS_NewCFunctionData(js_ctx_handle, JsBatchGetI64, 1, 0, 0, nullptr));
    JS_SetPropertyStr(js_ctx_handle, batch_obj, "writeF32",
      JS_NewCFunctionData(js_ctx_handle, JsBatchWriteF32, 1, 0, 0, nullptr));
    JS_SetPropertyStr(js_ctx_handle, batch_obj, "writeI64",
      JS_NewCFunctionData(js_ctx_handle, JsBatchWriteI64, 1, 0, 0, nullptr));
  }

  void SetupIoAPI(JSContext* js_ctx_handle, JSValueConst io_obj) {
    JS_SetPropertyStr(js_ctx_handle, io_obj, "readCsv",
      JS_NewCFunctionData(js_ctx_handle, JsIoReadCsv, 1, 0, 0, nullptr));
  }
};

NjsRunner::NjsRunner() : impl_(std::make_unique<Impl>()) {}

NjsRunner::~NjsRunner() = default;

CandidateBatch NjsRunner::Run(const ExecContext& ctx,
                              const CandidateBatch& input,
                              const nlohmann::json& params) {
  // Load module path from params
  if (!params.contains("module")) {
    throw std::runtime_error("njs node requires 'module' param");
  }

  std::string module_path = params["module"].get<std::string>();

  // Read the module source
  std::ifstream file(module_path);
  if (!file.is_open()) {
    throw std::runtime_error("Failed to open njs module: " + module_path);
  }
  std::stringstream buffer;
  buffer << file.rdbuf();
  std::string source = buffer.str();

  if (input.RowCount() == 0) {
    return input;
  }

  // Create a fresh context for this execution
  JSContext* js_ctx_handle = JS_NewContext(impl_->rt);
  JS_SetContextOpaque(js_ctx_handle, &impl_->js_ctx);

  // Set up interrupt handler for instruction counting
  impl_->js_ctx.instruction_count = 0;
  impl_->js_ctx.max_instructions = 1000000;  // 1M instructions default
  impl_->js_ctx.interrupted = false;
  JS_SetInterruptHandler(impl_->rt, JsInterruptHandler, &impl_->js_ctx);

  // Inject Keys global from registry
  JSValue global = JS_GetGlobalObject(js_ctx_handle);
  JSValue keys_obj = JS_NewObject(js_ctx_handle);
  JSValue key_info_obj = JS_NewObject(js_ctx_handle);

  if (ctx.registry) {
    for (const auto& key_entry : ctx.registry->AllKeys()) {
      // Convert name to constant format: "score.base" -> "SCORE_BASE"
      std::string const_name = key_entry.name;
      for (char& c : const_name) {
        if (c == '.') c = '_';
        else c = std::toupper(c);
      }

      // Keys.SCORE_BASE = 3001
      JS_SetPropertyStr(js_ctx_handle, keys_obj, const_name.c_str(),
                        JS_NewInt32(js_ctx_handle, key_entry.id));

      // KeyInfo.SCORE_BASE = { id: 3001, name: "score.base", type: "f32" }
      JSValue info = JS_NewObject(js_ctx_handle);
      JS_SetPropertyStr(js_ctx_handle, info, "id", JS_NewInt32(js_ctx_handle, key_entry.id));
      JS_SetPropertyStr(js_ctx_handle, info, "name",
                        JS_NewString(js_ctx_handle, key_entry.name.c_str()));
      std::string type_str(KeyTypeToString(key_entry.type));
      JS_SetPropertyStr(js_ctx_handle, info, "type",
                        JS_NewString(js_ctx_handle, type_str.c_str()));
      JS_SetPropertyStr(js_ctx_handle, key_info_obj, const_name.c_str(), info);
    }
  }

  JS_SetPropertyStr(js_ctx_handle, global, "Keys", keys_obj);
  JS_SetPropertyStr(js_ctx_handle, global, "KeyInfo", key_info_obj);
  JS_FreeValue(js_ctx_handle, global);

  // Wrap module in function to get exports
  std::string wrapped = R"(
    (function() {
      var exports = {};
      var module = { exports: exports };
      )" + source + R"(
      return module.exports.meta ? module.exports : exports;
    })()
  )";

  // Evaluate the module
  JSValue module_val = JS_Eval(js_ctx_handle, wrapped.c_str(), wrapped.length(),
                               module_path.c_str(), JS_EVAL_TYPE_GLOBAL);

  if (JS_IsException(module_val)) {
    JSValue exc = JS_GetException(js_ctx_handle);
    std::string error = JsGetString(js_ctx_handle, exc);
    JS_FreeValue(js_ctx_handle, exc);
    JS_FreeContext(js_ctx_handle);
    throw std::runtime_error("njs module evaluation failed: " + error);
  }

  // Extract meta
  JSValue meta_val = JS_GetPropertyStr(js_ctx_handle, module_val, "meta");
  if (JS_IsUndefined(meta_val)) {
    JS_FreeValue(js_ctx_handle, module_val);
    JS_FreeContext(js_ctx_handle);
    throw std::runtime_error("njs module missing 'meta' export");
  }

  nlohmann::json meta_json = JsToJson(js_ctx_handle, meta_val);
  JS_FreeValue(js_ctx_handle, meta_val);
  NjsMeta meta = NjsMeta::Parse(meta_json);

  // Extract runBatch
  JSValue run_batch_val = JS_GetPropertyStr(js_ctx_handle, module_val, "runBatch");
  if (!JS_IsFunction(js_ctx_handle, run_batch_val)) {
    JS_FreeValue(js_ctx_handle, run_batch_val);
    JS_FreeValue(js_ctx_handle, module_val);
    JS_FreeContext(js_ctx_handle);
    throw std::runtime_error("njs module missing 'runBatch' function");
  }

  // Create builder for COW semantics
  BatchBuilder builder(input);

  // Create budget tracker
  NjsBudget budget = meta.budget;

  // Create batch context
  BatchContext batch_ctx(input, builder, ctx.registry, meta.writes, budget);

  // Set up JS context
  impl_->js_ctx.batch_ctx = &batch_ctx;
  impl_->js_ctx.params = &params;
  impl_->js_ctx.registry = ctx.registry;
  impl_->js_ctx.tracked_writes.clear();

  // Initialize IO context (default: disabled)
  impl_->js_ctx.io_enabled = false;
  impl_->js_ctx.csv_assets_dir = "";
  impl_->js_ctx.budget = &budget;

  // Check if module requests IO capability
  bool io_requested = meta.capabilities.io.csv_read;
  bool io_allowed = false;

  if (io_requested) {
    // Check policy (default deny if no policy set)
    if (policy_ && policy_->IsIoCsvReadAllowed(meta.name, meta.version)) {
      io_allowed = true;
      impl_->js_ctx.io_enabled = true;
      impl_->js_ctx.csv_assets_dir = policy_->CsvAssetsDir();
    }
  }

  // Create ctx.batch object
  JSValue ctx_obj = JS_NewObject(js_ctx_handle);
  JSValue batch_obj = JS_NewObject(js_ctx_handle);
  impl_->SetupBatchAPI(js_ctx_handle, batch_obj);
  JS_SetPropertyStr(js_ctx_handle, ctx_obj, "batch", batch_obj);

  // Create ctx.io object if IO is allowed
  if (io_allowed) {
    JSValue io_obj = JS_NewObject(js_ctx_handle);
    impl_->SetupIoAPI(js_ctx_handle, io_obj);
    JS_SetPropertyStr(js_ctx_handle, ctx_obj, "io", io_obj);
  }

  // Create objs array (for row-level API compatibility)
  JSValue objs_arr = JS_NewArray(js_ctx_handle);

  // Create params object
  JSValue params_js = JsonToJs(js_ctx_handle, params);

  // Call runBatch(objs, ctx, params)
  JSValue args[3] = { objs_arr, ctx_obj, params_js };
  JSValue result = JS_Call(js_ctx_handle, run_batch_val, JS_UNDEFINED, 3, args);

  // Check for interrupt
  if (impl_->js_ctx.interrupted) {
    JS_FreeValue(js_ctx_handle, result);
    JS_FreeValue(js_ctx_handle, args[0]);
    JS_FreeValue(js_ctx_handle, args[1]);
    JS_FreeValue(js_ctx_handle, args[2]);
    JS_FreeValue(js_ctx_handle, run_batch_val);
    JS_FreeValue(js_ctx_handle, module_val);
    JS_FreeContext(js_ctx_handle);
    throw std::runtime_error("njs execution exceeded instruction limit");
  }

  if (JS_IsException(result)) {
    JSValue exc = JS_GetException(js_ctx_handle);
    std::string error = JsGetString(js_ctx_handle, exc);
    JS_FreeValue(js_ctx_handle, exc);
    JS_FreeValue(js_ctx_handle, args[0]);
    JS_FreeValue(js_ctx_handle, args[1]);
    JS_FreeValue(js_ctx_handle, args[2]);
    JS_FreeValue(js_ctx_handle, run_batch_val);
    JS_FreeValue(js_ctx_handle, module_val);
    JS_FreeContext(js_ctx_handle);
    throw std::runtime_error("njs runBatch failed: " + error);
  }

  // Commit tracked write arrays from JS to C++ storage
  CommitTrackedWrites(js_ctx_handle, &impl_->js_ctx);

  // Commit batch context if column writes were used
  if (batch_ctx.HasColumnWrites()) {
    batch_ctx.Commit();
  }

  // Cleanup
  JS_FreeValue(js_ctx_handle, result);
  JS_FreeValue(js_ctx_handle, args[0]);
  JS_FreeValue(js_ctx_handle, args[1]);
  JS_FreeValue(js_ctx_handle, args[2]);
  JS_FreeValue(js_ctx_handle, run_batch_val);
  JS_FreeValue(js_ctx_handle, module_val);
  JS_FreeContext(js_ctx_handle);

  return builder.Build();
}

CandidateBatch NjsRunner::RunWithMeta(
    const ExecContext& ctx,
    const CandidateBatch& input,
    const nlohmann::json& params,
    const NjsMeta& meta,
    std::function<void(BatchContext&, const nlohmann::json&)> column_fn) {

  if (input.RowCount() == 0) {
    return input;
  }

  // Create builder for COW semantics
  BatchBuilder builder(input);

  // Create budget tracker (copy from meta)
  NjsBudget budget = meta.budget;

  // Create batch context with enforcement
  BatchContext batch_ctx(input, builder, ctx.registry, meta.writes, budget);

  // Execute the column-level function
  column_fn(batch_ctx, params);

  // If column writers were used, commit them
  if (batch_ctx.HasColumnWrites()) {
    batch_ctx.Commit();
  }

  // Build and return the result
  return builder.Build();
}

// Register the node runner
REGISTER_NODE_RUNNER("njs", NjsRunner);

}  // namespace ranking_dsl
