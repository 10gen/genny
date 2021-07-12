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

#include <cast_core/actors/LoggingActor.hpp>

#include <boost/log/trivial.hpp>

#include <gennylib/Cast.hpp>
#include <gennylib/context.hpp>

namespace genny::actor {

//
// Note this Actor only has a manually-run test-case (LoggingActor_test.cpp).
// Be careful when making changes.
//

struct LoggingActor::PhaseConfig {
    TimeSpec logEvery;
    metrics::clock::time_point started;
    metrics::clock::time_point last;
    unsigned iteration = 0;

    explicit PhaseConfig(PhaseContext& phaseContext)
        : logEvery{phaseContext["LogEvery"].to<TimeSpec>()}, started{metrics::clock::now()} {
        last = started;
        if (phaseContext["Blocking"].to<std::string>() != "None") {
            BOOST_THROW_EXCEPTION(
                InvalidConfigurationException("LoggingActor must have Blocking:None"));
        }
    }
    void report() {
        // Avoid calling now() on most iterations
        if (++iteration % 10000 != 0) {
            return;
        }
        iteration = 0;

        const auto now = metrics::clock::now();
        const auto duration = now - last;
        if (duration >= logEvery.value) {
            BOOST_LOG_TRIVIAL(info) << "Phase still progressing (" <<
                std::chrono::duration_cast<std::chrono::seconds>(now - started).count() << "s)";
            last = now;
        }
    }
};

void LoggingActor::run() {
    for (auto&& config : _loop) {
        for (const auto&& _ : config) {
            config->report();
        }
    }
}

LoggingActor::LoggingActor(genny::ActorContext& context) : Actor{context}, _loop{context} {
    if (context["Threads"].to<size_t>() != 1) {
        BOOST_THROW_EXCEPTION(
            InvalidConfigurationException("LoggignActor must only have Threads:1"));
    }
}

namespace {
auto registerLoggingActor = Cast::registerDefault<LoggingActor>();
}  // namespace
}  // namespace genny::actor
