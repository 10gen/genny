#include "test.h"

#include <chrono>
#include <iostream>
#include <optional>

#include <gennylib/PhaseLoop.hpp>


using namespace genny;
using namespace std;


//
// Cute convenience operators -
//  100_i   gives optional<int>     holding 100
//  100_ms  gives optional<millis>  holding 100
//
optional<int> operator"" _i(unsigned long long int v) {
    return make_optional(v);
}
optional<chrono::milliseconds> operator"" _ms(unsigned long long int v) {
    return make_optional(chrono::milliseconds{v});
}

TEST_CASE("Correctness for N iterations") {
    SECTION("Loops 0 Times") {
        PhaseLoop loop{0_i, nullopt};
        int i = 0;
        for (auto _ : loop)
            ++i;
        REQUIRE(i == 0);
    }
    SECTION("Loops 1 Time") {
        PhaseLoop loop{1_i, nullopt};
        int i = 0;
        for (auto _ : loop)
            ++i;
        REQUIRE(i == 1);
    }
    SECTION("Loops 113 Times") {
        PhaseLoop loop{113_i, nullopt};
        int i = 0;
        for (auto _ : loop)
            ++i;
        REQUIRE(i == 113);
    }

    SECTION("Configured for -1 Times barfs") {
        REQUIRE_THROWS_WITH( (PhaseLoop{make_optional(-1), nullopt}),
                             Catch::Contains("Need non-negative number of iterations. Gave -1"));
    }
}

TEST_CASE("Correctness for N milliseconds") {
    SECTION("Loops 0 milliseconds so zero times") {
        PhaseLoop loop{nullopt, 0_ms};
        int i = 0;
        for (auto _ : loop)
            ++i;
        REQUIRE(i == 0);
    }
    SECTION("Looping for 10 milliseconds takes between 10 and 11 milliseconds") {
        // we nop in the loop so ideally it should take exactly 10ms, but don't want spurious
        // failures
        PhaseLoop loop{nullopt, 10_ms};

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
    SECTION("Loops 0 milliseconds but 100 times") {
        PhaseLoop loop{100_i, 0_ms};
        int i = 0;
        for (auto _ : loop)
            ++i;
        REQUIRE(i == 100);
    }
    SECTION("Loops 5 milliseconds, 100 times: 10 millis dominates") {
        PhaseLoop loop{100_i, 5_ms};

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
    // e.g. PhaseLoop loop{1000000000000_i, 1_ms} but that would fail spuriously
    // on super-fast hardware. So resist the temptation and trust the logical
    // combinations of the other tests 🙈

    SECTION("Configured for -1 milliseconds barfs") {
        REQUIRE_THROWS_WITH( (PhaseLoop{nullopt, make_optional(chrono::milliseconds{-1})}),
                              Catch::Contains("Need non-negative duration. Gave -1 milliseconds") );
    }
}

TEST_CASE("Need either iterations or duration") {
    REQUIRE_THROWS_WITH( (PhaseLoop{nullopt, nullopt}),
                         Catch::Contains("Need to specify either min iterations or min duration"));
}

TEST_CASE("Iterator concept correctness") {
    PhaseLoop loop{1_i, nullopt};

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