#include <metrics/v2/event.hpp>
#include <metrics/metrics.hpp>

namespace genny::metrics::internals::v2 {

/*
 * Static initialization of the global stub interface (which maintains the one global channel).
 *
 * Located in some random cpp file because it can't be initialized in the declaration header.
 */
std::unique_ptr<poplar::PoplarEventCollector::StubInterface> CollectorStubInterface::_stub =
    nullptr;

std::unique_ptr<ThreadObject> ConstructorClass::_thread = nullptr;

}  // namespace genny::metrics::internals::v2
