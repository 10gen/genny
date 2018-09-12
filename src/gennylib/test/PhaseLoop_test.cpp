#include "test.h"

#include <chrono>
#include <iostream>
#include <optional>

#include <gennylib/Orchestrator.hpp>
#include <gennylib/PhaseLoop.hpp>
#include <log.hh>

using namespace genny;
using namespace genny::V1;
using namespace std;

namespace {

//
// Cute convenience operators -
//  100_i   gives optional<int>     holding 100
//  100_ms  gives optional<millis>  holding 100
//
// TODO: these are copy/pasta in PhaseLoop_test and orchestrator_test. Refactor.
optional<int> operator"" _i(unsigned long long int v) {
    return make_optional(v);
}

optional<chrono::milliseconds> operator"" _ms(unsigned long long int v) {
    return make_optional(chrono::milliseconds{v});
}

}  // namespace

TEST_CASE("Correctness for N iterations") {
    Orchestrator o;

    SECTION("Loops 0 Times") {
        V1::ActorPhase<int> loop{o, {0_i, nullopt}, 1};
        int i = 0;
        for (auto _ : loop)
            ++i;
        REQUIRE(i == 0);
    }
    SECTION("Loops 1 Time") {
        V1::ActorPhase<int> loop{o, {1_i, nullopt}, 1};
        int i = 0;
        for (auto _ : loop)
            ++i;
        REQUIRE(i == 1);
    }
    SECTION("Loops 113 Times") {
        V1::ActorPhase<int> loop{o, {113_i, nullopt}, 1};
        int i = 0;
        for (auto _ : loop)
            ++i;
        REQUIRE(i == 113);
    }

    SECTION("Configured for -1 Times barfs") {
        REQUIRE_THROWS_WITH((V1::ActorPhase<int>{o, {make_optional(-1), nullopt}, 1}),
                            Catch::Contains("Need non-negative number of iterations. Gave -1"));
    }
}

TEST_CASE("Correctness for N milliseconds") {
    Orchestrator o;
    SECTION("Loops 0 milliseconds so zero times") {
        V1::ActorPhase<int> loop{o, {nullopt, 0_ms}, 0};
        int i = 0;
        for (auto _ : loop)
            ++i;
        REQUIRE(i == 0);
    }
    SECTION("Looping for 10 milliseconds takes between 10 and 11 milliseconds") {
        // we nop in the loop so ideally it should take exactly 10ms, but don't want spurious
        // failures
        V1::ActorPhase<int> loop{o, {nullopt, 10_ms}, 0};

        auto start = chrono::system_clock::now();
        for (auto _ : loop) {
        }  // nop
        auto elapsed =
            chrono::duration_cast<chrono::milliseconds>(chrono::system_clock::now() - start)
                .count();

        REQUIRE(elapsed >= 10);
        REQUIRE(elapsed <= 11);
    }
}

TEST_CASE("Combinations of duration and iterations") {
    Orchestrator o;
    SECTION("Loops 0 milliseconds but 100 times") {
        V1::ActorPhase<int> loop{o, {100_i, 0_ms}, 0};
        int i = 0;
        for (auto _ : loop)
            ++i;
        REQUIRE(i == 100);
    }
    SECTION("Loops 5 milliseconds, 100 times: 10 millis dominates") {
        V1::ActorPhase<int> loop{o, {100_i, 5_ms}, 0};

        auto start = chrono::system_clock::now();
        int i = 0;
        for (auto _ : loop)
            ++i;
        auto elapsed =
            chrono::duration_cast<chrono::milliseconds>(chrono::system_clock::now() - start)
                .count();

        REQUIRE(i > 100);
        REQUIRE(elapsed >= 5);
        REQUIRE(elapsed <= 6);
    }
    // It's tempting to write a test where the # iterations dominates the time
    // e.g. OperationLoop loop{1000000000000_i, 1_ms} but that would fail spuriously
    // on super-fast hardware. So resist the temptation and trust the logical
    // combinations of the other tests 🙈

    SECTION("Configured for -1 milliseconds barfs") {
        REQUIRE_THROWS_WITH(
            (V1::ActorPhase<int>{o, {nullopt, make_optional(chrono::milliseconds{-1})}, 0}),
            Catch::Contains("Need non-negative duration. Gave -1 milliseconds"));
    }
}

