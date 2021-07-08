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

#include <cast_core/actors/CrudActor.hpp>
#include <cast_core/actors/OptionsConversion.hpp>

#include <chrono>
#include <memory>
#include <utility>

#include <mongocxx/client.hpp>
#include <mongocxx/collection.hpp>

#include <boost/log/trivial.hpp>
#include <boost/throw_exception.hpp>

#include <bsoncxx/json.hpp>
#include <bsoncxx/string/to_string.hpp>

#include <gennylib/Cast.hpp>
#include <gennylib/MongoException.hpp>
#include <gennylib/context.hpp>
#include <gennylib/conventions.hpp>

using BsonView = bsoncxx::document::view;
using CrudActor = genny::actor::CrudActor;
using bsoncxx::type;

namespace {

using namespace genny;

enum class ThrowMode {
    kSwallow,
    kRethrow,
};

ThrowMode decodeThrowMode(const Node& operation, PhaseContext& phaseContext) {
    static const char* key = "ThrowOnFailure";

    bool throwOnFailure =
        operation[key] ? operation[key].to<bool>() : phaseContext[key].maybe<bool>().value_or(true);
    return throwOnFailure ? ThrowMode::kRethrow : ThrowMode::kSwallow;
}

bsoncxx::document::value emptyDoc = bsoncxx::from_json("{}");


// A large number of subclasses have
// - metrics::Operation
// - mongocxx::collection
// - bool (_onSession)
// it may make sense to add a CollectionAwareBaseOperation
// or something intermediate class to eliminate some
// duplication.
struct BaseOperation {
    ThrowMode throwMode;

    using MaybeDoc = std::optional<bsoncxx::document::value>;

    explicit BaseOperation(PhaseContext& phaseContext, const Node& operation)
        : throwMode{decodeThrowMode(operation, phaseContext)} {}

    template <typename F>
    void doBlock(metrics::Operation& op, F&& f) {
        MaybeDoc info = std::nullopt;
        auto ctx = op.start();
        try {
            info = f(ctx);
        } catch (const mongocxx::operation_exception& x) {
            if (throwMode == ThrowMode::kRethrow) {
                ctx.failure();
                BOOST_THROW_EXCEPTION(MongoException(x, info ? info->view() : emptyDoc.view()));
            }
        }
        ctx.success();
    }

    virtual void run(mongocxx::client_session& session) = 0;
    virtual ~BaseOperation() = default;
};

using OpCallback = std::function<std::unique_ptr<BaseOperation>(const Node&,
                                                                bool,
                                                                mongocxx::collection,
                                                                metrics::Operation,
                                                                PhaseContext& context,
                                                                ActorId id)>;

std::unordered_map<std::string, OpCallback&> getOpConstructors();

struct WriteOperation : public BaseOperation {
    WriteOperation(PhaseContext& phaseContext, const Node& operation)
        : BaseOperation(phaseContext, operation) {}
    virtual mongocxx::model::write getModel() = 0;
};

using WriteOpCallback = std::function<std::unique_ptr<WriteOperation>(
    const Node&, bool, mongocxx::collection, metrics::Operation, PhaseContext&, ActorId)>;

// Not technically "crud" but it was easy to add and made
// a few of the tests easier to write (by allowing inserts
// to fail due to index constraint-violations.
struct CreateIndexOperation : public BaseOperation {
    mongocxx::collection _collection;
    metrics::Operation _operation;
    DocumentGenerator _keys;
    DocumentGenerator _indexOptions;
    mongocxx::options::index_view _operationOptions;

    bool _onSession;

    CreateIndexOperation(const Node& opNode,
                         bool onSession,
                         mongocxx::collection collection,
                         metrics::Operation operation,
                         PhaseContext& context,
                         ActorId id)
        : BaseOperation(context, opNode),
          _collection(std::move(collection)),
          _operation{operation},
          _onSession{onSession},
          _keys{opNode["Keys"].to<DocumentGenerator>(context, id)},
          _indexOptions{opNode["IndexOptions"].to<DocumentGenerator>(context, id)} {}


    void run(mongocxx::client_session& session) override {
        auto keys = _keys();
        auto indexOptions = _indexOptions();

        this->doBlock(_operation, [&](metrics::OperationContext& ctx) {
            _onSession
                ? _collection.create_index(
                      session, keys.view(), indexOptions.view(), _operationOptions)
                : _collection.create_index(keys.view(), indexOptions.view(), _operationOptions);
            return std::make_optional(std::move(keys));
        });
    }
};

struct InsertOneOperation : public WriteOperation {
    InsertOneOperation(const Node& opNode,
                       bool onSession,
                       mongocxx::collection collection,
                       metrics::Operation operation,
                       PhaseContext& context,
                       ActorId id)
        : WriteOperation(context, opNode),
          _onSession{onSession},
          _collection{std::move(collection)},
          _operation{operation},
          _options{opNode["OperationOptions"].maybe<mongocxx::options::insert>().value_or(
              mongocxx::options::insert{})},
          _document{opNode["Document"].to<DocumentGenerator>(context, id)} {}

    mongocxx::model::write getModel() override {
        auto document = _document();
        return mongocxx::model::insert_one{std::move(document)};
    }

