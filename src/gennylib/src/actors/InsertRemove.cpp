#include <gennylib/actors/InsertRemove.hpp>

#include <memory>

#include <mongocxx/client.hpp>
//#include <mongocxx/database.hpp>
#include <yaml-cpp/yaml.h>

#include "log.hh"
#include <gennylib/context.hpp>
#include <gennylib/value_generators.hpp>

struct genny::actor::InsertRemove::PhaseConfig {
    PhaseConfig(const std::string collection_name,
                std::mt19937_64& rng,
                mongocxx::database db, int thread)
        : database{db}, collection{db[collection_name]}, myDoc(
                                                 bsoncxx::builder::stream::document{} << 
                                                 "_id" <<  thread << bsoncxx::builder::stream::finalize)
    {}
    PhaseConfig(PhaseContext& context, std::mt19937_64& rng, mongocxx::database db, int thread) : PhaseConfig(context.get<std::string>("Collection"), rng, db, thread) 
    {}
    mongocxx::database database;
    mongocxx::collection collection;
    bsoncxx::document::value myDoc;
    
};

void genny::actor::InsertRemove::run() {
    for (auto&& [phase, config]: _loop) {
        for (auto&& _: config) {
            BOOST_LOG_TRIVIAL(info) << " Inserting and then removing";
            {
                auto op = _insertTimer.raii();
                config->collection.insert_one(config->myDoc.view());
            }
            {
                auto op = _removeTimer.raii();
                config->collection.delete_many(config->myDoc.view());
            }

        }
    }
}

genny::actor::InsertRemove::InsertRemove(genny::ActorContext& context, const unsigned int thread)
    : _rng{context.workload().createRNG()},
      _insertTimer{context.timer("insert")}, // TODO: Actor specific names and thread
      _removeTimer{context.timer("remove")},
      _client{std::move(context.client())},
      _loop{context, _rng, (*_client)[context.get<std::string>("Database")], thread}
{}

genny::ActorVector genny::actor::InsertRemove::producer(genny::ActorContext& context) {
    auto out = std::vector<std::unique_ptr<genny::Actor>>{};
    if (context.get<std::string>("Type") != "InsertRemove") {
        return out;
    }
    auto threads = context.get<int>("Threads");
    for (int i = 0; i < threads; ++i) {
        out.push_back(std::make_unique<genny::actor::InsertRemove>(context, i));
    }
    return out;
}

