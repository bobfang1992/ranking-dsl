#include <catch2/catch_test_macros.hpp>
#include <catch2/matchers/catch_matchers_string.hpp>

#include "plan/plan.h"
#include "plan/compiler.h"
#include "plan/complexity.h"
#include "keys/registry.h"

using namespace ranking_dsl;

// Helper to create a simple linear plan with N nodes
static Plan CreateLinearPlan(int n) {
  Plan plan;
  plan.name = "linear_plan";
  plan.version = 1;

  for (int i = 0; i < n; ++i) {
    PlanNode node;
    node.id = "n" + std::to_string(i);
    node.op = "core:features";
    if (i > 0) {
      node.inputs = {"n" + std::to_string(i - 1)};
    }
    plan.nodes.push_back(node);
  }

  return plan;
}

// Helper to create a fan-out plan (1 source -> N dependents)
static Plan CreateFanoutPlan(int fanout) {
  Plan plan;
  plan.name = "fanout_plan";
  plan.version = 1;

  // Root node
  PlanNode root;
  root.id = "root";
  root.op = "core:sourcer";
  plan.nodes.push_back(root);

  // Fan-out nodes
  for (int i = 0; i < fanout; ++i) {
    PlanNode node;
    node.id = "child" + std::to_string(i);
    node.op = "core:features";
    node.inputs = {"root"};
    plan.nodes.push_back(node);
  }

  return plan;
}

// Helper to create a fan-in plan (N sources -> 1 merger)
static Plan CreateFaninPlan(int fanin) {
  Plan plan;
  plan.name = "fanin_plan";
  plan.version = 1;

  // Fan-in sources
  for (int i = 0; i < fanin; ++i) {
    PlanNode node;
    node.id = "src" + std::to_string(i);
    node.op = "core:sourcer";
    plan.nodes.push_back(node);
  }

  // Merger node
  PlanNode merger;
  merger.id = "merger";
  merger.op = "core:merge";
  for (int i = 0; i < fanin; ++i) {
    merger.inputs.push_back("src" + std::to_string(i));
  }
  plan.nodes.push_back(merger);

  return plan;
}

TEST_CASE("Complexity metrics computation", "[complexity][metrics]") {
  SECTION("Empty plan") {
    Plan plan;
    auto metrics = ComputeComplexityMetrics(plan);

    REQUIRE(metrics.node_count == 0);
    REQUIRE(metrics.edge_count == 0);
    REQUIRE(metrics.max_depth == 0);
    REQUIRE(metrics.fanout_peak == 0);
    REQUIRE(metrics.fanin_peak == 0);
  }

  SECTION("Single node plan") {
    Plan plan;
    PlanNode node;
    node.id = "n1";
    node.op = "core:sourcer";
    plan.nodes.push_back(node);

    auto metrics = ComputeComplexityMetrics(plan);

    REQUIRE(metrics.node_count == 1);
    REQUIRE(metrics.edge_count == 0);
    REQUIRE(metrics.max_depth == 1);
    REQUIRE(metrics.fanout_peak == 0);
    REQUIRE(metrics.fanin_peak == 0);
  }

  SECTION("Linear plan metrics") {
    auto plan = CreateLinearPlan(5);
    auto metrics = ComputeComplexityMetrics(plan);

    REQUIRE(metrics.node_count == 5);
    REQUIRE(metrics.edge_count == 4);
    REQUIRE(metrics.max_depth == 5);
    REQUIRE(metrics.fanout_peak == 1);
    REQUIRE(metrics.fanin_peak == 1);
    REQUIRE(metrics.longest_path.size() == 5);
    REQUIRE(metrics.longest_path.front() == "n0");
    REQUIRE(metrics.longest_path.back() == "n4");
  }

  SECTION("Fan-out plan metrics") {
    auto plan = CreateFanoutPlan(10);
    auto metrics = ComputeComplexityMetrics(plan);

    REQUIRE(metrics.node_count == 11);  // 1 root + 10 children
    REQUIRE(metrics.edge_count == 10);
    REQUIRE(metrics.max_depth == 2);
    REQUIRE(metrics.fanout_peak == 10);
    REQUIRE(metrics.fanin_peak == 1);
    REQUIRE(metrics.top_fanout.size() == 5);  // top-K
    REQUIRE(metrics.top_fanout[0].id == "root");
    REQUIRE(metrics.top_fanout[0].degree == 10);
  }

  SECTION("Fan-in plan metrics") {
    auto plan = CreateFaninPlan(8);
    auto metrics = ComputeComplexityMetrics(plan);

    REQUIRE(metrics.node_count == 9);  // 8 sources + 1 merger
    REQUIRE(metrics.edge_count == 8);
    REQUIRE(metrics.max_depth == 2);
    REQUIRE(metrics.fanout_peak == 1);
    REQUIRE(metrics.fanin_peak == 8);
    REQUIRE(metrics.top_fanin.size() == 5);
    REQUIRE(metrics.top_fanin[0].id == "merger");
    REQUIRE(metrics.top_fanin[0].degree == 8);
  }
}

