#include <gennylib/actors/HelloWorld.hpp>

#include <string>

#include "log.hh"

#include <gennylib/Cast.hpp>

struct genny::actor::HelloWorld::PhaseConfig {
    std::string message;
    explicit PhaseConfig(PhaseContext& context)
        : message{context.get<std::string, false>("Message").value_or("Hello, World!")} {}
};

void genny::actor::HelloWorld::run() {
    for (auto&& [phase, config] : _loop) {
        for (auto _ : config) {
            auto op = this->_outputTimer.raii();
            BOOST_LOG_TRIVIAL(info) << config->message;
        }
    }
}
genny::actor::HelloWorld::HelloWorld(genny::ActorContext& context, const unsigned int thread)
    : _outputTimer{context.timer("output", thread)},
      _operations{context.counter("operations", thread)},
      _loop{context} {}

namespace {
auto registerHelloWorld =
    genny::Cast::makeDefaultRegistration<genny::actor::HelloWorld>("HelloWorld");
}
