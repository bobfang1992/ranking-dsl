#include <catch2/catch_test_macros.hpp>
#include <catch2/matchers/catch_matchers_floating_point.hpp>

#include "object/obj.h"
#include "object/value.h"
#include "keys/registry.h"
#include "keys.h"

using namespace ranking_dsl;

TEST_CASE("Obj basic operations", "[obj]") {
  SECTION("Empty obj") {
    Obj obj;
    REQUIRE(obj.Size() == 0);
    REQUIRE_FALSE(obj.Has(1001));
    REQUIRE_FALSE(obj.Get(1001).has_value());
  }

  SECTION("Set and get") {
    Obj obj;
    Obj obj2 = obj.Set(1001, 42.0f);

    // Original unchanged
    REQUIRE(obj.Size() == 0);

    // New obj has value
    REQUIRE(obj2.Size() == 1);
    REQUIRE(obj2.Has(1001));

    auto val = obj2.Get(1001);
    REQUIRE(val.has_value());
    REQUIRE(std::get<float>(*val) == 42.0f);
  }

  SECTION("Multiple sets") {
    Obj obj = Obj()
                  .Set(1001, 1.0f)
                  .Set(1002, 2.0f)
                  .Set(1003, 3.0f);

    REQUIRE(obj.Size() == 3);
    REQUIRE(std::get<float>(*obj.Get(1001)) == 1.0f);
    REQUIRE(std::get<float>(*obj.Get(1002)) == 2.0f);
    REQUIRE(std::get<float>(*obj.Get(1003)) == 3.0f);
  }

  SECTION("Overwrite value") {
    Obj obj1 = Obj().Set(1001, 1.0f);
    Obj obj2 = obj1.Set(1001, 2.0f);

    REQUIRE(std::get<float>(*obj1.Get(1001)) == 1.0f);
    REQUIRE(std::get<float>(*obj2.Get(1001)) == 2.0f);
  }

  SECTION("Delete") {
    Obj obj1 = Obj().Set(1001, 1.0f).Set(1002, 2.0f);
    Obj obj2 = obj1.Del(1001);

    // Original unchanged
    REQUIRE(obj1.Has(1001));

    // New obj has key deleted
    REQUIRE_FALSE(obj2.Has(1001));
    REQUIRE(obj2.Has(1002));
  }

  SECTION("Keys()") {
    Obj obj = Obj().Set(1001, 1.0f).Set(1002, 2.0f);
    auto keys = obj.Keys();

    REQUIRE(keys.size() == 2);
    // Order not guaranteed, just check both present
    REQUIRE((keys[0] == 1001 || keys[1] == 1001));
    REQUIRE((keys[0] == 1002 || keys[1] == 1002));
  }
}

TEST_CASE("Obj value types", "[obj]") {
  SECTION("bool") {
    Obj obj = Obj().Set(1, true);
    auto val = obj.Get(1);
    REQUIRE(val.has_value());
    REQUIRE(std::get<bool>(*val) == true);
  }

  SECTION("i64") {
    Obj obj = Obj().Set(1, int64_t{123456789});
    auto val = obj.Get(1);
    REQUIRE(val.has_value());
    REQUIRE(std::get<int64_t>(*val) == 123456789);
  }

  SECTION("f32") {
    Obj obj = Obj().Set(1, 3.14159f);
    auto val = obj.Get(1);
    REQUIRE(val.has_value());
    REQUIRE_THAT(std::get<float>(*val), Catch::Matchers::WithinRel(3.14159f));
  }

  SECTION("string") {
    Obj obj = Obj().Set(1, std::string("hello"));
    auto val = obj.Get(1);
    REQUIRE(val.has_value());
    REQUIRE(std::get<std::string>(*val) == "hello");
  }

  SECTION("bytes") {
    std::vector<uint8_t> data = {0x01, 0x02, 0x03};
    Obj obj = Obj().Set(1, data);
    auto val = obj.Get(1);
    REQUIRE(val.has_value());
    REQUIRE(std::get<std::vector<uint8_t>>(*val) == data);
  }

  SECTION("f32vec") {
    std::vector<float> vec = {1.0f, 2.0f, 3.0f};
    Obj obj = Obj().Set(1, vec);
    auto val = obj.Get(1);
    REQUIRE(val.has_value());
    REQUIRE(std::get<std::vector<float>>(*val) == vec);
  }

  SECTION("null") {
    Obj obj = Obj().Set(1, MakeNull());
    auto val = obj.Get(1);
    REQUIRE(val.has_value());
    REQUIRE(IsNull(*val));
  }
}

TEST_CASE("Value type helpers", "[value]") {
  SECTION("GetValueType") {
    REQUIRE(GetValueType(MakeNull()) == ValueType::Null);
    REQUIRE(GetValueType(true) == ValueType::Bool);
    REQUIRE(GetValueType(int64_t{42}) == ValueType::I64);
    REQUIRE(GetValueType(3.14f) == ValueType::F32);
    REQUIRE(GetValueType(std::string("hello")) == ValueType::String);
    REQUIRE(GetValueType(std::vector<uint8_t>{}) == ValueType::Bytes);
    REQUIRE(GetValueType(std::vector<float>{}) == ValueType::F32Vec);
  }

  SECTION("FormatValue") {
    REQUIRE(FormatValue(MakeNull()) == "null");
    REQUIRE(FormatValue(true) == "true");
    REQUIRE(FormatValue(false) == "false");
    REQUIRE(FormatValue(int64_t{42}) == "42");
    REQUIRE(FormatValue(std::string("test")) == "\"test\"");
  }
}