    void run(mongocxx::client_session& session) override {
        auto document = _document();
        auto size = document.view().length();

        this->doBlock(_operation, [&](metrics::OperationContext& ctx) {
            (_onSession) ? _collection.insert_one(session, document.view(), _options)
                         : _collection.insert_one(document.view(), _options);
            ctx.addDocuments(1);
            ctx.addBytes(size);
            return std::make_optional(std::move(document));
        });
    }

private:
    bool _onSession;
    mongocxx::collection _collection;
    DocumentGenerator _document;
    metrics::Operation _operation;
    mongocxx::options::insert _options;
};


struct UpdateOneOperation : public WriteOperation {
    UpdateOneOperation(const Node& opNode,
                       bool onSession,
                       mongocxx::collection collection,
                       metrics::Operation operation,
                       PhaseContext& context,
                       ActorId id)
        : WriteOperation(context, opNode),
          _onSession{onSession},
          _collection{std::move(collection)},
          _operation{operation},
          _filter{opNode["Filter"].to<DocumentGenerator>(context, id)},
          _update{opNode["Update"].to<DocumentGenerator>(context, id)},
          _options{opNode["OperationOptions"].maybe<mongocxx::options::update>().value_or(
              mongocxx::options::update{})} {}

    mongocxx::model::write getModel() override {
        auto filter = _filter();
        auto update = _update();
        return mongocxx::model::update_one{std::move(filter), std::move(update)};
    }

    void run(mongocxx::client_session& session) override {
        auto filter = _filter();
        auto update = _update();
        this->doBlock(_operation, [&](metrics::OperationContext& ctx) {
            auto result = (_onSession)
                ? _collection.update_one(session, filter.view(), update.view(), _options)
                : _collection.update_one(filter.view(), update.view(), _options);
            if (result) {
                ctx.addDocuments(result->modified_count());
            }
            // pick an 'info' document...either one makes sense
            return std::make_optional(std::move(update));
        });
    }

private:
    bool _onSession;
    mongocxx::collection _collection;
    DocumentGenerator _filter;
    DocumentGenerator _update;
    metrics::Operation _operation;
    mongocxx::options::update _options;
};

struct UpdateManyOperation : public WriteOperation {
    UpdateManyOperation(const Node& opNode,
                        bool onSession,
                        mongocxx::collection collection,
                        metrics::Operation operation,
                        PhaseContext& context,
                        ActorId id)
        : WriteOperation(context, opNode),
          _onSession{onSession},
          _collection{std::move(collection)},
          _operation{operation},
          _filter{opNode["Filter"].to<DocumentGenerator>(context, id)},
          _update{opNode["Update"].to<DocumentGenerator>(context, id)} {}

    mongocxx::model::write getModel() override {
        auto filter = _filter();
        auto update = _update();
        return mongocxx::model::update_many{std::move(filter), std::move(update)};
    }

    void run(mongocxx::client_session& session) override {
        auto filter = _filter();
        auto update = _update();

        this->doBlock(_operation, [&](metrics::OperationContext& ctx) {
            auto result = (_onSession)
                ? _collection.update_many(session, filter.view(), update.view(), _options)
                : _collection.update_many(filter.view(), update.view(), _options);
            if (result) {
                ctx.addDocuments(result->modified_count());
            }
            return std::make_optional(std::move(update));
        });
    }

private:
    bool _onSession;
    mongocxx::collection _collection;
    DocumentGenerator _filter;
    DocumentGenerator _update;
    metrics::Operation _operation;
    mongocxx::options::update _options;
};

struct DeleteOneOperation : public WriteOperation {
    DeleteOneOperation(const Node& opNode,
                       bool onSession,
                       mongocxx::collection collection,
                       metrics::Operation operation,
                       PhaseContext& context,
                       ActorId id)
        : WriteOperation(context, opNode),
          _onSession{onSession},
          _collection{std::move(collection)},
          _operation{operation},
          _filter(opNode["Filter"].to<DocumentGenerator>(context, id)),
          _options{opNode["OperationOptions"].maybe<mongocxx::options::delete_options>().value_or(
              mongocxx::options::delete_options{})} {}

    mongocxx::model::write getModel() override {
        auto filter = _filter();
        return mongocxx::model::delete_one{std::move(filter)};
    }

    void run(mongocxx::client_session& session) override {
        auto filter = _filter();
        this->doBlock(_operation, [&](metrics::OperationContext& ctx) {
            auto result = (_onSession) ? _collection.delete_one(session, filter.view(), _options)
                                       : _collection.delete_one(filter.view(), _options);
            if (result) {
                ctx.addDocuments(result->deleted_count());
            }
            return std::make_optional(std::move(filter));
        });
    }

private:
    bool _onSession;
    mongocxx::collection _collection;
    DocumentGenerator _filter;
    metrics::Operation _operation;
    mongocxx::options::delete_options _options;
};

struct DeleteManyOperation : public WriteOperation {
    DeleteManyOperation(const Node& opNode,
                        bool onSession,
                        mongocxx::collection collection,
                        metrics::Operation operation,
                        PhaseContext& context,
                        ActorId id)
        : WriteOperation(context, opNode),
          _onSession{onSession},
          _collection{std::move(collection)},
          _operation{operation},
          _filter{opNode["Filter"].to<DocumentGenerator>(context, id)} {}

