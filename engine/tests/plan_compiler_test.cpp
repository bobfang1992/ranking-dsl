#include <catch2/catch_test_macros.hpp>

#include <nlohmann/json.hpp>

#include "plan/plan.h"
#include "plan/compiler.h"
#include "keys/registry.h"
#include "logging/trace.h"

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

TEST_CASE("trace_key parsing and validation", "[plan][trace]") {
  SECTION("Parse plan with trace_key") {
    auto j = json::parse(R"({
      "name": "test",
      "nodes": [
        {"id": "source", "op": "core:sourcer", "inputs": [], "params": {}, "trace_key": "src/main"},
        {"id": "score", "op": "core:score_formula", "inputs": ["source"], "params": {}, "trace_key": "scorer.v1"}
      ]
    })");

    Plan plan;
    std::string error;
    REQUIRE(ParsePlan(j, plan, &error));
    REQUIRE(plan.nodes.size() == 2);
    REQUIRE(plan.nodes[0].trace_key == "src/main");
    REQUIRE(plan.nodes[1].trace_key == "scorer.v1");
  }

  SECTION("trace_key is optional") {
    auto j = json::parse(R"({
      "name": "test",
      "nodes": [
        {"id": "source", "op": "core:sourcer", "inputs": [], "params": {}},
        {"id": "score", "op": "core:score_formula", "inputs": ["source"], "params": {}, "trace_key": "scorer"}
      ]
    })");

    Plan plan;
    REQUIRE(ParsePlan(j, plan));
    REQUIRE(plan.nodes[0].trace_key.empty());
    REQUIRE(plan.nodes[1].trace_key == "scorer");
  }

  SECTION("ValidateTraceKey accepts valid keys") {
    REQUIRE(ValidateTraceKey("").empty());  // Empty is valid (not set)
    REQUIRE(ValidateTraceKey("score").empty());
    REQUIRE(ValidateTraceKey("Score_Base.v1/model-2").empty());
    REQUIRE(ValidateTraceKey(std::string(64, 'a')).empty());
  }

  SECTION("ValidateTraceKey rejects too long keys") {
    std::string error = ValidateTraceKey(std::string(65, 'a'));
    REQUIRE(error.find("at most 64") != std::string::npos);
    REQUIRE(error.find("got 65") != std::string::npos);
  }

  SECTION("ValidateTraceKey rejects invalid characters") {
    REQUIRE(ValidateTraceKey("score base").find("[A-Za-z0-9._/-]") != std::string::npos);
    REQUIRE(ValidateTraceKey("score@base").find("[A-Za-z0-9._/-]") != std::string::npos);
    REQUIRE(ValidateTraceKey("score:base").find("[A-Za-z0-9._/-]") != std::string::npos);
  }

  SECTION("Plan parsing rejects invalid trace_key") {
    auto j = json::parse(R"({
      "name": "test",
      "nodes": [
        {"id": "source", "op": "core:sourcer", "inputs": [], "params": {}, "trace_key": "invalid key with spaces"}
      ]
    })");

    Plan plan;
    std::string error;
    REQUIRE_FALSE(ParsePlan(j, plan, &error));
    REQUIRE(error.find("source") != std::string::npos);
    REQUIRE(error.find("[A-Za-z0-9._/-]") != std::string::npos);
  }

  SECTION("Plan parsing rejects trace_key exceeding length") {
    auto j = json::parse(R"({
      "name": "test",
      "nodes": [
        {"id": "source", "op": "core:sourcer", "inputs": [], "params": {}, "trace_key": ")" + std::string(65, 'x') + R"("}
      ]
    })");

    Plan plan;
    std::string error;
    REQUIRE_FALSE(ParsePlan(j, plan, &error));
    REQUIRE(error.find("source") != std::string::npos);
    REQUIRE(error.find("at most 64") != std::string::npos);
  }
}

TEST_CASE("Tracer span naming", "[trace]") {
  SECTION("SpanName with trace_key") {
    REQUIRE(Tracer::SpanName("core:sourcer", "main") == "core:sourcer(main)");
    REQUIRE(Tracer::SpanName("core:score_formula", "scorer.v1") == "core:score_formula(scorer.v1)");
  }

  SECTION("SpanName without trace_key") {
    REQUIRE(Tracer::SpanName("core:sourcer", "") == "core:sourcer");
    REQUIRE(Tracer::SpanName("core:model", "") == "core:model");
  }

  SECTION("PrefixedTraceKey combines prefix and child") {
    REQUIRE(Tracer::PrefixedTraceKey("rank_vm", "score") == "rank_vm::score");
    REQUIRE(Tracer::PrefixedTraceKey("parent", "child") == "parent::child");
  }

  SECTION("PrefixedTraceKey handles empty values") {
    REQUIRE(Tracer::PrefixedTraceKey("", "child") == "child");
    REQUIRE(Tracer::PrefixedTraceKey("parent", "") == "parent");
    REQUIRE(Tracer::PrefixedTraceKey("", "") == "");
  }

  SECTION("DeriveTracePrefix extracts filename stem") {
    REQUIRE(Tracer::DeriveTracePrefix("path/to/rank_vm.njs") == "rank_vm");
    REQUIRE(Tracer::DeriveTracePrefix("/absolute/path/module.njs") == "module");
    REQUIRE(Tracer::DeriveTracePrefix("simple.njs") == "simple");
  }

  SECTION("DeriveTracePrefix handles edge cases") {
    REQUIRE(Tracer::DeriveTracePrefix("") == "");
    REQUIRE(Tracer::DeriveTracePrefix("noext") == "noext");
    REQUIRE(Tracer::DeriveTracePrefix("file.txt") == "file");
    REQUIRE(Tracer::DeriveTracePrefix("path/to/file") == "file");
  }

  SECTION("DeriveTracePrefix handles Windows paths") {
    REQUIRE(Tracer::DeriveTracePrefix("path\\to\\module.njs") == "module");
  }
}
