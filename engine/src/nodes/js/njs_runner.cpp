#include "nodes/js/njs_runner.h"

#include <fstream>
#include <sstream>
#include <stdexcept>

extern "C" {
#include "quickjs.h"
}

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
  }

  return meta;
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
};

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
    ctx = JS_NewContext(rt);
  }

  ~Impl() {
    if (ctx) JS_FreeContext(ctx);
    if (rt) JS_FreeRuntime(rt);
  }

  void SetupBatchAPI(JSContext* js_ctx, JSValueConst batch_obj) {
    JS_SetPropertyStr(js_ctx, batch_obj, "rowCount",
      JS_NewCFunctionData(js_ctx, JsBatchRowCount, 0, 0, 0, nullptr));
    JS_SetPropertyStr(js_ctx, batch_obj, "f32",
      JS_NewCFunctionData(js_ctx, JsBatchGetF32, 1, 0, 0, nullptr));
    JS_SetPropertyStr(js_ctx, batch_obj, "i64",
      JS_NewCFunctionData(js_ctx, JsBatchGetI64, 1, 0, 0, nullptr));
    JS_SetPropertyStr(js_ctx, batch_obj, "writeF32",
      JS_NewCFunctionData(js_ctx, JsBatchWriteF32, 1, 0, 0, nullptr));
    JS_SetPropertyStr(js_ctx, batch_obj, "writeI64",
      JS_NewCFunctionData(js_ctx, JsBatchWriteI64, 1, 0, 0, nullptr));
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

  // Create ctx.batch object
  JSValue ctx_obj = JS_NewObject(js_ctx_handle);
  JSValue batch_obj = JS_NewObject(js_ctx_handle);
  impl_->SetupBatchAPI(js_ctx_handle, batch_obj);
  JS_SetPropertyStr(js_ctx_handle, ctx_obj, "batch", batch_obj);

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