    mongocxx::model::write getModel() override {
        auto filter = _filter();
        return mongocxx::model::delete_many{std::move(filter)};
    }

    void run(mongocxx::client_session& session) override {
        auto filter = _filter();
        this->doBlock(_operation, [&](metrics::OperationContext& ctx) {
            auto results = (_onSession) ? _collection.delete_many(session, filter.view(), _options)
                                        : _collection.delete_many(filter.view(), _options);
            if (results) {
                ctx.addDocuments(results->deleted_count());
            }
            return std::make_optional(std::move(filter));
        });
    }

private:
    bool _onSession;
    mongocxx::collection _collection;
    DocumentGenerator _filter;
    metrics::Operation _operation;
    mongocxx::options::delete_options _options;
};

struct ReplaceOneOperation : public WriteOperation {
    ReplaceOneOperation(const Node& opNode,
                        bool onSession,
                        mongocxx::collection collection,
                        metrics::Operation operation,
                        PhaseContext& context,
                        ActorId id)
        : WriteOperation(context, opNode),
          _onSession{onSession},
          _collection{std::move(collection)},
          _operation{operation},
          _filter{opNode["Filter"].to<DocumentGenerator>(context, id)},
          _replacement{opNode["Replacement"].to<DocumentGenerator>(context, id)} {}

    mongocxx::model::write getModel() override {
        auto filter = _filter();
        auto replacement = _replacement();
        return mongocxx::model::replace_one{std::move(filter), std::move(replacement)};
    }

    void run(mongocxx::client_session& session) override {
        auto filter = _filter();
        auto replacement = _replacement();
        auto size = replacement.view().length();

        this->doBlock(_operation, [&](metrics::OperationContext& ctx) {
            auto result = (_onSession)
                ? _collection.replace_one(session, filter.view(), replacement.view(), _options)
                : _collection.replace_one(filter.view(), replacement.view(), _options);

            if (result) {
                ctx.addDocuments(result->modified_count());
            }
            ctx.addBytes(size);

            return std::make_optional(std::move(replacement));
        });
    }

private:
    bool _onSession;
    mongocxx::collection _collection;
    DocumentGenerator _filter;
    DocumentGenerator _replacement;
    metrics::Operation _operation;
    mongocxx::options::replace _options;
};

template <class P, class C, class O>
C baseCallback = [](const Node& opNode,
                    bool onSession,
                    mongocxx::collection collection,
                    metrics::Operation operation,
                    PhaseContext& context,
                    ActorId id) -> std::unique_ptr<P> {
    return std::make_unique<O>(opNode, onSession, collection, operation, context, id);
};

// Maps the WriteCommand name to the constructor of the designated Operation struct.
std::unordered_map<std::string, WriteOpCallback&> bulkWriteConstructors = {
    {"insertOne", baseCallback<WriteOperation, WriteOpCallback, InsertOneOperation>},
    {"updateOne", baseCallback<WriteOperation, WriteOpCallback, UpdateOneOperation>},
    {"deleteOne", baseCallback<WriteOperation, WriteOpCallback, DeleteOneOperation>},
    {"deleteMany", baseCallback<WriteOperation, WriteOpCallback, DeleteManyOperation>},
    {"replaceOne", baseCallback<WriteOperation, WriteOpCallback, ReplaceOneOperation>},
    {"updateMany", baseCallback<WriteOperation, WriteOpCallback, UpdateManyOperation>}};

/**
 * Example usage:
 *    Operations:
 *    - OperationName: bulkWrite
 *      OperationCommand:
 *        WriteOperations:
 *        - WriteCommand: insertOne
 *          Document: { a: 1 }
 *        - WriteCommand: updateOne
 *          Filter: { a: 1 }
 *          Update: { $set: { a: 5 } }
 *        Options:
 *          Ordered: true
 *          WriteConcern:
 *            Level: majority
 *            Journal: true
 *        OnSession: false
 */
struct BulkWriteOperation : public BaseOperation {

    BulkWriteOperation(const Node& opNode,
                       bool onSession,
                       mongocxx::collection collection,
                       metrics::Operation operation,
                       PhaseContext& context,
                       ActorId id)
        : BaseOperation(context, opNode),
          _onSession{onSession},
          _collection{std::move(collection)},
          _operation{operation} {
        auto& writeOpsYaml = opNode["WriteOperations"];
        if (!writeOpsYaml.isSequence()) {
            BOOST_THROW_EXCEPTION(InvalidConfigurationException(
                "'bulkWrite' requires a 'WriteOperations' node of sequence type."));
        }
        for (const auto& [k, writeOp] : writeOpsYaml) {
            createOps(writeOp, context, id);
        }
        if (opNode["Options"]) {
            _options = opNode["Options"].to<mongocxx::options::bulk_write>();
        }
    }