TEST_CASE("Complexity score computation", "[complexity][score]") {
  Plan plan = CreateLinearPlan(10);
  auto metrics = ComputeComplexityMetrics(plan);

  // Default weights: N*1 + D*5 + F_out*2 + F_in*2 + E*0.5
  // 10*1 + 10*5 + 1*2 + 1*2 + 9*0.5 = 10 + 50 + 2 + 2 + 4.5 = 68
  int64_t score = ComputeComplexityScore(metrics);
  REQUIRE(score == 68);
}

TEST_CASE("Complexity budget parsing", "[complexity][budget]") {
  SECTION("Parse from JSON string") {
    std::string json = R"({
      "hard": { "node_count": 100, "max_depth": 50, "fanout_peak": 8, "fanin_peak": 8 },
      "soft": { "edge_count": 500, "complexity_score": 1000 }
    })";

    std::string error;
    auto budget = ComplexityBudget::Parse(json, &error);

    REQUIRE(error.empty());
    REQUIRE(budget.node_count_hard == 100);
    REQUIRE(budget.max_depth_hard == 50);
    REQUIRE(budget.fanout_peak_hard == 8);
    REQUIRE(budget.fanin_peak_hard == 8);
    REQUIRE(budget.edge_count_soft == 500);
    REQUIRE(budget.complexity_score_soft == 1000);
  }

  SECTION("Default budget values") {
    auto budget = ComplexityBudget::Default();

    REQUIRE(budget.node_count_hard == 2000);
    REQUIRE(budget.max_depth_hard == 120);
    REQUIRE(budget.fanout_peak_hard == 16);
    REQUIRE(budget.fanin_peak_hard == 16);
    REQUIRE(budget.edge_count_soft == 10000);
    REQUIRE(budget.complexity_score_soft == 8000);
  }

  SECTION("Parse custom score_weights from JSON") {
    std::string json = R"({
      "hard": { "node_count": 100 },
      "soft": { "complexity_score": 500 },
      "score_weights": {
        "node_count": 10.0,
        "max_depth": 20.0,
        "fanout_peak": 5.0,
        "fanin_peak": 5.0,
        "edge_count": 1.0
      }
    })";

    std::string error;
    auto budget = ComplexityBudget::Parse(json, &error);

    REQUIRE(error.empty());
    REQUIRE(budget.score_weights.node_count == 10.0);
    REQUIRE(budget.score_weights.max_depth == 20.0);
    REQUIRE(budget.score_weights.fanout_peak == 5.0);
    REQUIRE(budget.score_weights.fanin_peak == 5.0);
    REQUIRE(budget.score_weights.edge_count == 1.0);
  }
}

