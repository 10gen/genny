#ifndef HEADER_FF3E897B_C747_468B_AAAC_EA6421DB0902_INCLUDED
#define HEADER_FF3E897B_C747_468B_AAAC_EA6421DB0902_INCLUDED

#include <iostream>

#include <mongocxx/client_session.hpp>

#include <gennylib/PhaseLoop.hpp>

namespace genny::actor {

class HelloWorld : public genny::Actor {

public:
    explicit HelloWorld(ActorContext& context);
    ~HelloWorld() = default;

    static std::string_view defaultName() {
        return "HelloWorld";
    }
    void run() override;

private:
    metrics::Timer _outputTimer;
    metrics::Counter _operations;

    struct PhaseConfig;
    PhaseLoop<PhaseConfig> _loop;
};

}  // namespace genny::actor

#endif  // HEADER_FF3E897B_C747_468B_AAAC_EA6421DB0902_INCLUDED