    void createOps(const Node& writeOp, PhaseContext& context, ActorId id) {
        auto writeCommand = writeOp["WriteCommand"].to<std::string>();
        auto writeOpConstructor = bulkWriteConstructors.find(writeCommand);
        if (writeOpConstructor == bulkWriteConstructors.end()) {
            BOOST_THROW_EXCEPTION(InvalidConfigurationException(
                "WriteCommand '" + writeCommand + "' not supported in bulkWrite operations."));
        }
        auto createWriteOp = writeOpConstructor->second;
        _writeOps.push_back(
            createWriteOp(writeOp, _onSession, _collection, _operation, context, id));
    }

    void run(mongocxx::client_session& session) override {
        auto bulk = (_onSession) ? _collection.create_bulk_write(session, _options)
                                 : _collection.create_bulk_write(_options);
        for (auto&& op : _writeOps) {
            auto writeModel = op->getModel();
            bulk.append(writeModel);
        }

        this->doBlock(_operation, [&](metrics::OperationContext& ctx) {
            auto result = bulk.execute();

            size_t docs = 0;
            if (result) {
                docs += result->modified_count();
                docs += result->deleted_count();
                docs += result->inserted_count();
                docs += result->upserted_count();
                ctx.addDocuments(docs);
            }
            // no viable option
            return std::nullopt;
        });
    }

private:
    std::vector<std::unique_ptr<WriteOperation>> _writeOps;
    bool _onSession;
    mongocxx::collection _collection;
    mongocxx::options::bulk_write _options;
    metrics::Operation _operation;
};

/**
 * Example usage:
 *    Operations:
 *    - OperationName: count
 *      OperationCommand:
 *        Filter: { a : 1 }
 *        Options:
 *          Limit: 5
 *        OnSession: false
 */
struct CountDocumentsOperation : public BaseOperation {
    CountDocumentsOperation(const Node& opNode,
                            bool onSession,
                            mongocxx::collection collection,
                            metrics::Operation operation,
                            PhaseContext& context,
                            ActorId id)
        : BaseOperation(context, opNode),
          _onSession{onSession},
          _collection{std::move(collection)},
          _operation{operation},
          _filter{opNode["Filter"].to<DocumentGenerator>(context, id)} {
        if (opNode["Options"]) {
            _options = opNode["Options"].to<mongocxx::options::count>();
        }
    }

    void run(mongocxx::client_session& session) override {
        auto filter = _filter();

        this->doBlock(_operation, [&](metrics::OperationContext& ctx) {
            auto count = (_onSession)
                ? _collection.count_documents(session, filter.view(), _options)
                : _collection.count_documents(filter.view(), _options);
            ctx.addDocuments(count);
            return std::make_optional(std::move(filter));
        });
    }


private:
    bool _onSession;
    mongocxx::collection _collection;
    mongocxx::options::count _options;
    DocumentGenerator _filter;
    metrics::Operation _operation;
};

struct EstimatedDocumentCountOperation : public BaseOperation {
    EstimatedDocumentCountOperation(const Node& opNode,
                                    bool onSession,
                                    mongocxx::collection collection,
                                    metrics::Operation operation,
                                    PhaseContext& context,
                                    ActorId id)
        : BaseOperation(context, opNode),
          _onSession{onSession},
          _collection{std::move(collection)},
          _operation{operation} {
        if (opNode["Options"]) {
            _options = opNode["Options"].to<mongocxx::options::estimated_document_count>();
        }
        if (onSession) {
            // Technically the docs don't explicitly mention this but the C++ driver doesn't
            // let you specify a session. In fact count operations operations don't work inside
            // a transaction so maybe CountOperation should fail in the same way if _onSession
            // is set?
            //
            // https://docs.mongodb.com/manual/reference/method/db.collection.estimatedDocumentCount/
            BOOST_THROW_EXCEPTION(
                InvalidConfigurationException("Cannot run EstimatedDocumentCount on a session"));
        }
    }

    void run(mongocxx::client_session& session) override {
        this->doBlock(_operation, [&](metrics::OperationContext& ctx) {
            auto count = _collection.estimated_document_count(_options);
            ctx.addDocuments(count);
            return std::nullopt;
        });
    }


private:
    bool _onSession;
    mongocxx::collection _collection;
    mongocxx::options::estimated_document_count _options;
    metrics::Operation _operation;
};

struct FindOperation : public BaseOperation {
    FindOperation(const Node& opNode,
                  bool onSession,
                  mongocxx::collection collection,
                  metrics::Operation operation,
                  PhaseContext& context,
                  ActorId id)
        : BaseOperation(context, opNode),
          _onSession{onSession},
          _collection{std::move(collection)},
          _operation{operation},
          _filter{opNode["Filter"].to<DocumentGenerator>(context, id)} {
        if (opNode["Options"]) {
            _options = opNode["Options"].to<mongocxx::options::find>();
        }
    }

