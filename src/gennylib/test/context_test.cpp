#include "test.h"

#include <functional>
#include <iomanip>
#include <iostream>
#include <optional>
#include <random>

#include <yaml-cpp/yaml.h>

#include <gennylib/context.hpp>
#include <gennylib/metrics.hpp>
#include <log.hh>


using namespace genny;
using namespace std;

using Catch::Matchers::Matches;
using Catch::Matchers::StartsWith;

template <class Out, class... Args>
void errors(const string& yaml, string message, Args... args) {
    genny::metrics::Registry metrics;
    genny::Orchestrator orchestrator;
    string modified =
        "SchemaVersion: 2018-07-01\nMongoUri: mongodb://localhost:27017\nActors: []\n" + yaml;
    auto read = YAML::Load(modified);
    auto test = [&]() {
        auto context = WorkloadContext{read, metrics, orchestrator, {}};
        return context.get<Out>(std::forward<Args>(args)...);
    };
    CHECK_THROWS_WITH(test(), StartsWith(message));
}
template <class Out,
          bool Required = true,
          class OutV = typename std::conditional<Required, Out, std::optional<Out>>::type,
          class... Args>
void gives(const string& yaml, OutV expect, Args... args) {
    genny::metrics::Registry metrics;
    genny::Orchestrator orchestrator;
    string modified =
        "SchemaVersion: 2018-07-01\nMongoUri: mongodb://localhost:27107\nActors: []\n" + yaml;
    auto read = YAML::Load(modified);
    auto test = [&]() {
        auto context = WorkloadContext{read, metrics, orchestrator, {}};
        return context.get<Out, Required>(std::forward<Args>(args)...);
    };
    REQUIRE(test() == expect);
}

TEST_CASE("loads configuration okay") {
    genny::metrics::Registry metrics;
    genny::Orchestrator orchestrator;
    SECTION("Valid YAML") {
        auto yaml = YAML::Load(R"(
SchemaVersion: 2018-07-01
MongoUri: mongodb://localhost:27017
Actors:
- Name: HelloWorld
  Count: 7
        )");
        WorkloadContext w{yaml, metrics, orchestrator, {}};
        auto actors = w.get("Actors");
    }

    SECTION("Invalid Schema Version") {
        auto yaml = YAML::Load(
            "SchemaVersion: 2018-06-27\nMongoUri: mongodb://localhost:27017\nActors: []");

        auto test = [&]() { WorkloadContext w(yaml, metrics, orchestrator, {}); };
        REQUIRE_THROWS_WITH(test(), Matches("Invalid schema version"));
    }

    SECTION("Invalid config accesses") {
        // key not found
        errors<string>("Foo: bar", "Invalid key [FoO]", "FoO");
        // yaml library does type-conversion; we just forward through...
        gives<string>("Foo: 123", "123", "Foo");
        gives<int>("Foo: 123", 123, "Foo");
        // ...and propagate errors.
        errors<int>("Foo: Bar", "Bad conversion of [Bar] to [i] at path [Foo/]:", "Foo");
        // okay
        gives<int>("Foo: [1,\"bar\"]", 1, "Foo", 0);
        // give meaningful error message:
        errors<string>("Foo: [1,\"bar\"]",
                       "Invalid key [0] at path [Foo/0/]. Last accessed [[1, bar]].",
                       "Foo",
                       "0");

        errors<string>("Foo: 7", "Wanted [Foo/Bar] but [Foo/] is scalar: [7]", "Foo", "Bar");
        errors<string>(
            "Foo: 7", "Wanted [Foo/Bar] but [Foo/] is scalar: [7]", "Foo", "Bar", "Baz", "Bat");

        auto other = R"(Other: [{ Foo: [{Key: 1, Another: true, Nested: [false, true]}] }])";

        gives<int>(other, 1, "Other", 0, "Foo", 0, "Key");
        gives<bool>(other, true, "Other", 0, "Foo", 0, "Another");
        gives<bool>(other, false, "Other", 0, "Foo", 0, "Nested", 0);
        gives<bool>(other, true, "Other", 0, "Foo", 0, "Nested", 1);

        gives<int>("Some Ints: [1,2,[3,4]]", 1, "Some Ints", 0);
        gives<int>("Some Ints: [1,2,[3,4]]", 2, "Some Ints", 1);
        gives<int>("Some Ints: [1,2,[3,4]]", 3, "Some Ints", 2, 0);
        gives<int>("Some Ints: [1,2,[3,4]]", 4, "Some Ints", 2, 1);

        gives<int, false>("A: 1", std::nullopt, "B");
        gives<int, false>("A: 2", make_optional<int>(2), "A");
        gives<int, false>("A: {B: [1,2,3]}", make_optional<int>(2), "A", "B", 1);

        gives<int, false>("A: {B: [1,2,3]}", std::nullopt, "A", "B", 30);
        gives<int, false>("A: {B: [1,2,3]}", std::nullopt, "B");
    }

    SECTION("Empty Yaml") {
        auto yaml = YAML::Load("MongoUri: mongodb://localhost:27017\nActors: []");
        auto test = [&]() { WorkloadContext w(yaml, metrics, orchestrator, {}); };
        REQUIRE_THROWS_WITH(test(), Matches(R"(Invalid key \[SchemaVersion\] at path(.*\n*)*)"));
    }
    SECTION("No Actors") {
        auto yaml = YAML::Load("SchemaVersion: 2018-07-01\nMongoUri: mongodb://localhost:27017");
        auto test = [&]() { WorkloadContext w(yaml, metrics, orchestrator, {}); };
        REQUIRE_THROWS_WITH(test(), Matches(R"(Invalid key \[Actors\] at path(.*\n*)*)"));
    }
    SECTION("No MongoUri") {
        auto yaml = YAML::Load("SchemaVersion: 2018-07-01\nActors: []");
        auto test = [&]() { WorkloadContext w(yaml, metrics, orchestrator, {}); };
        REQUIRE_THROWS_WITH(test(), Matches(R"(bad conversion)"));
    }
    SECTION("Invalid MongoUri") {
        auto yaml = YAML::Load("SchemaVersion: 2018-07-01\nMongoUri: notValid\nActors: []");
        auto test = [&]() { WorkloadContext w(yaml, metrics, orchestrator, {}); };
        REQUIRE_THROWS_WITH(test(), Matches(R"(an invalid MongoDB URI was provided)"));
    }

    SECTION("Can call two actor producers") {
        auto yaml = YAML::Load(R"(
SchemaVersion: 2018-07-01
MongoUri: mongodb://localhost:27017
Actors:
- Name: One
  SomeList: [100, 2, 3]
- Name: Two
  Count: 7
  SomeList: [2]
        )");

        int calls = 0;
        std::vector<ActorProducer> producers;
        producers.emplace_back([&](ActorContext& context) {
            REQUIRE(context.workload().get<int>("Actors", 0, "SomeList", 0) == 100);
            ++calls;
            return ActorVector{};
        });
        producers.emplace_back([&](ActorContext& context) {
            REQUIRE(context.workload().get<int>("Actors", 1, "Count") == 7);
            ++calls;
            return ActorVector{};
        });

        auto context = WorkloadContext{yaml, metrics, orchestrator, producers};
        REQUIRE(std::distance(context.actors().begin(), context.actors().end()) == 0);
    }
}