TEST_CASE("Can do without either iterations or duration") {
    Orchestrator o;
    V1::ActorPhase<int>{o, {nullopt, nullopt}, 0};
    // TODO: assert behavior with Orchestrator here
}

TEST_CASE("Iterator concept correctness") {
    Orchestrator o;
    V1::ActorPhase<int> loop{o, {1_i, nullopt}, 0};

    // can deref
    SECTION("Deref and advance works") {
        auto it = loop.begin();
        REQUIRE(it != loop.end());
        *it;
        REQUIRE(it != loop.end());
        ++it;
        REQUIRE(it == loop.end());
        REQUIRE(it == loop.end());
        REQUIRE(loop.end() == it);

        auto end = loop.end();
        *end;
        ++end;
        REQUIRE(end == end);
        REQUIRE(end == loop.end());

        // can still advance and still deref
        // debatable about whether this *should* work or whether it should be tested/asserted
        ++it;
        *it;
        REQUIRE(it == loop.end());
    }

    SECTION("Equality is maintained through incrementation") {
        auto it1 = loop.begin();
        auto it2 = loop.begin();

        REQUIRE(it1 == it1);
        REQUIRE(it2 == it2);

        REQUIRE(it1 == it2);
        REQUIRE(it2 == it1);

        ++it1;
        REQUIRE(it1 != it2);
        REQUIRE(it2 != it1);

        ++it2;
        REQUIRE(it1 == it2);
        REQUIRE(it2 == it1);
    }

    SECTION("End iterators all equal") {
        auto end1 = loop.end();
        auto end2 = loop.end();
        REQUIRE(end1 == end2);
        REQUIRE(end2 == end1);
        REQUIRE(end1 == end1);
        REQUIRE(end2 == end2);
    }
}


TEST_CASE("Actual Actor Example", "[real]") {

    struct IncrementsMapValues : public Actor {
        struct IncrPhaseConfig {
            int _key;
            IncrPhaseConfig(PhaseContext& ctx) : _key{ctx.get<int>("Key")} {}
        };

        PhaseLoop<IncrPhaseConfig> _loop;
        std::unordered_map<int, int>& _counters;

        IncrementsMapValues(ActorContext& actorContext, std::unordered_map<int, int>& counters)
            : _loop{actorContext}, _counters{counters} {}

        void run() override {
            for (auto&& [num, cfg] : _loop) {
                for (auto&& _ : cfg) {
                    ++this->_counters[cfg->_key];
                }
            }
        }

        static auto producer(std::unordered_map<int, int>& counters) {
            return [&](ActorContext& actorContext) {
                ActorVector out;
                out.push_back(std::make_unique<IncrementsMapValues>(actorContext, counters));
                return out;
            };
        }
    };

    Orchestrator o;
    o.addRequiredTokens(1);
    o.phasesAtLeastTo(0);  // this will need to be 2, but we don't set it; rely on actor to do so

    metrics::Registry reg;

    YAML::Node doc = YAML::Load(R"(
        SchemaVersion: 2018-07-01
        MongoUri: mongodb://localhost:27017
        Actors:
        - Phases:
          - Repeat: 100
            Key: 71
          - Repeat: 3
            Key: 93
    )");

    std::unordered_map<int, int> counters;

    std::vector<ActorProducer> producers = {IncrementsMapValues::producer(counters)};
    WorkloadContext wl{doc, reg, o, producers};
    REQUIRE(wl.actors().size() == 1);

    wl.actors()[0]->run();
    REQUIRE(counters ==
            std::unordered_map<int, int>{{71, 100},  // keys came from yaml config
                                         {93, 3}});
}
