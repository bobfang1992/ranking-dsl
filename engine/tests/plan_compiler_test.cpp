#include <catch2/catch_test_macros.hpp>

#include <nlohmann/json.hpp>

#include "plan/plan.h"
#include "plan/compiler.h"
#include "keys/registry.h"

using namespace ranking_dsl;
using json = nlohmann::json;

TEST_CASE("Plan parsing", "[plan]") {
  SECTION("Parse minimal plan") {
    auto j = json::parse(R"({
      "name": "test",
      "version": 1,
      "nodes": []
    })");

    Plan plan;
    std::string error;
    REQUIRE(ParsePlan(j, plan, &error));
    REQUIRE(plan.name == "test");
    REQUIRE(plan.version == 1);
    REQUIRE(plan.nodes.empty());
  }

  SECTION("Parse plan with nodes") {
    auto j = json::parse(R"({
      "name": "test",
      "nodes": [
        {"id": "source", "op": "core:sourcer", "inputs": [], "params": {"k": 100}},
        {"id": "score", "op": "core:score_formula", "inputs": ["source"], "params": {}}
      ]
    })");

    Plan plan;
    REQUIRE(ParsePlan(j, plan));
    REQUIRE(plan.nodes.size() == 2);
    REQUIRE(plan.nodes[0].id == "source");
    REQUIRE(plan.nodes[0].op == "core:sourcer");
    REQUIRE(plan.nodes[1].inputs.size() == 1);
    REQUIRE(plan.nodes[1].inputs[0] == "source");
  }

  SECTION("Parse plan with logging config") {
    auto j = json::parse(R"({
      "name": "test",
      "nodes": [],
      "logging": {
        "sample_rate": 0.1,
        "dump_keys": [3001, 3002]
      }
    })");

    Plan plan;
    REQUIRE(ParsePlan(j, plan));
    REQUIRE(plan.logging.sample_rate == 0.1f);
    REQUIRE(plan.logging.dump_keys.size() == 2);
  }
}

TEST_CASE("Plan compilation", "[plan]") {
  KeyRegistry registry;
  registry.LoadFromCompiled();
  PlanCompiler compiler(registry);

  SECTION("Compile empty plan") {
    Plan plan;
    plan.name = "empty";

    CompiledPlan compiled;
    REQUIRE(compiler.Compile(plan, compiled));
    REQUIRE(compiled.topo_order.empty());
  }

  SECTION("Compile simple pipeline") {
    auto j = json::parse(R"({
      "name": "simple",
      "nodes": [
        {"id": "source", "op": "core:sourcer", "inputs": [], "params": {}},
        {"id": "score", "op": "core:score_formula", "inputs": ["source"], "params": {}}
      ]
    })");

    Plan plan;
    REQUIRE(ParsePlan(j, plan));

    CompiledPlan compiled;
    REQUIRE(compiler.Compile(plan, compiled));

    // Topological order: source before score
    REQUIRE(compiled.topo_order.size() == 2);
    REQUIRE(compiled.topo_order[0] == "source");
    REQUIRE(compiled.topo_order[1] == "score");
  }

  SECTION("Detect duplicate node IDs") {
    auto j = json::parse(R"({
      "name": "dup",
      "nodes": [
        {"id": "node1", "op": "core:sourcer", "inputs": [], "params": {}},
        {"id": "node1", "op": "core:sourcer", "inputs": [], "params": {}}
      ]
    })");

    Plan plan;
    REQUIRE(ParsePlan(j, plan));

    CompiledPlan compiled;
    std::string error;
    REQUIRE_FALSE(compiler.Compile(plan, compiled, &error));
    REQUIRE(error.find("Duplicate") != std::string::npos);
  }

  SECTION("Detect cycle") {
    auto j = json::parse(R"({
      "name": "cycle",
      "nodes": [
        {"id": "a", "op": "core:sourcer", "inputs": ["b"], "params": {}},
        {"id": "b", "op": "core:sourcer", "inputs": ["a"], "params": {}}
      ]
    })");

    Plan plan;
    REQUIRE(ParsePlan(j, plan));

    CompiledPlan compiled;
    std::string error;
    REQUIRE_FALSE(compiler.Compile(plan, compiled, &error));
    REQUIRE(error.find("cycle") != std::string::npos);
  }

  SECTION("Detect unknown op") {
    auto j = json::parse(R"({
      "name": "unknown",
      "nodes": [
        {"id": "node1", "op": "unknown:op", "inputs": [], "params": {}}
      ]
    })");

    Plan plan;
    REQUIRE(ParsePlan(j, plan));

    CompiledPlan compiled;
    std::string error;
    REQUIRE_FALSE(compiler.Compile(plan, compiled, &error));
    REQUIRE(error.find("Unknown op") != std::string::npos);
  }
}
