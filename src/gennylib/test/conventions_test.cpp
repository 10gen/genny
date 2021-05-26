// Copyright 2019-present MongoDB Inc.
//
// Licensed under the Apache License, Version 2.0 (the "License");
// you may not use this file except in compliance with the License.
// You may obtain a copy of the License at
//
// http://www.apache.org/licenses/LICENSE-2.0
//
// Unless required by applicable law or agreed to in writing, software
// distributed under the License is distributed on an "AS IS" BASIS,
// WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
// See the License for the specific language governing permissions and
// limitations under the License.

#include <chrono>
#include <cmath>

#include <gennylib/Node.hpp>
#include <gennylib/conventions.hpp>

#include <testlib/MongoTestFixture.hpp>
#include <testlib/helpers.hpp>


namespace genny {
namespace {
using namespace std::chrono;

auto&& fromYaml(const std::string& yaml) {
    // hacky but we can't return the .root() and then dangle the source root ref.
    static std::optional<NodeSource> source;
    source.emplace(yaml, "");
    return source->root();
}

TEST_CASE("Conventions used by PhaseLoop") {
    NodeSource ns(R"(
    SchemaVersion: 2018-07-01
    Database: test
    Actors:
    - Name: MetricsNameTest
      Type: HelloWorld
      Threads: 1
      Phases:
      - Repeat: 1
    )",
                  "");
    auto& yaml = ns.root();
    auto& phaseContext = yaml["Actors"][0]["Phases"][0];
    // Test of the test
    REQUIRE(phaseContext);

    REQUIRE(phaseContext["Nop"].maybe<bool>().value_or(false) == false);

