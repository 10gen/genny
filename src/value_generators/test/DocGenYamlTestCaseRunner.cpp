#include <algorithm>
#include <iosfwd>
#include <iostream>
#include <memory>
#include <string>
#include <vector>

#include <bsoncxx/builder/basic/document.hpp>
#include <bsoncxx/json.hpp>
#include <bsoncxx/types/value.hpp>

#include <yaml-cpp/yaml.h>

#include <testlib/ActorHelper.hpp>
#include <testlib/findRepoRoot.hpp>
#include <testlib/helpers.hpp>
#include <testlib/yamlToBson.hpp>
#include <testlib/yamltest.hpp>

#include <value_generators/DefaultRandom.hpp>
#include <value_generators/DocumentGenerator.hpp>

namespace genny {

namespace {

genny::DefaultRandom rng;


}  // namespace

struct YamlTestCase;

using Result = genny::testing::ResultT<YamlTestCase>;

class YamlTestCase {
public:
    using Result = Result;
    explicit YamlTestCase() = default;

    explicit YamlTestCase(YAML::Node node)
        : _wholeTest{node},
          _name{node["Name"].as<std::string>("No Name")},
          _givenTemplate{node["GivenTemplate"]},
          _thenReturns{node["ThenReturns"]},
          _expectedExceptionMessage{node["ThenThrows"]} {
        if (!_givenTemplate) {
            std::stringstream msg;
            msg << "Need GivenTemplate in '" << testing::toString(node) << "'";
            throw std::invalid_argument(msg.str());
        }
        if (_thenReturns && _expectedExceptionMessage) {
            std::stringstream msg;
            msg << "Can't have ThenReturns and ThenThrows in '" << testing::toString(node) << "'";
            throw std::invalid_argument(msg.str());
        }
        if (_thenReturns) {
            if (!_thenReturns.IsSequence()) {
                std::stringstream msg;
                msg << "ThenReturns must be list in '" << testing::toString(node) << "'";
                throw std::invalid_argument(msg.str());
            }
            _runMode = RunMode::kExpectReturn;
        } else {
            if (!_expectedExceptionMessage) {
                std::stringstream msg;
                msg << "Need ThenThrows if no ThenReturns in '" << testing::toString(node) << "'";
                throw std::invalid_argument(msg.str());
            }
            _runMode = RunMode::kExpectException;
        }
    }

    genny::Result run() const {
        genny::Result out{*this};
        if (_runMode == RunMode::kExpectException) {
            try {
                genny::DocumentGenerator(this->_givenTemplate, rng);
                out.expectedExceptionButNotThrown();
            } catch (const std::exception& x) {
                out.expectEqual("InvalidValueGeneratorSyntax",
                                this->_expectedExceptionMessage.as<std::string>());
            }
            return out;
        }

        auto docGen = genny::DocumentGenerator(this->_givenTemplate, rng);
        for (const auto&& nextValue : this->_thenReturns) {
            auto expected = testing::toDocumentBson(nextValue);
            auto actual = docGen();
            out.expectEqual(expected.view(), actual.view());
        }
        return out;
    }

    const std::string name() const {
        return _name;
    }

    const YAML::Node givenTemplate() const {
        return _givenTemplate;
    }

private:
    enum class RunMode {
        kExpectException,
        kExpectReturn,
    };

    RunMode _runMode = RunMode::kExpectException;
    std::string _name;

    // really only "need" _wholeTest but others are here for convenience
    YAML::Node _wholeTest;
    YAML::Node _givenTemplate;
    YAML::Node _thenReturns;
    YAML::Node _expectedExceptionMessage;
};


std::ostream& operator<<(std::ostream& out, const std::vector<Result>& results) {
    out << std::endl;
    for (auto&& result : results) {
        out << "- Name: " << result.testCase().name() << std::endl;
        out << "  GivenTemplate: " << testing::toString(result.testCase().givenTemplate()) << std::endl;
        out << "  ThenReturns: " << std::endl;
        for (auto&& [expect, actual] : result.expectedVsActual()) {
            out << "    - " << actual << std::endl;
        }
    }
    return out;
}

}  // namespace genny


namespace YAML {

template <>
struct convert<genny::testing::YamlTests<genny::YamlTestCase>> {
    static Node encode(const genny::testing::YamlTests<genny::YamlTestCase>& rhs) {
        return {};
    }

    static bool decode(const Node& node, genny::testing::YamlTests<genny::YamlTestCase>& rhs) {
        rhs = genny::testing::YamlTests<genny::YamlTestCase>(node);
        return true;
    }
};

template <>
struct convert<genny::YamlTestCase> {
    static Node encode(const genny::YamlTestCase& rhs) {
        return {};
    }

    static bool decode(const Node& node, genny::YamlTestCase& rhs) {
        rhs = genny::YamlTestCase(node);
        return true;
    }
};

}  // namespace YAML


namespace {

TEST_CASE("YAML Tests") {
    try {
        const auto file =
            genny::findRepoRoot() + "/src/value_generators/test/DocumentGeneratorTestCases.yml";
        const auto yaml = YAML::LoadFile(file);
        auto tests = yaml.as<genny::testing::YamlTests<genny::YamlTestCase>>();
        std::vector<genny::Result> results = tests.run();
        if (!results.empty()) {
            std::stringstream msg;
            msg << results;
            WARN(msg.str());
        }
        REQUIRE(results.empty());
    } catch (const std::exception& ex) {
        WARN(ex.what());
        throw;
    }
}

}  // namespace