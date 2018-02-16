#include "yaml-cpp/yaml.h"
#include <bsoncxx/builder/stream/document.hpp>
#include "document.hpp"

#include "operation.hpp"
#pragma once

using namespace std;

namespace mwg {

class find : public operation {
public:
    find(YAML::Node&);
    find() = delete;
    virtual ~find() = default;
    find(const find&) = default;
    find(find&&) = default;
    // Execute the node
    virtual void execute(mongocxx::client&, threadState&) override;

private:
    unique_ptr<document> filter{};
    mongocxx::options::find options{};
};
}