void onContext(YAML::Node& yaml, std::function<void(ActorContext&)>& op) {
    genny::metrics::Registry metrics;
    genny::Orchestrator orchestrator;

    ActorProducer producer = [&](ActorContext& context) -> ActorVector {
        op(context);
        return {};
    };

    WorkloadContext {yaml, metrics, orchestrator, {
        producer
    }};
}

TEST_CASE("PhaseContexts constructed as expected") {
    auto yaml = YAML::Load(R"(
    SchemaVersion: 2018-07-01
    MongoUri: mongodb://localhost:27017
    Actors:
    - Name: HelloWorld
      Foo: Bar
      Foo2: Bar2
      Phases:
      - Operation: One
        Foo: Baz
      - Operation: Two
        Phase: 2 # intentionally out of order for testing
      - Operation: Three
        Phase: 1 # intentionally out of order for testing
        Extra: [1,2]
    )");

    SECTION("Loads Phases") {
        // "test of the test"
        int calls = 0;
        std::function<void(ActorContext&)> op = [&](ActorContext&ctx) {
            ++calls;
        };
        onContext(yaml, op);
        REQUIRE(calls == 1);
    }

    SECTION("One Phase per block") {
        std::function<void(ActorContext&)> op = [&](ActorContext&ctx) {
            const auto& ph = ctx.phases();
            REQUIRE(ph.size() == 3);
        };
        onContext(yaml, op);
    }
    SECTION("Phase index is defaulted") {
        std::function<void(ActorContext&)> op = [&](ActorContext&ctx) {
            REQUIRE(ctx.phases().at(0)->get<std::string>("Operation") == "One");
            REQUIRE(ctx.phases().at(1)->get<std::string>("Operation") == "Three");
            REQUIRE(ctx.phases().at(2)->get<std::string>("Operation") == "Two");
        };
        onContext(yaml, op);
    }
    SECTION("Phase values can override parent values") {
        std::function<void(ActorContext&)> op = [&](ActorContext&ctx) {
            REQUIRE(ctx.phases().at(0)->get<std::string>("Foo") == "Baz");
            REQUIRE(ctx.phases().at(1)->get<std::string>("Foo") == "Bar");
            REQUIRE(ctx.phases().at(2)->get<std::string>("Foo") == "Bar");
        };
        onContext(yaml, op);
    }
    SECTION("Optional values also override") {
        std::function<void(ActorContext&)> op = [&](ActorContext&ctx) {
            REQUIRE(*(ctx.phases().at(0)->get<std::string, false>("Foo")) == "Baz");
            REQUIRE(*(ctx.phases().at(1)->get<std::string, false>("Foo")) == "Bar");
            REQUIRE(*(ctx.phases().at(2)->get<std::string, false>("Foo")) == "Bar");
            // call twice just for funsies
            REQUIRE(*(ctx.phases().at(2)->get<std::string, false>("Foo")) == "Bar");
        };
        onContext(yaml, op);
    }
    SECTION("Optional values can be found from parent") {
        std::function<void(ActorContext&)> op = [&](ActorContext&ctx) {
            REQUIRE(*(ctx.phases().at(0)->get<std::string, false>("Foo2")) == "Bar2");
            REQUIRE(*(ctx.phases().at(1)->get<std::string, false>("Foo2")) == "Bar2");
            REQUIRE(*(ctx.phases().at(2)->get<std::string, false>("Foo2")) == "Bar2");
        };
        onContext(yaml, op);
    }
    SECTION("Phases can have extra configs") {
        std::function<void(ActorContext&)> op = [&](ActorContext&ctx) {
            REQUIRE(ctx.phases().at(1)->get<int>("Extra", 0) == 1);
        };
        onContext(yaml, op);
    }
    SECTION("Missing require values throw") {
        std::function<void(ActorContext&)> op = [&](ActorContext&ctx) {
            REQUIRE_THROWS(ctx.phases().at(1)->get<int>("Extra", 100));
        };
        onContext(yaml, op);
    }
}