    // from `explicit IterationChecker(PhaseContext& phaseContext)` ctor
    REQUIRE(phaseContext["Duration"].maybe<TimeSpec>() == std::nullopt);
    REQUIRE(*(phaseContext["Repeat"].maybe<IntegerSpec>()) == IntegerSpec{1});
    REQUIRE(phaseContext["SleepBefore"].maybe<TimeSpec>().value_or(TimeSpec{33}) == TimeSpec{33});
    REQUIRE(phaseContext["SleepAfter"].maybe<TimeSpec>().value_or(TimeSpec{33}) == TimeSpec{33});
    REQUIRE(phaseContext["Rate"].maybe<RateSpec>() == std::nullopt);
    REQUIRE(phaseContext["RateLimiterName"].maybe<std::string>().value_or("defaultRateLimiter") ==
            "defaultRateLimiter");
}

TEST_CASE("genny::TimeSpec conversions") {
    SECTION("Can convert to genny::TimeSpec") {
        REQUIRE(fromYaml("D: 3 seconds")["D"].to<TimeSpec>().count() == 3 * std::pow(10, 9));
        REQUIRE(fromYaml("0 second").to<TimeSpec>().count() == 0);
        REQUIRE(fromYaml("20 millisecond").to<TimeSpec>().count() == 20 * std::pow(10, 6));
        REQUIRE(fromYaml("33 microsecond").to<TimeSpec>().count() == 33 * std::pow(10, 3));
        REQUIRE(fromYaml("2e3 microseconds").to<TimeSpec>().count() == 2000 * std::pow(10, 3));
        REQUIRE(fromYaml("10.3e2 nanoseconds").to<TimeSpec>().count() == 1030);
        REQUIRE(fromYaml("3 hour").to<TimeSpec>().count() == 3 * 3600 * std::pow(10, 9));
        REQUIRE(fromYaml("2 minutes").to<TimeSpec>().count() == 2 * 60 * std::pow(10, 9));
    }

    SECTION("Overlooks small typos") {
        REQUIRE(fromYaml("D: 3 secondsasdfadsf     ")["D"].to<TimeSpec>().count() ==
                3 * std::pow(10, 9));
    }

    SECTION("Barfs on unknown types") {
        REQUIRE_THROWS(fromYaml("-1 nanosecond").to<TimeSpec>());
        REQUIRE_THROWS(fromYaml("foo").to<TimeSpec>());
        REQUIRE_THROWS(fromYaml("[1,2,3]").to<TimeSpec>());
        REQUIRE_THROWS(fromYaml("[]").to<TimeSpec>());
        REQUIRE_THROWS(fromYaml("{}").to<TimeSpec>());
        REQUIRE_THROWS(fromYaml("what nanoseconds").to<TimeSpec>());
        REQUIRE_THROWS(fromYaml("29 picoseconds").to<TimeSpec>());
        REQUIRE_THROWS(fromYaml("1e3 centuries").to<TimeSpec>());
        REQUIRE_THROWS(fromYaml("mongodb").to<TimeSpec>());
        REQUIRE_THROWS(fromYaml("1").to<TimeSpec>());
        REQUIRE_THROWS(fromYaml("333").to<TimeSpec>());
    }

    SECTION("Barfs on invalid number of spaces") {
        REQUIRE_THROWS(fromYaml("1  second").to<TimeSpec>());
        REQUIRE_THROWS(fromYaml("1second").to<TimeSpec>());
    }
}

TEST_CASE("genny::IntegerSpec conversions") {
    SECTION("Can convert to genny::IntegerSpec") {
        REQUIRE(fromYaml("Repeat: 300")["Repeat"].to<IntegerSpec>().value == 300);
        REQUIRE(fromYaml("0").to<IntegerSpec>().value == 0);
        REQUIRE(fromYaml("1e3").to<IntegerSpec>().value == 1000);
        REQUIRE(fromYaml("10.3e2").to<IntegerSpec>().value == 1030);
    }

    SECTION("Barfs on invalid values") {
        REQUIRE_THROWS(fromYaml("-1").to<IntegerSpec>());
        REQUIRE_THROWS(fromYaml("1e100000").to<IntegerSpec>());
        REQUIRE_THROWS(fromYaml("1e-3").to<IntegerSpec>());
        REQUIRE_THROWS(fromYaml("foo").to<IntegerSpec>());
        REQUIRE_THROWS(fromYaml("").to<IntegerSpec>());
        REQUIRE_THROWS(fromYaml("-e1").to<IntegerSpec>());
        REQUIRE_THROWS(fromYaml("e").to<IntegerSpec>());
        REQUIRE_THROWS(fromYaml("0.1").to<IntegerSpec>());
        REQUIRE_THROWS(fromYaml("-100.33e-1").to<IntegerSpec>());
    }
}

TEST_CASE("genny::BaseRateSpec conversions") {
    SECTION("Can convert to genny::BaseRateSpec") {
        REQUIRE(fromYaml("GlobalRate: 300 per 2 nanoseconds")["GlobalRate"]
                    .to<BaseRateSpec>()
                    .operations == 300);
        REQUIRE(fromYaml("GlobalRate: 300 per 2 nanoseconds")["GlobalRate"]
                    .to<BaseRateSpec>()
                    .per.count() == 2);
    }

    SECTION("Barfs on invalid values") {
        REQUIRE_THROWS(fromYaml("-1 per -1 nanosecond").to<BaseRateSpec>());
        REQUIRE_THROWS(fromYaml("-1 per -1 nanosecond").to<BaseRateSpec>());
        REQUIRE_THROWS(fromYaml("1 pe 1000 nanoseconds").to<BaseRateSpec>());
        REQUIRE_THROWS(fromYaml("per").to<BaseRateSpec>());
        REQUIRE_THROWS(fromYaml("nanoseconds per 1").to<BaseRateSpec>());
        REQUIRE_THROWS(fromYaml("1per2second").to<BaseRateSpec>());
        REQUIRE_THROWS(fromYaml("0per").to<BaseRateSpec>());
        REQUIRE_THROWS(fromYaml("xper").to<BaseRateSpec>());
        REQUIRE_THROWS(fromYaml("{foo}").to<BaseRateSpec>());
        REQUIRE_THROWS(fromYaml("").to<BaseRateSpec>());
    }
}

TEST_CASE("genny::PercentileRateSpec conversions") {
    SECTION("Can convert to genny::PercentileRateSpec") {
        REQUIRE(fromYaml("GlobalRate: 50%")["GlobalRate"].to<PercentileRateSpec>().percent == 50);
        REQUIRE(fromYaml("GlobalRate: 78%")["GlobalRate"].to<PercentileRateSpec>().percent == 78);
        REQUIRE(fromYaml("GlobalRate: 5%")["GlobalRate"].to<PercentileRateSpec>().percent == 5);
    }

    SECTION("Barfs on invalid values") {
        REQUIRE_THROWS(fromYaml("-1%").to<PercentileRateSpec>());
        REQUIRE_THROWS(fromYaml("2899").to<PercentileRateSpec>());
        REQUIRE_THROWS(fromYaml("300 per 2 nanoseconds").to<PercentileRateSpec>());
        REQUIRE_THROWS(fromYaml("%").to<PercentileRateSpec>());
        REQUIRE_THROWS(fromYaml("%15").to<PercentileRateSpec>());
        REQUIRE_THROWS(fromYaml("28.999%").to<PercentileRateSpec>());
        REQUIRE_THROWS(fromYaml("28.999%").to<PercentileRateSpec>());
        REQUIRE_THROWS(fromYaml("").to<PercentileRateSpec>());
    }
}

TEST_CASE("genny::RateSpec conversions") {
    SECTION("Can convert to genny::RateSpec") {
        REQUIRE(fromYaml("GlobalRate: 25 per 5 seconds")["GlobalRate"]
                    .to<RateSpec>()
                    .getBaseSpec()
                    ->operations == 25);
        REQUIRE(fromYaml("GlobalRate: 25 per 5 seconds")["GlobalRate"]
                    .to<RateSpec>()
                    .getBaseSpec()
                    ->per.count() == 5000000000);
        REQUIRE_FALSE(fromYaml("GlobalRate: 25 per 5 seconds")["GlobalRate"]
                          .to<RateSpec>()
                          .getPercentileSpec());

        REQUIRE(
            fromYaml("GlobalRate: 30%")["GlobalRate"].to<RateSpec>().getPercentileSpec()->percent ==
            30);
        REQUIRE_FALSE(fromYaml("GlobalRate: 30%")["GlobalRate"].to<RateSpec>().getBaseSpec());
    }

    SECTION("Barfs on invalid values") {
        REQUIRE_THROWS(fromYaml("p%er").to<RateSpec>());
        REQUIRE_THROWS(fromYaml("25 nanoseconds per 1").to<RateSpec>());
        REQUIRE_THROWS(fromYaml("46%28").to<RateSpec>());
        REQUIRE_THROWS(fromYaml("{499}").to<RateSpec>());
        REQUIRE_THROWS(fromYaml("").to<RateSpec>());
    }
}


TEST_CASE("genny::PhaseRangeSpec conversions") {
    SECTION("Can convert to genny::PhaseRangeSpec") {
        {
            auto&& yaml = fromYaml("Phase: 0..20");
            REQUIRE(yaml["Phase"].to<PhaseRangeSpec>().start == 0);
            REQUIRE(yaml["Phase"].to<PhaseRangeSpec>().end == 20);
        }

        {
            auto&& yaml = fromYaml("Phase: 2..2");
            REQUIRE(yaml["Phase"].to<PhaseRangeSpec>().start == 2);
            REQUIRE(yaml["Phase"].to<PhaseRangeSpec>().end == 2);
        }

        {
            auto&& yaml = fromYaml("Phase: 0..1e2");
            REQUIRE(yaml["Phase"].to<PhaseRangeSpec>().start == 0);
            REQUIRE(yaml["Phase"].to<PhaseRangeSpec>().end == 100);
        }

        {
            auto&& yaml = fromYaml("Phase: 10 .. 1e2");
            REQUIRE(yaml["Phase"].to<PhaseRangeSpec>().start == 10);
            REQUIRE(yaml["Phase"].to<PhaseRangeSpec>().end == 100);
        }

        {
            auto&& yaml = fromYaml("Phase: 12");
            REQUIRE(yaml["Phase"].to<PhaseRangeSpec>().start == 12);
            REQUIRE(yaml["Phase"].to<PhaseRangeSpec>().end == 12);
        }
    }

    SECTION("Barfs on invalid values") {
        REQUIRE_THROWS(fromYaml("0....20").to<PhaseRangeSpec>());
        REQUIRE_THROWS(fromYaml("0.1").to<PhaseRangeSpec>());
        REQUIRE_THROWS(fromYaml("-1..1").to<PhaseRangeSpec>());
        REQUIRE_THROWS(fromYaml("0abc..20").to<PhaseRangeSpec>());
        REQUIRE_THROWS(fromYaml("0abc .. 20").to<PhaseRangeSpec>());
        REQUIRE_THROWS(fromYaml("10..4294967296").to<PhaseRangeSpec>());  // uint_max + 1
        REQUIRE_THROWS(fromYaml("4294967296..4294967296").to<PhaseRangeSpec>());
        REQUIRE_THROWS(fromYaml("20..25abc").to<PhaseRangeSpec>());
        REQUIRE_THROWS(fromYaml("-10").to<PhaseRangeSpec>());
        REQUIRE_THROWS(fromYaml("12abc").to<PhaseRangeSpec>());
        REQUIRE_THROWS(fromYaml("{foo}").to<PhaseRangeSpec>());
        REQUIRE_THROWS(fromYaml("").to<PhaseRangeSpec>());
    }
}

}  // namespace
}  // namespace genny
