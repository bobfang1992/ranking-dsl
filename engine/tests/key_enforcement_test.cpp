#include <catch2/catch_test_macros.hpp>

#include "object/obj.h"
#include "keys/registry.h"
#include "keys.h"

using namespace ranking_dsl;

TEST_CASE("Key type enforcement", "[key]") {
  KeyRegistry registry;
  registry.LoadFromCompiled();

  SECTION("Valid type passes") {
    Obj obj;
    // score.base is f32, setting float should work
    REQUIRE_NOTHROW(obj.Set(keys::id::SCORE_BASE, 0.5f, &registry));
  }

  SECTION("Wrong type throws") {
    Obj obj;
    // score.base is f32, setting string should fail
    REQUIRE_THROWS(obj.Set(keys::id::SCORE_BASE, std::string("wrong"), &registry));
  }

  SECTION("i64 key accepts int64") {
    Obj obj;
    // cand.candidate_id is i64
    REQUIRE_NOTHROW(obj.Set(keys::id::CAND_CANDIDATE_ID, int64_t{123}, &registry));
  }

  SECTION("i64 key rejects float") {
    Obj obj;
    REQUIRE_THROWS(obj.Set(keys::id::CAND_CANDIDATE_ID, 123.0f, &registry));
  }

  SECTION("f32vec key accepts vector") {
    Obj obj;
    std::vector<float> vec = {1.0f, 2.0f, 3.0f};
    REQUIRE_NOTHROW(obj.Set(keys::id::FEAT_EMBEDDING, vec, &registry));
  }

  SECTION("f32vec key rejects scalar") {
    Obj obj;
    REQUIRE_THROWS(obj.Set(keys::id::FEAT_EMBEDDING, 1.0f, &registry));
  }

  SECTION("Null value always accepted") {
    Obj obj;
    // Null should be allowed for any key type
    REQUIRE_NOTHROW(obj.Set(keys::id::SCORE_BASE, MakeNull(), &registry));
    REQUIRE_NOTHROW(obj.Set(keys::id::CAND_CANDIDATE_ID, MakeNull(), &registry));
    REQUIRE_NOTHROW(obj.Set(keys::id::FEAT_EMBEDDING, MakeNull(), &registry));
  }

  SECTION("Unknown key throws") {
    Obj obj;
    REQUIRE_THROWS(obj.Set(99999, 1.0f, &registry));
  }

  SECTION("Without registry, no validation") {
    Obj obj;
    // Without registry, any type should work
    REQUIRE_NOTHROW(obj.Set(keys::id::SCORE_BASE, std::string("wrong")));
  }
}

TEST_CASE("KeyRegistry", "[key]") {
  SECTION("Load from compiled") {
    KeyRegistry registry;
    registry.LoadFromCompiled();

    REQUIRE(registry.AllKeys().size() > 0);

    auto* key = registry.GetById(keys::id::SCORE_BASE);
    REQUIRE(key != nullptr);
    REQUIRE(key->name == "score.base");
    REQUIRE(key->type == keys::KeyType::F32);
  }

  SECTION("Lookup by name") {
    KeyRegistry registry;
    registry.LoadFromCompiled();

    auto* key = registry.GetByName("score.final");
    REQUIRE(key != nullptr);
    REQUIRE(key->id == keys::id::SCORE_FINAL);
  }

  SECTION("Unknown key returns nullptr") {
    KeyRegistry registry;
    registry.LoadFromCompiled();

    REQUIRE(registry.GetById(99999) == nullptr);
    REQUIRE(registry.GetByName("unknown.key") == nullptr);
  }
}
