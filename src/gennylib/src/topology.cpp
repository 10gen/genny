// Copyright 2021-present MongoDB Inc.
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

#include <string>
#include <optional>
#include <mongocxx/database.hpp>
#include <bsoncxx/builder/basic/document.hpp>
#include <bsoncxx/builder/basic/kvp.hpp>
#include <bsoncxx/json.hpp>
#include <bsoncxx/types.hpp>
#include <boost/log/trivial.hpp>

#include <gennylib/topology.hpp>

namespace genny {

using bsoncxx::builder::basic::kvp;
using bsoncxx::builder::basic::make_document;

// Used to make sure we set a proper URI for connections we make.
mongocxx::uri Topology::nameToUri(const mongocxx::uri& uri, const std::string& name) {
    auto uriStr = uri.to_string();
    auto strippedName = name;
    if (name.find('/') != std::string::npos) {
        strippedName = name.substr(name.find('/') + 1, name.find(','));
    }

    std::string output;
    if (uriStr.find('@') != std::string::npos) {
        // The uri has a username:password field.
        size_t start = uriStr.find("@");
        size_t end = uriStr.find("/", start + 1);
        output = uriStr.erase(start + 1, end - start - 1);
        output = output.insert(start + 1, strippedName);
    } else {
        size_t start = uriStr.find("//");
        size_t end = uriStr.find("/", start + 2);
        output = uriStr.erase(start + 2, end - start - 2);
        output = output.insert(start + 2, strippedName);
    }

    return mongocxx::uri(output);
}

void Topology::getDataMemberConnectionStrings(mongocxx::client& client) {
    auto admin = client.database("admin");
    auto res = admin.run_command(make_document(kvp("isMaster", 1)));
    if (!res.view()["setName"]) {
        std::unique_ptr<MongodDescription> desc = std::make_unique<MongodDescription>();
        desc->mongodUri = nameToUri(_baseUri, client.uri().to_string()).to_string();
        this->_topology.reset(desc.release());
        return;
    }

    auto primary = res.view()["primary"];

    std::unique_ptr<ReplSetDescription> desc = std::make_unique<ReplSetDescription>();
    desc->primaryUri = nameToUri(_baseUri, std::string(primary.get_utf8().value)).to_string();

    auto hosts = res.view()["hosts"];
    if (hosts && hosts.type() == bsoncxx::type::k_array) {
        bsoncxx::array::view hosts_view = hosts.get_array();
        for (auto member : hosts_view) {
            MongodDescription memberDesc;
            memberDesc.mongodUri = nameToUri(_baseUri, std::string(member.get_utf8().value)).to_string();
            desc->nodes.push_back(memberDesc);
        }
    }

    // The "passives" field contains the list of unelectable (priority=0) secondaries
    // and is omitted from the server's response when there are none.
    auto passives = res.view()["passives"];
    if (passives && passives.type() == bsoncxx::type::k_array) {
        bsoncxx::array::view passives_view = passives.get_array();
        for (auto member : passives_view) {
            MongodDescription memberDesc;
            memberDesc.mongodUri = nameToUri(_baseUri, std::string(member.get_utf8().value)).to_string();
            desc->nodes.push_back(memberDesc);
        }
    }

    this->_topology.reset(desc.release());
}

void Topology::findConnectedNodesViaMongos(mongocxx::client& client) {
    class ReplSetRetriever : public TopologyVisitor {
    public:
        void visitReplSetDescriptionPre(const ReplSetDescription& desc) {
            replSet = desc; 
        }
        ReplSetDescription replSet;
    };

    auto admin = client.database("admin");
    std::unique_ptr<ShardedDescription> desc = std::make_unique<ShardedDescription>();
    ReplSetRetriever retriever;

    // Config Server
    auto shardMap = admin.run_command(make_document(kvp("getShardMap", 1)));
    std::string configServerConn(shardMap.view()["map"]["config"].get_utf8().value);
    Topology configTopology(nameToUri(_baseUri, configServerConn));
    configTopology.accept(retriever);
    desc->configsvr = retriever.replSet;
    desc->configsvr.configsvr = true;

    // Shards
    auto shardListRes = admin.run_command(make_document(kvp("listShards", 1)));
    bsoncxx::array::view shards = shardListRes.view()["shards"].get_array();
    for (auto shard : shards) {
        std::string shardConn(shard["host"].get_utf8().value);
        Topology shardTopology(nameToUri(_baseUri, shardConn));
        shardTopology.accept(retriever);
        desc->shards.push_back(retriever.replSet);
    }

    // Mongos
    for (auto host : _baseUri.hosts()) {
        std::string hostName = host.name + ":" + std::to_string(host.port);
        MongosDescription mongosDesc;
        mongosDesc.mongosUri = nameToUri(_baseUri, host.name).to_string();
        desc->mongoses.push_back(mongosDesc);
    }

    this->_topology.reset(desc.release());
}

void Topology::update(mongocxx::client& client) {
    auto admin = client.database("admin");

    bool isMongos = false;
    auto res = admin.run_command(make_document(kvp("isMaster", 1)));
    auto msg = res.view()["msg"];
    if (msg && msg.type() == bsoncxx::type::k_utf8) {
        isMongos = msg.get_utf8().value == "isdbgrid";
    }

    if (isMongos) {
        findConnectedNodesViaMongos(client);
    } else {
        getDataMemberConnectionStrings(client);
    }

}

}  // namespace genny