    void run(mongocxx::client_session& session) override {
        auto filter = _filter();
        this->doBlock(_operation, [&](metrics::OperationContext& ctx) {
            auto cursor = (_onSession) ? _collection.find(session, filter.view(), _options)
                                       : _collection.find(filter.view(), _options);
            for (auto&& doc : cursor) {
                ctx.addDocuments(1);
                ctx.addBytes(doc.length());
            }
            return std::make_optional(std::move(filter));
        });
    }


private:
    bool _onSession;
    mongocxx::collection _collection;
    mongocxx::options::find _options;
    DocumentGenerator _filter;
    metrics::Operation _operation;
};

struct FindOneOperation : public BaseOperation {
    FindOneOperation(const Node& opNode,
                     bool onSession,
                     mongocxx::collection collection,
                     metrics::Operation operation,
                     PhaseContext& context,
                     ActorId id)
        : BaseOperation(context, opNode),
          _onSession{onSession},
          _collection{std::move(collection)},
          _operation{operation},
          _filter{opNode["Filter"].to<DocumentGenerator>(context, id)} {
        if (opNode["Options"]) {
            _options = opNode["Options"].to<mongocxx::options::find>();
        }
    }

    void run(mongocxx::client_session& session) override {
        auto filter = _filter();
        this->doBlock(_operation, [&](metrics::OperationContext& ctx) {
            auto result = (_onSession) ? _collection.find_one(session, filter.view(), _options)
                                       : _collection.find_one(filter.view(), _options);
            if (result) {
                ctx.addDocuments(1);
                ctx.addBytes(result->view().length());
            }
            return std::make_optional(std::move(filter));
        });
    }

private:
    bool _onSession;
    mongocxx::collection _collection;
    mongocxx::options::find _options;
    DocumentGenerator _filter;
    metrics::Operation _operation;
};

struct FindOneAndUpdateOperation : public BaseOperation {
    FindOneAndUpdateOperation(const Node& opNode,
                              bool onSession,
                              mongocxx::collection collection,
                              metrics::Operation operation,
                              PhaseContext& context,
                              ActorId id)
        : BaseOperation(context, opNode),
          _onSession{onSession},
          _collection{std::move(collection)},
          _operation{operation},
          _filter{opNode["Filter"].to<DocumentGenerator>(context, id)},
          _update{opNode["Update"].to<DocumentGenerator>(context, id)} {}

    void run(mongocxx::client_session& session) override {
        auto filter = _filter();
        auto update = _update();
        this->doBlock(_operation, [&](metrics::OperationContext& ctx) {
            auto result = (_onSession)
                ? _collection.find_one_and_update(session, filter.view(), update.view(), _options)
                : _collection.find_one_and_update(filter.view(), update.view(), _options);
            if (result) {
                ctx.addDocuments(1);
                ctx.addBytes(result->view().length());
            }
            return std::make_optional(std::move(filter));
        });
    }

private:
    bool _onSession;
    mongocxx::collection _collection;
    mongocxx::options::find_one_and_update _options;
    DocumentGenerator _filter;
    DocumentGenerator _update;
    metrics::Operation _operation;
};

struct FindOneAndDeleteOperation : public BaseOperation {
    FindOneAndDeleteOperation(const Node& opNode,
                              bool onSession,
                              mongocxx::collection collection,
                              metrics::Operation operation,
                              PhaseContext& context,
                              ActorId id)
        : BaseOperation(context, opNode),
          _onSession{onSession},
          _collection{std::move(collection)},
          _operation{operation},
          _filter{opNode["Filter"].to<DocumentGenerator>(context, id)} {}

    void run(mongocxx::client_session& session) override {
        auto filter = _filter();
        this->doBlock(_operation, [&](metrics::OperationContext& ctx) {
            auto result = (_onSession)
                ? _collection.find_one_and_delete(session, filter.view(), _options)
                : _collection.find_one_and_delete(filter.view(), _options);
            if (result) {
                ctx.addDocuments(1);
                ctx.addBytes(result->view().length());
            }
            return std::make_optional(std::move(filter));
        });
    }

private:
    bool _onSession;
    mongocxx::collection _collection;
    mongocxx::options::find_one_and_delete _options;
    DocumentGenerator _filter;
    metrics::Operation _operation;
};

struct FindOneAndReplaceOperation : public BaseOperation {
    FindOneAndReplaceOperation(const Node& opNode,
                               bool onSession,
                               mongocxx::collection collection,
                               metrics::Operation operation,
                               PhaseContext& context,
                               ActorId id)
        : BaseOperation(context, opNode),
          _onSession{onSession},
          _collection{std::move(collection)},
          _operation{operation},
          _filter{opNode["Filter"].to<DocumentGenerator>(context, id)},
          _replacement{opNode["Replacement"].to<DocumentGenerator>(context, id)} {}

    void run(mongocxx::client_session& session) override {
        auto filter = _filter();
        auto replacement = _replacement();
        this->doBlock(_operation, [&](metrics::OperationContext& ctx) {
            auto result = (_onSession)
                ? _collection.find_one_and_replace(
                      session, filter.view(), replacement.view(), _options)
                : _collection.find_one_and_replace(filter.view(), replacement.view(), _options);
            if (result) {
                ctx.addDocuments(1);
                ctx.addBytes(result->view().length());
            }
            return std::make_optional(std::move(filter));
        });
    }

private:
    bool _onSession;
    mongocxx::collection _collection;
    mongocxx::options::find_one_and_replace _options;
    DocumentGenerator _filter;
    DocumentGenerator _replacement;
    metrics::Operation _operation;
};

/**
 * Example usage:
 *    Operations:
 *    - OperationName: insertMany
 *      OperationCommand:
 *        Documents:
 *        - { a : 1 }
 *        - { b : 1 }
 */
struct InsertManyOperation : public BaseOperation {