TEST_CASE("Complexity budget enforcement", "[complexity][enforcement]") {
  SECTION("Plan within budget passes") {
    auto plan = CreateLinearPlan(5);
    auto metrics = ComputeComplexityMetrics(plan);
    auto budget = ComplexityBudget::Default();

    auto result = CheckComplexityBudget(metrics, budget);

    REQUIRE(result.passed);
    REQUIRE(result.error_code.empty());
  }

  SECTION("Node count exceeds hard limit") {
    auto plan = CreateLinearPlan(10);
    auto metrics = ComputeComplexityMetrics(plan);

    ComplexityBudget budget;
    budget.node_count_hard = 5;  // Plan has 10 nodes

    auto result = CheckComplexityBudget(metrics, budget);

    REQUIRE_FALSE(result.passed);
    REQUIRE(result.error_code == "PLAN_TOO_COMPLEX");
    REQUIRE_THAT(result.diagnostics, Catch::Matchers::ContainsSubstring("node_count=10"));
    REQUIRE_THAT(result.diagnostics, Catch::Matchers::ContainsSubstring("hard_limit=5"));
  }

  SECTION("Max depth exceeds hard limit") {
    auto plan = CreateLinearPlan(20);
    auto metrics = ComputeComplexityMetrics(plan);

    ComplexityBudget budget;
    budget.max_depth_hard = 10;  // Plan has depth 20

    auto result = CheckComplexityBudget(metrics, budget);

    REQUIRE_FALSE(result.passed);
    REQUIRE(result.error_code == "PLAN_TOO_COMPLEX");
    REQUIRE_THAT(result.diagnostics, Catch::Matchers::ContainsSubstring("max_depth=20"));
    REQUIRE_THAT(result.diagnostics, Catch::Matchers::ContainsSubstring("hard_limit=10"));
  }

  SECTION("Fanout exceeds hard limit") {
    auto plan = CreateFanoutPlan(20);
    auto metrics = ComputeComplexityMetrics(plan);

    ComplexityBudget budget;
    budget.fanout_peak_hard = 10;  // Plan has fanout 20

    auto result = CheckComplexityBudget(metrics, budget);

    REQUIRE_FALSE(result.passed);
    REQUIRE(result.error_code == "PLAN_TOO_COMPLEX");
    REQUIRE_THAT(result.diagnostics, Catch::Matchers::ContainsSubstring("fanout_peak=20"));
    REQUIRE_THAT(result.diagnostics, Catch::Matchers::ContainsSubstring("hard_limit=10"));
  }

  SECTION("Fanin exceeds hard limit") {
    auto plan = CreateFaninPlan(20);
    auto metrics = ComputeComplexityMetrics(plan);

    ComplexityBudget budget;
    budget.fanin_peak_hard = 10;  // Plan has fanin 20

    auto result = CheckComplexityBudget(metrics, budget);

    REQUIRE_FALSE(result.passed);
    REQUIRE(result.error_code == "PLAN_TOO_COMPLEX");
    REQUIRE_THAT(result.diagnostics, Catch::Matchers::ContainsSubstring("fanin_peak=20"));
    REQUIRE_THAT(result.diagnostics, Catch::Matchers::ContainsSubstring("hard_limit=10"));
  }

  SECTION("Custom score_weights affect complexity_score check") {
    // Linear plan with 5 nodes: N=5, D=5, E=4, fanout=1, fanin=1
    auto plan = CreateLinearPlan(5);
    auto metrics = ComputeComplexityMetrics(plan);

    // With default weights: 5*1 + 5*5 + 1*2 + 1*2 + 4*0.5 = 5+25+2+2+2 = 36
    // This would pass a soft limit of 50

    // With high depth weight (100x): 5*1 + 5*100 + 1*2 + 1*2 + 4*0.5 = 5+500+2+2+2 = 511
    // This would FAIL a soft limit of 50

    ComplexityBudget budget;
    budget.complexity_score_soft = 50;
    budget.score_weights.max_depth = 100.0;  // Very high weight on depth

    auto result = CheckComplexityBudget(metrics, budget);

    // Should pass (no hard violations) but have warnings (soft limit exceeded)
    REQUIRE(result.passed);
    REQUIRE(result.has_warnings);
    REQUIRE_THAT(result.diagnostics, Catch::Matchers::Equals(""));  // No diagnostics for warnings-only
  }
}

