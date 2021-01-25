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

#include <gennylib/service.hpp>
#include <mongocxx/client.hpp>
#include <mongocxx/uri.hpp>
#include <bsoncxx/builder/basic/document.hpp>
#include <bsoncxx/builder/basic/kvp.hpp>

namespace genny {

DBService::~DBService() {}

MongoService::MongoService(ServiceUri uri) : _client(mongocxx::uri(uri)) {}

ServiceUri MongoService::uri() { return _client.uri().to_string(); }

bsoncxx::document::value MongoService::runAdminCommand(std::string command) { 
    using bsoncxx::builder::basic::kvp;
    using bsoncxx::builder::basic::make_document;

    auto admin = _client.database("admin");
    return admin.run_command(make_document(kvp(command, 1)));
}

std::unique_ptr<DBService> MongoService::makePeer(ServiceUri uri) {
    return std::make_unique<MongoService>(uri);
}

} // namespace genny