TEST_CASE("Get Value Generators") {
    auto yaml = YAML::Load(R"(
    SchemaVersion: 2018-07-01
    MongoUri: mongodb://localhost:27017
    Actors:
    - Name: HelloWorld
      Document:
        z: {$randomint: {min: 50, max: 60}}
    )");

    std::mt19937_64 rng{};

    std::function<void(ActorContext&)> op = [&](ActorContext&ctx) {
        auto docgen = ctx.get<genny::value_generators::DocumentGenerator>(rng, "Document");
        bsoncxx::builder::stream::document mydoc{};
        auto view = docgen->view(mydoc);
        auto z = view["z"].get_int64().value;
        REQUIRE(z >= 50);
        REQUIRE(z <= 60);
    };
    onContext(yaml, op);

    std::function<void(ActorContext&)> op331 = [&](ActorContext&ctx) {
        auto docgen = ctx.get<genny::value_generators::DocumentGenerator, false>(rng, "Document");
        bsoncxx::builder::stream::document mydoc{};
        auto view = (*docgen)->view(mydoc);
        auto z = view["z"].get_int64().value;
        REQUIRE(z >= 50);
        REQUIRE(z <= 60);
    };
    onContext(yaml, op331);

    std::function<void(ActorContext&)> op33 = [&](ActorContext&ctx) {
        auto docgen = ctx.get<genny::value_generators::DocumentGenerator, false>(rng, "DocumentNotFound");
        REQUIRE(!docgen);
    };
    onContext(yaml, op33);

    std::function<void(ActorContext&)> op2 = [&](ActorContext&ctx) {
        auto node = ctx.get("Document");
        REQUIRE(node["z"]["$randomint"]["min"].as<int>() == 50);
    };
    onContext(yaml, op2);

    std::function<void(ActorContext&)> op3 = [&](ActorContext&ctx) {
        auto node = ctx.get();
        REQUIRE(node["Name"].as<std::string>() == "HelloWorld");
    };
    onContext(yaml, op3);
}

TEST_CASE("No PhaseContexts") {
    auto yaml = YAML::Load(R"(
    SchemaVersion: 2018-07-01
    MongoUri: mongodb://localhost:27017
    Actors:
    - Name: HelloWorld
    )");

    SECTION("Empty PhaseContexts") {
        std::function<void(ActorContext&)> op = [&](ActorContext&ctx) {
            REQUIRE(ctx.phases().size() == 0);
        };
        onContext(yaml, op);
    }
}