TEST_CASE("Complexity diagnostics content", "[complexity][diagnostics]") {
  auto plan = CreateFanoutPlan(20);
  auto metrics = ComputeComplexityMetrics(plan);

  ComplexityBudget budget;
  budget.fanout_peak_hard = 10;

  auto result = CheckComplexityBudget(metrics, budget);

  REQUIRE_FALSE(result.passed);

  // Check diagnostics contain required elements
  REQUIRE_THAT(result.diagnostics, Catch::Matchers::ContainsSubstring("PLAN_TOO_COMPLEX"));
  REQUIRE_THAT(result.diagnostics, Catch::Matchers::ContainsSubstring("node_count="));
  REQUIRE_THAT(result.diagnostics, Catch::Matchers::ContainsSubstring("edge_count="));
  REQUIRE_THAT(result.diagnostics, Catch::Matchers::ContainsSubstring("max_depth="));
  REQUIRE_THAT(result.diagnostics, Catch::Matchers::ContainsSubstring("fanout_peak="));
  REQUIRE_THAT(result.diagnostics, Catch::Matchers::ContainsSubstring("fanin_peak="));

  // Top offenders
  REQUIRE_THAT(result.diagnostics, Catch::Matchers::ContainsSubstring("Top fanout nodes:"));
  REQUIRE_THAT(result.diagnostics, Catch::Matchers::ContainsSubstring("root core:sourcer fanout=20"));

  // Longest path
  REQUIRE_THAT(result.diagnostics, Catch::Matchers::ContainsSubstring("Longest path"));

  // Remediation hint
  REQUIRE_THAT(result.diagnostics, Catch::Matchers::ContainsSubstring("Hint:"));
  REQUIRE_THAT(result.diagnostics, Catch::Matchers::ContainsSubstring("njs module"));
  REQUIRE_THAT(result.diagnostics, Catch::Matchers::ContainsSubstring("core C++ node"));
  REQUIRE_THAT(result.diagnostics, Catch::Matchers::ContainsSubstring("complexity-governance.md"));
}

TEST_CASE("PlanCompiler complexity enforcement", "[complexity][compiler]") {
  KeyRegistry registry;
  registry.LoadFromCompiled();

  SECTION("Compile fails when complexity exceeded") {
    auto plan = CreateFanoutPlan(20);

    ComplexityBudget budget;
    budget.fanout_peak_hard = 10;

    PlanCompiler compiler(registry);
    compiler.SetComplexityBudget(budget);

    CompiledPlan out;
    std::string error;
    bool success = compiler.Compile(plan, out, &error);

    REQUIRE_FALSE(success);
    REQUIRE_THAT(error, Catch::Matchers::ContainsSubstring("PLAN_TOO_COMPLEX"));
    REQUIRE_THAT(error, Catch::Matchers::ContainsSubstring("fanout_peak=20"));
  }

  SECTION("Compile succeeds when within budget") {
    auto plan = CreateFanoutPlan(5);

    ComplexityBudget budget;
    budget.fanout_peak_hard = 10;

    PlanCompiler compiler(registry);
    compiler.SetComplexityBudget(budget);

    CompiledPlan out;
    std::string error;
    bool success = compiler.Compile(plan, out, &error);

    REQUIRE(success);
    REQUIRE(out.complexity.node_count == 6);
    REQUIRE(out.complexity.fanout_peak == 5);
  }

  SECTION("Complexity check can be disabled") {
    auto plan = CreateFanoutPlan(100);  // Would fail default budget

    PlanCompiler compiler(registry);
    compiler.DisableComplexityCheck();

    CompiledPlan out;
    std::string error;
    bool success = compiler.Compile(plan, out, &error);

    REQUIRE(success);
    // Metrics still computed
    REQUIRE(out.complexity.node_count == 101);
    REQUIRE(out.complexity.fanout_peak == 100);
  }

  SECTION("Default budget is applied if not set") {
    // Default fanout_peak_hard = 16
    auto plan = CreateFanoutPlan(20);

    PlanCompiler compiler(registry);
    // Don't set budget - should use default

    CompiledPlan out;
    std::string error;
    bool success = compiler.Compile(plan, out, &error);

    REQUIRE_FALSE(success);
    REQUIRE_THAT(error, Catch::Matchers::ContainsSubstring("fanout_peak=20"));
    REQUIRE_THAT(error, Catch::Matchers::ContainsSubstring("hard_limit=16"));
  }
}

