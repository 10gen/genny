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

#include <vector>

#include <boost/algorithm/string/trim.hpp>
#include <boost/exception/detail/exception_ptr.hpp>
#include <boost/exception/diagnostic_information.hpp>


#include <bsoncxx/builder/stream/document.hpp>
#include <bsoncxx/json.hpp>
#include <bsoncxx/types.hpp>

#include <mongocxx/client.hpp>
#include <mongocxx/stdx.hpp>
#include <mongocxx/uri.hpp>

#include <testlib/ActorHelper.hpp>
#include <testlib/MongoTestFixture.hpp>
#include <testlib/helpers.hpp>
#include <testlib/yamlTest.hpp>
#include <testlib/yamlToBson.hpp>


namespace genny::testing {

struct CrudActorTestCase {

    enum class RunMode { kNormal, kExpectedSetupException, kExpectedRuntimeException };

    static RunMode convertRunMode(YAML::Node tcase) {
        if (tcase["OutcomeData"]) {
            return RunMode::kNormal;
        }
        if (tcase["OutcomeCounts"]) {
            return RunMode::kNormal;
        }
        if (tcase["ExpectedCollectionsExist"]) {
            return RunMode::kNormal;
        }
        if (tcase["Error"]) {
            auto error = tcase["Error"].as<std::string>();
            if (error == "InvalidSyntax") {
                return RunMode::kExpectedSetupException;
            }
            return RunMode::kExpectedRuntimeException;
        }
        BOOST_THROW_EXCEPTION(std::logic_error("Invalid test-case"));
    }

    explicit CrudActorTestCase() = default;

    explicit CrudActorTestCase(YAML::Node node)
        : description{node["Description"].as<std::string>()},
          operations{node["Operations"]},
          runMode{convertRunMode(node)},
          error{node["Error"]},
          tcase{node} {}

    static void assertAfterState(mongocxx::pool::entry& client, YAML::Node tcase) {
        // TODO: handle ExpectedEvents
        // TODO: handle ExpectedCollectionsExist
        if (auto ocd = tcase["OutcomeData"]; ocd) {
            assertOutcomeData(client, ocd);
        }
        if (auto ocounts = tcase["OutcomeCounts"]; ocounts) {
            assertOutcomeCounts(client, ocounts);
        }
    }

    static void assertCount(mongocxx::pool::entry& client,
                            YAML::Node filterYaml,
                            long expected = 1,
                            std::string db = "mydb",
                            std::string coll = "test") {
        auto filter = genny::testing::toDocumentBson(filterYaml);
        INFO("Requiring " << expected << " document" << (expected == 1 ? "" : "s") << " in " << db
                          << "." << coll << " matching " << bsoncxx::to_json(filter.view()));
        long long actual = (*client)[db][coll].count_documents(filter.view());
        REQUIRE(actual == expected);
    }

    static void assertOutcomeData(mongocxx::pool::entry& client, YAML::Node ocdata) {
        for (auto&& filterYaml : ocdata) {
            assertCount(client, filterYaml);
        }
    }

    static void assertOutcomeCounts(mongocxx::pool::entry& client, YAML::Node ocounts) {
        for (auto&& countAssertion : ocounts) {
            assertCount(client, countAssertion["Filter"], countAssertion["Count"].as<long>());
        }
    }

    static YAML::Node build(YAML::Node operations) {
        YAML::Node config = YAML::Load(R"(
          SchemaVersion: 2018-07-01
          Actors:
          - Name: CrudActor
            Type: CrudActor
            Database: mydb
            RetryStrategy:
              ThrowOnFailure: true
            Phases:
            - Repeat: 1
              Collection: test
          )");
        config["Actors"][0]["Phases"][0]["Operations"] = operations;
        return config;
    }

    void doRun() const {
        try {
            auto config = build(operations);
            genny::ActorHelper ah(config, 1, MongoTestFixture::connectionUri().to_string());
            auto client = ah.client();
            dropAllDatabases(client);
            ah.run([](const genny::WorkloadContext& wc) { wc.actors()[0]->run(); });

            if (runMode == RunMode::kExpectedSetupException ||
                runMode == RunMode::kExpectedRuntimeException) {
                FAIL("Expected exception " << error.as<std::string>() << " but not thrown");
            } else {
                assertAfterState(client, tcase);
            }
        } catch (const std::exception& e) {
            if (runMode == RunMode::kExpectedSetupException ||
                runMode == RunMode::kExpectedRuntimeException) {
                auto actual = boost::trim_copy(std::string{e.what()});
                auto expect = boost::trim_copy(error.as<std::string>());
                REQUIRE(actual == expect);
            } else {
                auto diagInfo = boost::diagnostic_information(e);
                INFO(description << "CAUGHT " << diagInfo);
                FAIL(diagInfo);
            }
        }
    }

    void run() const {
        DYNAMIC_SECTION(description) {
            this->doRun();
        }
    }


    YAML::Node error;
    RunMode runMode;
    std::string description;
    YAML::Node operations;
    YAML::Node tcase;
};

}  // namespace genny::testing


namespace YAML {

template <>
struct convert<genny::testing::CrudActorTestCase> {
    static bool decode(YAML::Node node, genny::testing::CrudActorTestCase& out) {
        out = genny::testing::CrudActorTestCase(node);
        return true;
    }
};

}  // namespace YAML


namespace {
TEST_CASE("CrudActor YAML Tests",
          "[standalone][single_node_replset][three_node_replset][CrudActor]") {

    genny::testing::runTestCaseYaml<genny::testing::CrudActorTestCase>(
        "/src/cast_core/test/CrudActorYamlTests.yaml");
}
}  // namespace