    InsertManyOperation(const Node& opNode,
                        bool onSession,
                        mongocxx::collection collection,
                        metrics::Operation operation,
                        PhaseContext& context,
                        ActorId id)
        : BaseOperation(context, opNode),
          _onSession{onSession},
          _collection{std::move(collection)},
          _operation{operation} {
        auto& documents = opNode["Documents"];
        if (!documents && !documents.isSequence()) {
            BOOST_THROW_EXCEPTION(InvalidConfigurationException(
                "'insertMany' expects a 'Documents' field of sequence type."));
        }
        for (auto&& [k, document] : documents) {
            _docExprs.push_back(document.to<DocumentGenerator>(context, id));
        }
    }

    void run(mongocxx::client_session& session) override {
        std::vector<bsoncxx::document::view_or_value> writeOps;
        size_t bytes = 0;
        for (auto&& docExpr : _docExprs) {
            auto doc = docExpr();
            bytes += doc.view().length();
            writeOps.emplace_back(std::move(doc));
        }

        this->doBlock(_operation, [&](metrics::OperationContext& ctx) {
            auto result = (_onSession) ? _collection.insert_many(session, writeOps, _options)
                                       : _collection.insert_many(writeOps, _options);

            ctx.addBytes(bytes);
            if (result) {
                ctx.addDocuments(result->inserted_count());
            }
            return std::nullopt;
        });
    }

private:
    mongocxx::collection _collection;
    const bool _onSession;
    mongocxx::options::insert _options;
    metrics::Operation _operation;
    std::vector<DocumentGenerator> _docExprs;
};

/**
 * Example usage:
 *    Operations:
 *    - OperationName: startTransaction
 *      OperationCommand:
 *        Options:
 *          WriteConcern:
 *            Level: majority
 *            Journal: true
 *          ReadConcern:
 *            Level: snapshot
 *          ReadPreference:
 *            ReadMode: primaryPreferred
 *            MaxStalenessSeconds: 1000
 */

struct StartTransactionOperation : public BaseOperation {

    StartTransactionOperation(const Node& opNode,
                              bool onSession,
                              mongocxx::collection collection,
                              metrics::Operation operation,
                              PhaseContext& context,
                              ActorId id)
        : BaseOperation(context, opNode), _operation{operation} {
        if (!opNode.isMap())
            return;
        if (opNode["Options"]) {
            _options = opNode["Options"].to<mongocxx::options::transaction>();
        }
    }

    void run(mongocxx::client_session& session) override {
        this->doBlock(_operation, [&](metrics::OperationContext& ctx) {
            session.start_transaction(_options);
            return std::nullopt;
        });
    }

private:
    mongocxx::options::transaction _options;
    metrics::Operation _operation;
};

/**
 * Example usage:
 *    Operations:
 *    - OperationName: commitTransaction
 */

struct CommitTransactionOperation : public BaseOperation {

    CommitTransactionOperation(const Node& opNode,
                               bool onSession,
                               mongocxx::collection collection,
                               metrics::Operation operation,
                               PhaseContext& context,
                               ActorId id)
        : BaseOperation(context, opNode), _operation{operation} {}

    void run(mongocxx::client_session& session) override {
        this->doBlock(_operation, [&](metrics::OperationContext& ctx) {
            session.commit_transaction();
            return std::nullopt;
        });
    }

private:
    metrics::Operation _operation;
};

/**
 * Example usage:
 *    Operations:
 *    - OperationName: withTransaction
 *      OperationCommand:
 *        OperationsInTransaction:
 *          - OperationName: insertOne
 *            OperationCommand:
 *              Document: {a: 100}
 *          - OperationName: findOneAndReplace
 *            OperationCommand:
 *              Filter: {a: 100}
 *              Replacement: {a: 30}
 */

struct WithTransactionOperation : public BaseOperation {

    WithTransactionOperation(const Node& opNode,
                             bool onSession,
                             mongocxx::collection collection,
                             metrics::Operation operation,
                             PhaseContext& context,
                             ActorId id)
        : BaseOperation(context, opNode),
          _onSession{onSession},
          _collection{std::move(collection)},
          _operation{operation} {
        auto& opsInTxn = opNode["OperationsInTransaction"];
        if (!opsInTxn.isSequence()) {
            BOOST_THROW_EXCEPTION(InvalidConfigurationException(
                "'WithTransaction' requires an 'OperationsInTransaction' node of sequence type."));
        }
        for (const auto&& [k, txnOp] : opsInTxn) {
            createTxnOps(txnOp, context, id);
        }
        if (opNode["Options"]) {
            _options = opNode["Options"].to<mongocxx::options::transaction>();
        }
    }