TEST_CASE("Cross-check fixture plan metrics", "[complexity][cross-check]") {
  // This test verifies the C++ metrics match the TypeScript metrics
  // for the same fixture plan: test-fixtures/complexity-fixture.plan.json

  // Create the exact same plan structure as the fixture
  Plan plan;
  plan.name = "complexity_fixture";
  plan.version = 1;

  // sourcer
  {
    PlanNode node;
    node.id = "sourcer";
    node.op = "core:sourcer";
    plan.nodes.push_back(node);
  }
  // feat1, feat2, feat3 (all depend on sourcer)
  for (int i = 1; i <= 3; ++i) {
    PlanNode node;
    node.id = "feat" + std::to_string(i);
    node.op = "core:features";
    node.inputs = {"sourcer"};
    plan.nodes.push_back(node);
  }
  // model1 (depends on feat1)
  {
    PlanNode node;
    node.id = "model1";
    node.op = "core:model";
    node.inputs = {"feat1"};
    plan.nodes.push_back(node);
  }
  // model2 (depends on feat2)
  {
    PlanNode node;
    node.id = "model2";
    node.op = "core:model";
    node.inputs = {"feat2"};
    plan.nodes.push_back(node);
  }
  // merge (depends on model1, model2, feat3)
  {
    PlanNode node;
    node.id = "merge";
    node.op = "core:merge";
    node.inputs = {"model1", "model2", "feat3"};
    plan.nodes.push_back(node);
  }
  // final (depends on merge)
  {
    PlanNode node;
    node.id = "final";
    node.op = "core:score_formula";
    node.inputs = {"merge"};
    plan.nodes.push_back(node);
  }

  auto metrics = ComputeComplexityMetrics(plan);

  // These values MUST match TypeScript test:
  // - node_count: 8 nodes total
  // - edge_count: 9 edges
  // - max_depth: 5
  // - fanout_peak: 3 (sourcer has 3 outgoing edges)
  // - fanin_peak: 3 (merge has 3 incoming edges)
  REQUIRE(metrics.node_count == 8);
  REQUIRE(metrics.edge_count == 9);
  REQUIRE(metrics.max_depth == 5);
  REQUIRE(metrics.fanout_peak == 3);
  REQUIRE(metrics.fanin_peak == 3);

  // Verify longest path
  REQUIRE(metrics.longest_path.size() == 5);
  REQUIRE(metrics.longest_path.front() == "sourcer");
  REQUIRE(metrics.longest_path.back() == "final");

  // Verify top fanout
  REQUIRE(metrics.top_fanout.size() >= 1);
  REQUIRE(metrics.top_fanout[0].id == "sourcer");
  REQUIRE(metrics.top_fanout[0].degree == 3);

  // Verify top fanin
  REQUIRE(metrics.top_fanin.size() >= 1);
  REQUIRE(metrics.top_fanin[0].id == "merge");
  REQUIRE(metrics.top_fanin[0].degree == 3);

  // Compute score with default weights: N*1 + D*5 + F_out*2 + F_in*2 + E*0.5
  // 8*1 + 5*5 + 3*2 + 3*2 + 9*0.5 = 8 + 25 + 6 + 6 + 4.5 = 49 (floored)
  int64_t score = ComputeComplexityScore(metrics);
  REQUIRE(score == 49);
}
