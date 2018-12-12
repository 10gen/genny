#include <gennylib/actors/Loader.hpp>

#include <algorithm>
#include <memory>

#include <bsoncxx/json.hpp>
#include <mongocxx/client.hpp>
#include <mongocxx/pool.hpp>
#include <yaml-cpp/yaml.h>

#include "log.hh"
#include <gennylib/context.hpp>
#include <gennylib/value_generators.hpp>


namespace {}  // namespace

struct genny::actor::Loader::PhaseConfig {
    PhaseConfig(PhaseContext& context, std::mt19937_64& rng, mongocxx::pool::entry& client, uint thread)
        : database{(*client)[context.get<std::string>("Database")]},
          // The next line uses integer division. The Remainder is accounted for below.
          numCollections{context.get<uint>("CollectionCount")/context.get<int>("Threads")},
          numDocuments{context.get<uint>("DocumentCount")},
          batchSize{context.get<uint>("BatchSize")},
          documentTemplate{value_generators::makeDoc(context.get("Document"), rng)},
          collectionOffset{numCollections*thread} {
              auto indexNodes = context.get<std::vector<YAML::Node>>("Indexes");
              for (auto indexNode : indexNodes) {
                  indexes.push_back(value_generators::makeDoc(indexNode, rng));
              }
              if (thread == context.get<int>("Threads") - 1) {
                  // Pick up any extra collections left over by the division
                  numCollections += context.get<uint>("CollectionCount") % context.get<uint>("Threads");
              }
          }

    mongocxx::database database;
    uint numCollections;
    uint numDocuments;
    uint batchSize;
    std::unique_ptr<value_generators::DocumentGenerator> documentTemplate;
    std::vector<std::unique_ptr<value_generators::DocumentGenerator>> indexes;
    uint collectionOffset;
};

void genny::actor::Loader::run() {
    for (auto&& [phase, config] : _loop) {
        for (auto&& _ : config) {
            for (uint i = config->collectionOffset; i < config->collectionOffset + config->numCollections; i++) {
                auto collectionName = "Collection" + std::to_string(i);
                auto collection = config->database[collectionName];
                // Insert the documents
                uint remainingInserts = config->numDocuments;
                {
                    auto totalOp = _totalBulkLoadTimer.raii();
                    while (remainingInserts > 0) {
                        // insert the next batch
                        uint numberToInsert = std::max(config->batchSize, remainingInserts);
                        std::vector<bsoncxx::builder::stream::document> docs(numberToInsert);
                        std::vector<bsoncxx::document::view> views;
                        auto newDoc = docs.begin();
                        for (uint j = 0; j < numberToInsert; j++, newDoc++) {
                            views.push_back(config->documentTemplate->view(*newDoc));
                        }
                        {
                            auto individualOp = _individualBulkLoadTimer.raii();
                            auto result = collection.insert_many(views);
                            remainingInserts -= result->inserted_count();
                        }
                    }
                }
                // For each index
                for (auto& index : config->indexes) {
                    // Make the index
                    bsoncxx::builder::stream::document keys;
                    auto keyView = index->view(keys);
                    BOOST_LOG_TRIVIAL(debug) << "Building index " << bsoncxx::to_json(keyView);
                    {
                        auto op = _indexBuildTimer.raii();
                        collection.create_index(keyView);
                    }
                }
            }
            BOOST_LOG_TRIVIAL(info) << "Done with load phase. All documents loaded";
        }
    }
}

genny::actor::Loader::Loader(genny::ActorContext& context, uint thread)
    : Actor(context),
      _rng{context.workload().createRNG()},
      _totalBulkLoadTimer{context.timer("totalBulkInsertTime", Loader::id())},
      _individualBulkLoadTimer{context.timer("individualBulkInsertTime", Loader::id())},
      _indexBuildTimer{context.timer("indexBuildTime", Loader::id())},
      _client{context.client()},
      _loop{context, _rng, _client, thread} {}

genny::ActorVector genny::actor::Loader::producer(genny::ActorContext& context) {
    auto out = std::vector<std::unique_ptr<genny::Actor>>{};
    if (context.get<std::string>("Type") != "Loader") {
        return out;
    }

    // This is currently broken. 
    out.emplace_back(std::make_unique<genny::actor::Loader>(context, 1));
    return out;
}