    void run(mongocxx::client_session& session) override {
        auto run_txn_ops([&](mongocxx::client_session* session) {
            for (auto&& op : _txnOps) {
                op->run(*session);
            }
        });
        this->doBlock(_operation, [&](metrics::OperationContext& ctx) {
            session.with_transaction(run_txn_ops, _options);
            return std::nullopt;
        });
    }

private:
    void createTxnOps(const Node& txnOp, PhaseContext& context, ActorId id) {
        auto opName = txnOp["OperationName"].to<std::string>();
        // MongoDB does not support transactions within transactions.
        std::set<std::string> excludeSet = {
            "startTransaction", "commitTransaction", "withTransaction"};
        if (excludeSet.find(opName) != excludeSet.end()) {
            BOOST_THROW_EXCEPTION(InvalidConfigurationException(
                "Operation '" + opName + "' not supported inside a 'with_transaction' operation."));
        }
        auto opConstructors = getOpConstructors();
        auto opConstructor = opConstructors.find(opName);
        if (opConstructor == opConstructors.end()) {
            BOOST_THROW_EXCEPTION(InvalidConfigurationException(
                "Operation '" + opName + "' not supported inside a 'with_transaction' operation."));
        }
        auto createOp = opConstructor->second;
        auto& yamlCommand = txnOp["OperationCommand"];
        _txnOps.push_back(createOp(yamlCommand, _onSession, _collection, _operation, context, id));
    }

    mongocxx::collection _collection;
    bool _onSession;
    metrics::Operation _operation;
    mongocxx::options::transaction _options;
    std::vector<std::unique_ptr<BaseOperation>> _txnOps;
};

/**
 * Example usage:
 *    Operations:
 *    - OperationName: setReadConcern
 *      OperationCommand:
 *        ReadConcern:
 *          Level: majority
 */

struct SetReadConcernOperation : public BaseOperation {
    SetReadConcernOperation(const Node& opNode,
                            bool onSession,
                            mongocxx::collection collection,
                            metrics::Operation operation,
                            PhaseContext& context,
                            ActorId id)
        : BaseOperation(context, opNode),
          _collection{std::move(collection)},
          _operation{operation} {
        _readConcern = opNode["ReadConcern"].to<mongocxx::read_concern>();
    }

    void run(mongocxx::client_session& session) override {
        this->doBlock(_operation, [&](metrics::OperationContext& ctx) {
            _collection.read_concern(_readConcern);
            return std::nullopt;
        });
    }

private:
    mongocxx::collection _collection;
    mongocxx::read_concern _readConcern;
    metrics::Operation _operation;
};

/**
 * Example usage:
 *    Operations:
 *    - OperationName: drop
 *      OperationCommand:
 *        OnSession: false
 *        Options:
 *          WriteConcern:
 *            Level: majority
 */
struct DropOperation : public BaseOperation {
    DropOperation(const Node& opNode,
                  bool onSession,
                  mongocxx::collection collection,
                  metrics::Operation operation,
                  PhaseContext& context,
                  ActorId id)
        : BaseOperation(context, opNode),
          _onSession{onSession},
          _collection{std::move(collection)},
          _operation{operation} {
        if (auto& c = opNode["Options"]["WriteConcern"]) {
            _wc = c.to<mongocxx::write_concern>();
        }
    }

    void run(mongocxx::client_session& session) override {
        this->doBlock(_operation, [&](metrics::OperationContext& ctx) {
            (_onSession) ? _collection.drop(session, _wc) : _collection.drop(_wc);
            // ideally return something indicating collection name
            return std::nullopt;
        });
    }

private:
    mongocxx::collection _collection;
    const bool _onSession;
    metrics::Operation _operation;
    mongocxx::write_concern _wc;
};

std::unordered_map<std::string, OpCallback&> getOpConstructors() {
    // Maps the yaml 'OperationName' string to the appropriate constructor of 'BaseOperation' type.
    std::unordered_map<std::string, OpCallback&> opConstructors = {
        {"bulkWrite", baseCallback<BaseOperation, OpCallback, BulkWriteOperation>},
        {"countDocuments", baseCallback<BaseOperation, OpCallback, CountDocumentsOperation>},
        {"estimatedDocumentCount",
         baseCallback<BaseOperation, OpCallback, EstimatedDocumentCountOperation>},
        {"createIndex", baseCallback<BaseOperation, OpCallback, CreateIndexOperation>},
        {"find", baseCallback<BaseOperation, OpCallback, FindOperation>},
        {"findOne", baseCallback<BaseOperation, OpCallback, FindOneOperation>},
        {"findOneAndUpdate", baseCallback<BaseOperation, OpCallback, FindOneAndUpdateOperation>},
        {"findOneAndDelete", baseCallback<BaseOperation, OpCallback, FindOneAndDeleteOperation>},
        {"findOneAndReplace", baseCallback<BaseOperation, OpCallback, FindOneAndReplaceOperation>},
        {"insertMany", baseCallback<BaseOperation, OpCallback, InsertManyOperation>},
        {"startTransaction", baseCallback<BaseOperation, OpCallback, StartTransactionOperation>},
        {"commitTransaction", baseCallback<BaseOperation, OpCallback, CommitTransactionOperation>},
        {"withTransaction", baseCallback<BaseOperation, OpCallback, WithTransactionOperation>},
        {"setReadConcern", baseCallback<BaseOperation, OpCallback, SetReadConcernOperation>},
        {"drop", baseCallback<BaseOperation, OpCallback, DropOperation>},
        {"insertOne", baseCallback<BaseOperation, OpCallback, InsertOneOperation>},
        {"deleteOne", baseCallback<BaseOperation, OpCallback, DeleteOneOperation>},
        {"deleteMany", baseCallback<BaseOperation, OpCallback, DeleteManyOperation>},
        {"updateOne", baseCallback<BaseOperation, OpCallback, UpdateOneOperation>},
        {"updateMany", baseCallback<BaseOperation, OpCallback, UpdateManyOperation>},
        {"replaceOne", baseCallback<BaseOperation, OpCallback, ReplaceOneOperation>}};

    return opConstructors;
}

// Equivalent to `nvl(phase[Database], actor[Database])`.
std::string getDbName(const PhaseContext& phaseContext) {
    auto phaseDb = phaseContext["Database"].maybe<std::string>();
    auto actorDb = phaseContext.actor()["Database"].maybe<std::string>();
    if (!phaseDb && !actorDb) {
        BOOST_THROW_EXCEPTION(
            InvalidConfigurationException("Must give Database in Phase or Actor block."));
    }
    return phaseDb ? *phaseDb : *actorDb;
}

}  // namespace

