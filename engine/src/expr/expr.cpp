#include "expr/expr.h"

#include <algorithm>
#include <cmath>
#include <numeric>
#include <stdexcept>

#include <nlohmann/json.hpp>

#include "keys/registry.h"

namespace ranking_dsl {

namespace {

// Compute cosine similarity between two vectors
// Returns 0 if either vector is empty or has zero norm
// Clamps result to [-1, 1]
float CosineSimilarity(const std::vector<float>& a, const std::vector<float>& b) {
  if (a.empty() || b.empty() || a.size() != b.size()) {
    return 0.0f;
  }

  float dot = 0.0f;
  float norm_a = 0.0f;
  float norm_b = 0.0f;

  for (size_t i = 0; i < a.size(); ++i) {
    dot += a[i] * b[i];
    norm_a += a[i] * a[i];
    norm_b += b[i] * b[i];
  }

  if (norm_a == 0.0f || norm_b == 0.0f) {
    return 0.0f;
  }

  float result = dot / (std::sqrt(norm_a) * std::sqrt(norm_b));

  // Clamp to [-1, 1] to avoid numerical drift
  return std::clamp(result, -1.0f, 1.0f);
}

// Get a float value from an Obj, defaulting to 0 if missing or wrong type
float GetFloatValue(const Obj& obj, int32_t key_id) {
  auto val = obj.Get(key_id);
  if (!val || IsNull(*val)) {
    return 0.0f;
  }
  if (auto* f = std::get_if<float>(&*val)) {
    return *f;
  }
  if (auto* i = std::get_if<int64_t>(&*val)) {
    return static_cast<float>(*i);
  }
  return 0.0f;
}

// Get a vector value from an Obj
std::vector<float> GetVectorValue(const Obj& obj, int32_t key_id) {
  auto val = obj.Get(key_id);
  if (!val || IsNull(*val)) {
    return {};
  }
  if (auto* vec = std::get_if<std::vector<float>>(&*val)) {
    return *vec;
  }
  return {};
}

// Get a float value from a ColumnBatch at a specific row
float GetFloatValueFromBatch(const ColumnBatch& batch, size_t row_index, int32_t key_id) {
  Value val = batch.GetValue(row_index, key_id);
  if (IsNull(val)) {
    return 0.0f;
  }
  if (auto* f = std::get_if<float>(&val)) {
    return *f;
  }
  if (auto* i = std::get_if<int64_t>(&val)) {
    return static_cast<float>(*i);
  }
  return 0.0f;
}

// Get a vector value from a ColumnBatch at a specific row
std::vector<float> GetVectorValueFromBatch(const ColumnBatch& batch, size_t row_index, int32_t key_id) {
  Value val = batch.GetValue(row_index, key_id);
  if (IsNull(val)) {
    return {};
  }
  if (auto* vec = std::get_if<std::vector<float>>(&val)) {
    return *vec;
  }
  return {};
}

}  // namespace

ExprNode ParseExpr(const nlohmann::json& json, std::string* error_out) {
  try {
    std::string op = json["op"].get<std::string>();

    if (op == "const") {
      return ConstExpr{json["value"].get<float>()};
    }

    if (op == "signal") {
      return SignalExpr{json["key_id"].get<int32_t>()};
    }

    if (op == "add") {
      auto add = std::make_unique<AddExpr>();
      for (const auto& arg : json["args"]) {
        add->args.push_back(ParseExpr(arg, error_out));
      }
      return add;
    }

    if (op == "mul") {
      auto mul = std::make_unique<MulExpr>();
      for (const auto& arg : json["args"]) {
        mul->args.push_back(ParseExpr(arg, error_out));
      }
      return mul;
    }

    if (op == "min") {
      auto min = std::make_unique<MinExpr>();
      for (const auto& arg : json["args"]) {
        min->args.push_back(ParseExpr(arg, error_out));
      }
      return min;
    }

    if (op == "max") {
      auto max = std::make_unique<MaxExpr>();
      for (const auto& arg : json["args"]) {
        max->args.push_back(ParseExpr(arg, error_out));
      }
      return max;
    }

    if (op == "cos") {
      auto cos = std::make_unique<CosExpr>();
      cos->a = ParseExpr(json["a"], error_out);
      cos->b = ParseExpr(json["b"], error_out);
      return cos;
    }

    if (op == "clamp") {
      auto clamp = std::make_unique<ClampExpr>();
      clamp->x = ParseExpr(json["x"], error_out);
      clamp->lo = ParseExpr(json["lo"], error_out);
      clamp->hi = ParseExpr(json["hi"], error_out);
      return clamp;
    }

    if (op == "penalty") {
      auto penalty = std::make_unique<PenaltyExpr>();
      penalty->name = json["name"].get<std::string>();
      return penalty;
    }

    if (error_out) {
      *error_out = "Unknown expression op: " + op;
    }
    return ConstExpr{0.0f};  // Default

  } catch (const std::exception& e) {
    if (error_out) {
      *error_out = std::string("Expression parse error: ") + e.what();
    }
    return ConstExpr{0.0f};
  }
}

float EvalExpr(const ExprNode& expr, const Obj& obj, const KeyRegistry* registry) {
  return std::visit(
      [&](auto&& node) -> float {
        using T = std::decay_t<decltype(node)>;

        if constexpr (std::is_same_v<T, ConstExpr>) {
          return node.value;
        }

        else if constexpr (std::is_same_v<T, SignalExpr>) {
          return GetFloatValue(obj, node.key_id);
        }

        else if constexpr (std::is_same_v<T, std::unique_ptr<AddExpr>>) {
          float sum = 0.0f;
          for (const auto& arg : node->args) {
            sum += EvalExpr(arg, obj, registry);
          }
          return sum;
        }

        else if constexpr (std::is_same_v<T, std::unique_ptr<MulExpr>>) {
          float product = 1.0f;
          for (const auto& arg : node->args) {
            product *= EvalExpr(arg, obj, registry);
          }
          return product;
        }

        else if constexpr (std::is_same_v<T, std::unique_ptr<MinExpr>>) {
          if (node->args.empty()) return 0.0f;
          float result = EvalExpr(node->args[0], obj, registry);
          for (size_t i = 1; i < node->args.size(); ++i) {
            result = std::min(result, EvalExpr(node->args[i], obj, registry));
          }
          return result;
        }

        else if constexpr (std::is_same_v<T, std::unique_ptr<MaxExpr>>) {
          if (node->args.empty()) return 0.0f;
          float result = EvalExpr(node->args[0], obj, registry);
          for (size_t i = 1; i < node->args.size(); ++i) {
            result = std::max(result, EvalExpr(node->args[i], obj, registry));
          }
          return result;
        }

        else if constexpr (std::is_same_v<T, std::unique_ptr<CosExpr>>) {
          // For cos, we need vector values, not float results
          // The operands should be signal expressions pointing to f32vec keys
          auto get_vec = [&](const ExprNode& e) -> std::vector<float> {
            if (auto* sig = std::get_if<SignalExpr>(&e)) {
              return GetVectorValue(obj, sig->key_id);
            }
            return {};
          };
          auto vec_a = get_vec(node->a);
          auto vec_b = get_vec(node->b);
          return CosineSimilarity(vec_a, vec_b);
        }

        else if constexpr (std::is_same_v<T, std::unique_ptr<ClampExpr>>) {
          float x = EvalExpr(node->x, obj, registry);
          float lo = EvalExpr(node->lo, obj, registry);
          float hi = EvalExpr(node->hi, obj, registry);
          return std::clamp(x, lo, hi);
        }

        else if constexpr (std::is_same_v<T, std::unique_ptr<PenaltyExpr>>) {
          // Look up penalty.{name} key
          if (registry) {
            std::string key_name = "penalty." + node->name;
            const auto* key_info = registry->GetByName(key_name);
            if (key_info) {
              return GetFloatValue(obj, key_info->id);
            }
          }
          return 0.0f;  // Default if not found
        }

        else {
          return 0.0f;
        }
      },
      expr);
}

std::vector<int32_t> CollectKeyIds(const ExprNode& expr) {
  std::vector<int32_t> result;

  std::visit(
      [&](auto&& node) {
        using T = std::decay_t<decltype(node)>;

        if constexpr (std::is_same_v<T, SignalExpr>) {
          result.push_back(node.key_id);
        }

        else if constexpr (std::is_same_v<T, std::unique_ptr<AddExpr>> ||
                          std::is_same_v<T, std::unique_ptr<MulExpr>> ||
                          std::is_same_v<T, std::unique_ptr<MinExpr>> ||
                          std::is_same_v<T, std::unique_ptr<MaxExpr>>) {
          for (const auto& arg : node->args) {
            auto sub = CollectKeyIds(arg);
            result.insert(result.end(), sub.begin(), sub.end());
          }
        }

        else if constexpr (std::is_same_v<T, std::unique_ptr<CosExpr>>) {
          auto sub_a = CollectKeyIds(node->a);
          auto sub_b = CollectKeyIds(node->b);
          result.insert(result.end(), sub_a.begin(), sub_a.end());
          result.insert(result.end(), sub_b.begin(), sub_b.end());
        }

        else if constexpr (std::is_same_v<T, std::unique_ptr<ClampExpr>>) {
          auto sub_x = CollectKeyIds(node->x);
          auto sub_lo = CollectKeyIds(node->lo);
          auto sub_hi = CollectKeyIds(node->hi);
          result.insert(result.end(), sub_x.begin(), sub_x.end());
          result.insert(result.end(), sub_lo.begin(), sub_lo.end());
          result.insert(result.end(), sub_hi.begin(), sub_hi.end());
        }

        // ConstExpr and PenaltyExpr don't reference key IDs directly
      },
      expr);

  return result;
}

float EvalExpr(const ExprNode& expr, const ColumnBatch& batch, size_t row_index,
               const KeyRegistry* registry) {
  return std::visit(
      [&](auto&& node) -> float {
        using T = std::decay_t<decltype(node)>;

        if constexpr (std::is_same_v<T, ConstExpr>) {
          return node.value;
        }

        else if constexpr (std::is_same_v<T, SignalExpr>) {
          return GetFloatValueFromBatch(batch, row_index, node.key_id);
        }

        else if constexpr (std::is_same_v<T, std::unique_ptr<AddExpr>>) {
          float sum = 0.0f;
          for (const auto& arg : node->args) {
            sum += EvalExpr(arg, batch, row_index, registry);
          }
          return sum;
        }

        else if constexpr (std::is_same_v<T, std::unique_ptr<MulExpr>>) {
          float product = 1.0f;
          for (const auto& arg : node->args) {
            product *= EvalExpr(arg, batch, row_index, registry);
          }
          return product;
        }

        else if constexpr (std::is_same_v<T, std::unique_ptr<MinExpr>>) {
          if (node->args.empty()) return 0.0f;
          float result = EvalExpr(node->args[0], batch, row_index, registry);
          for (size_t i = 1; i < node->args.size(); ++i) {
            result = std::min(result, EvalExpr(node->args[i], batch, row_index, registry));
          }
          return result;
        }

        else if constexpr (std::is_same_v<T, std::unique_ptr<MaxExpr>>) {
          if (node->args.empty()) return 0.0f;
          float result = EvalExpr(node->args[0], batch, row_index, registry);
          for (size_t i = 1; i < node->args.size(); ++i) {
            result = std::max(result, EvalExpr(node->args[i], batch, row_index, registry));
          }
          return result;
        }

        else if constexpr (std::is_same_v<T, std::unique_ptr<CosExpr>>) {
          // For cos, we need vector values
          auto get_vec = [&](const ExprNode& e) -> std::vector<float> {
            if (auto* sig = std::get_if<SignalExpr>(&e)) {
              return GetVectorValueFromBatch(batch, row_index, sig->key_id);
            }
            return {};
          };
          auto vec_a = get_vec(node->a);
          auto vec_b = get_vec(node->b);
          return CosineSimilarity(vec_a, vec_b);
        }

        else if constexpr (std::is_same_v<T, std::unique_ptr<ClampExpr>>) {
          float x = EvalExpr(node->x, batch, row_index, registry);
          float lo = EvalExpr(node->lo, batch, row_index, registry);
          float hi = EvalExpr(node->hi, batch, row_index, registry);
          return std::clamp(x, lo, hi);
        }

        else if constexpr (std::is_same_v<T, std::unique_ptr<PenaltyExpr>>) {
          // Look up penalty.{name} key
          if (registry) {
            std::string key_name = "penalty." + node->name;
            const auto* key_info = registry->GetByName(key_name);
            if (key_info) {
              return GetFloatValueFromBatch(batch, row_index, key_info->id);
            }
          }
          return 0.0f;  // Default if not found
        }

        else {
          return 0.0f;
        }
      },
      expr);
}

}  // namespace ranking_dsl
