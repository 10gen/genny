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

#include <bsoncxx/json.hpp>
#include <bsoncxx/builder/stream/document.hpp>

#include <boost/exception/diagnostic_information.hpp>

#include <yaml-cpp/yaml.h>

#include <testlib/MongoTestFixture.hpp>
#include <testlib/ActorHelper.hpp>
#include <testlib/helpers.hpp>

#include <gennylib/context.hpp>

namespace {
using namespace genny::testing;

TEST_CASE_METHOD(MongoTestFixture, "CommitLatency",
          "[single_node_replset][three_node_replset][sharded][CommitLatency]") {

    dropAllDatabases();
    auto db = client.database("mydb");

    YAML::Node config = YAML::Load(R"(
        SchemaVersion: 2018-07-01
        Database: mydb
        Collection: &Collection CommitLatency
        Actors:
        - Name: CommitLatency
          Type: CommitLatency
          Threads: 1
          Repeat: 500
          Database: test
          Phases:
           - WriteConcern:
               Level: 0
             ReadConcern:
               Level: local
             ReadPreference:
               ReadMode: primary
           - WriteConcern:
               Level: majority
             ReadConcern:
               Level: snapshot
             ReadPreference:
               ReadMode: primary
             Transaction: True         # Implies Session

    )");


    SECTION("With and without transactions.") {
        try {
            // First insert 2 documents: [{_id: 1, n: 100}, {_id: 2, n: 100}]
            auto coll = db["CommitLatency"];
            auto builder = bsoncxx::builder::stream::document{};
            builder << "_id" << 1
                    << "n" << 100
                    << bsoncxx::builder::stream::finalize;
            coll.insert_one(builder.view());
            auto builder2 = bsoncxx::builder::stream::document{};
            builder2 << "_id" << 1
                    << "n" << 100
                    << bsoncxx::builder::stream::finalize;
            coll.insert_one(builder2.view());

            genny::ActorHelper ah(config, 1, MongoTestFixture::connectionUri().to_string());
            ah.run([](const genny::WorkloadContext& wc) { wc.actors()[0]->run(); });

            auto count = db.collection("CommitLatency").estimated_document_count();
            REQUIRE(count == 2);
        } catch (const std::exception& e) {
            auto diagInfo = boost::diagnostic_information(e);
            INFO("CAUGHT " << diagInfo);
            FAIL(diagInfo);
        }
    }
}
} // namespace