namespace genny::actor {

struct CrudActor::CollectionName {
    std::optional<std::string> collectionName;
    std::optional<int64_t> numCollections;
    explicit CollectionName(PhaseContext& phaseContext)
        : collectionName{phaseContext["Collection"].maybe<std::string>()},
          numCollections{phaseContext["CollectionCount"].maybe<IntegerSpec>()} {
        if (collectionName && numCollections) {
            BOOST_THROW_EXCEPTION(InvalidConfigurationException(
                "Collection or CollectionCount, not both in Crud Actor."));
        }
        if (!collectionName && !numCollections) {
            BOOST_THROW_EXCEPTION(InvalidConfigurationException(
                "One of Collection or CollectionCount must be provided in Crud Actor."));
        }
    }
    // Get the assigned collection name or generate a name based on collectionCount and the
    // the actorId.
    std::string generateName(ActorId id) {
        if (collectionName) {
            return collectionName.value();
        }
        auto collectionNumber = id % numCollections.value();
        return "Collection" + std::to_string(collectionNumber);
    }
};

struct CrudActor::PhaseConfig {
    std::vector<std::unique_ptr<BaseOperation>> operations;
    metrics::Operation metrics;
    std::string dbName;
    CrudActor::CollectionName collectionName;

    PhaseConfig(PhaseContext& phaseContext, mongocxx::pool::entry& client, ActorId id)
        : dbName{getDbName(phaseContext)},
          metrics{phaseContext.actor().operation("Crud", id)},
          collectionName{phaseContext} {
        auto name = collectionName.generateName(id);
        auto addOperation = [&](const Node& node) -> std::unique_ptr<BaseOperation> {
            auto collection = (*client)[dbName][name];
            auto& yamlCommand = node["OperationCommand"];
            auto opName = node["OperationName"].to<std::string>();
            auto onSession = yamlCommand["OnSession"].maybe<bool>().value_or(false);

            auto opConstructors = getOpConstructors();
            // Grab the appropriate Operation struct defined by 'OperationName'.
            auto op = opConstructors.find(opName);
            if (op == opConstructors.end()) {
                BOOST_THROW_EXCEPTION(InvalidConfigurationException(
                    "Operation '" + opName + "' not supported in Crud Actor."));
            }

            // If the yaml specified MetricsName in the Phase block, then associate the metrics
            // with the Phase; otherwise, associate with the Actor (e.g. all bulkWrite operations
            // get recorded together across all Phases). The latter case (not specifying
            // MetricsName) is legacy configuration-syntax.
            //
            // Node is convertible to bool but only explicitly so need to do the odd-looking
            // `? true : false` thing.
            const bool perPhaseMetrics = phaseContext["MetricsName"] ? true : false;

            auto createOperation = op->second;
            return createOperation(yamlCommand,
                                   onSession,
                                   collection,
                                   perPhaseMetrics ? phaseContext.operation(opName, id)
                                                   : phaseContext.actor().operation(opName, id),
                                   phaseContext,
                                   id);
        };

        operations = phaseContext.getPlural<std::unique_ptr<BaseOperation>>(
            "Operation", "Operations", addOperation);
    }
};

void CrudActor::run() {
    for (auto&& config : _loop) {
        auto session = _client->start_session();
        for (const auto&& _ : config) {
            auto metricsContext = config->metrics.start();

            for (auto&& op : config->operations) {
                op->run(session);
            }

            metricsContext.success();
        }
    }
}

CrudActor::CrudActor(genny::ActorContext& context)
    : Actor(context),
      _client{std::move(
          context.client(context.get("ClientName").maybe<std::string>().value_or("Default")))},
      _loop{context, _client, CrudActor::id()} {}

namespace {
auto registerCrudActor = Cast::registerDefault<CrudActor>();
}
}  // namespace genny::actor
