#include <gennylib/generators.hpp>

#include <boost/log/trivial.hpp>
#include <bsoncxx/json.hpp>
#include <random>
#include <stdlib.h>

using bsoncxx::builder::stream::close_array;
using bsoncxx::builder::stream::close_document;
using bsoncxx::builder::stream::finalize;
using bsoncxx::builder::stream::open_array;
using bsoncxx::builder::stream::open_document;

namespace genny {

BsonDocument::BsonDocument() {
    doc = bsoncxx::builder::stream::document{} << bsoncxx::builder::stream::finalize;
}

BsonDocument::BsonDocument(const YAML::Node node) {
    if (!node) {
        BOOST_LOG_TRIVIAL(info) << "BsonDocument constructor using empty document";
    } else if (!node.IsMap()) {
        BOOST_LOG_TRIVIAL(fatal) << "Not map in BsonDocument constructor";
        exit(EXIT_FAILURE);
    } else {
        BOOST_LOG_TRIVIAL(trace) << "In BsonDocument constructor";
        doc = parseMap(node);
        BOOST_LOG_TRIVIAL(trace) << "Parsed map in BsonDocument constructor";
    }
}

bsoncxx::document::view BsonDocument::view(bsoncxx::builder::stream::document&) {
    return doc->view();
}

TemplateDocument::TemplateDocument(YAML::Node node, std::mt19937_64& rng) : Document() {
    if (!node) {
        BOOST_LOG_TRIVIAL(fatal) << "TemplateDocument constructor and !node";
        exit(EXIT_FAILURE);
    }
    if (!node.IsMap()) {
        BOOST_LOG_TRIVIAL(fatal) << "Not map in TemplateDocument constructor";
        exit(EXIT_FAILURE);
    }

    auto templates = getGeneratorTypes();
    std::vector<std::tuple<std::string, std::string, YAML::Node>> overrides;

    BOOST_LOG_TRIVIAL(trace) << "In TemplateDocument constructor";
    doc.setDoc(parseMap(node, templates, "", overrides));
    BOOST_LOG_TRIVIAL(trace)
        << "In TemplateDocument constructor. Parsed the document. About to deal with overrides";
    for (auto entry : overrides) {
        auto key = std::get<0>(entry);
        auto typeString = std::get<1>(entry);
        YAML::Node yamlOverride = std::get<2>(entry);
        BOOST_LOG_TRIVIAL(trace) << "In TemplateDocument constructor. Dealing with an override for "
                                 << key;

        auto type = typeString.substr(1, typeString.length());
        BOOST_LOG_TRIVIAL(trace) << "Making value generator for key " << key << " and type "
                                 << type;
        override[key] = makeUniqueValueGenerator(yamlOverride, type, rng);
    }
}

void TemplateDocument::applyOverrideLevel(bsoncxx::builder::stream::document& output,
                                          bsoncxx::document::view doc,
                                          string prefix) {
    // Going to need variants of this for arrays

    // iterate through keys. if key matches exactly, replace in output.
    // if key doesn't match, copy element to output
    // if key prefix matches, descend a level.

    // process override for elements at this level

    // I don't think I want this as a naked pointer. It's owned
    // above. Can switch to shared_ptr
    unordered_map<string, ValueGenerator*> thislevel;
    // process override for elements at lower level
    set<string> lowerlevel;
    //    cout << "prefix is " << prefix ;
    for (auto& elem : override) {
        string key = elem.first;
        //        BOOST_LOG_TRIVIAL(trace) << "Going through overrides key: " << key << " value is "
        //                         << elem.second << " prefix.length() = " << prefix.length();
        if (prefix == "" || key.compare(0, prefix.length(), prefix) == 0) {
            // prefix match. Need what comes after
            // grab everything after prefix
            // BOOST_LOG_TRIVIAL(trace) << "Key matched with prefix";
            auto suffix = key.substr(prefix.length(), key.length() - prefix.length());
            // check for a period. If no period, put in thislevel
            auto find = suffix.find('.');
            // no match
            if (find == std::string::npos) {
                thislevel[suffix] = elem.second.get();
                //  BOOST_LOG_TRIVIAL(trace) << "Putting thislevel[" << suffix << "]=" <<
                //  elem.second;
            } else {
                // if period, grab from suffix to period and put in lowerlevel
                // We won't actually use the second element here
                // BOOST_LOG_TRIVIAL(trace) << "Putting lowerlevel[" << suffix << "]=" <<
                // elem.second;
                lowerlevel.insert(suffix.substr(0, find));
            }
        } else {
            // BOOST_LOG_TRIVIAL(trace) << "No prefix match";
        }
    }

    for (auto elem : doc) {
        // BOOST_LOG_TRIVIAL(trace) << "Looking at key " << elem.key().to_string();
        auto iter = thislevel.find(elem.key().to_string());
        auto iter2 = lowerlevel.find(elem.key().to_string());
        if (iter != thislevel.end()) {
            // replace this entry
            // BOOST_LOG_TRIVIAL(trace) << "Matched on this level. Replacing ";
            output << elem.key().to_string() << iter->second->generate().view()[0].get_value();
        } else if (iter2 != lowerlevel.end()) {
            // need to check if child is document, array, or other.
            //            BOOST_LOG_TRIVIAL(trace) << "Partial match. Need to descend";
            switch (elem.type()) {
                case bsoncxx::type::k_document: {
                    bsoncxx::builder::stream::document mydoc{};
                    applyOverrideLevel(
                        mydoc, elem.get_document().value, prefix + elem.key().to_string() + '.');
                    output << elem.key().to_string() << open_document
                           << bsoncxx::builder::concatenate(mydoc.view()) << close_document;
                } break;
                case bsoncxx::type::k_array:
                    BOOST_LOG_TRIVIAL(fatal)
                        << "Trying to descend a level of bson in overrides. Array not "
                           "supported "
                           "yet.";
                default:
                    BOOST_LOG_TRIVIAL(fatal) << "Trying to descend a level of bson in "
                                                "overrides but not a map or "
                                                "array";
                    exit(EXIT_FAILURE);
            }
        } else {
            //            BOOST_LOG_TRIVIAL(trace) << "No match, just pass through";
            bsoncxx::types::value ele_val{elem.get_value()};
            output << elem.key().to_string() << ele_val;
        }
    }
}

bsoncxx::document::view TemplateDocument::view(bsoncxx::builder::stream::document& output) {
    // Need to iterate through the doc, and for any field see if it
    // matches. Override the value if it does.
    // bson output

    // scope problem -- output is going out of scope here
    // to be thread safe this has to be on the stack or in the per thread data.

    // Not sure I need the tempdoc in addition to output
    bsoncxx::builder::stream::document tempdoc{};
    applyOverrideLevel(output, doc.view(tempdoc), "");
    return output.view();
}


// parse a YAML Node and make a document of the correct type
unique_ptr<Document> makeDoc(const YAML::Node node, std::mt19937_64& rng) {
    if (!node) {  // empty document should be BsonDocument
        return unique_ptr<Document>{new BsonDocument(node)};
    } else
        return unique_ptr<Document>{new TemplateDocument(node, rng)};
};

// This returns a set of the value generator types with $ prefixes
const std::set<std::string> getGeneratorTypes() {
    return (std::set<std::string>{"$randomint", "$fastrandomstring", "$randomstring", "$useval"});
}

ValueGenerator* makeValueGenerator(YAML::Node yamlNode, std::string type, std::mt19937_64& rng) {
    if (type == "randomint") {
        return new RandomIntGenerator(yamlNode, rng);
    } else if (type == "randomstring") {
        return new RandomStringGenerator(yamlNode, rng);
    } else if (type == "fastrandomstring") {
        return new FastRandomStringGenerator(yamlNode, rng);
    } else if (type == "useval") {
        return new UseValueGenerator(yamlNode, rng);
    }
    BOOST_LOG_TRIVIAL(fatal) << "In makeValueGenerator and don't know how to handle type " << type;
    exit(EXIT_FAILURE);
}

ValueGenerator* makeValueGenerator(YAML::Node yamlNode, std::mt19937_64& rng) {
    // Should we put a list directly into UseValueGenerator also?
    if (!yamlNode.IsMap()) {
        BOOST_LOG_TRIVIAL(fatal)
            << "ValueGenerator Node in makeValueGenerator is not a yaml map or a sequence";
        exit(EXIT_FAILURE);
    }
    if (auto type = yamlNode["type"])
        return (makeValueGenerator(yamlNode, type.Scalar(), rng));
    // If it doesn't have a type field, search for templating keys
    for (auto&& entry : yamlNode) {
        auto key = entry.first.Scalar();
        if (getGeneratorTypes().count(key)) {
            auto type = key.substr(1, key.length());
            return (makeValueGenerator(entry.second, type, rng));
        }
    }
    return (makeValueGenerator(yamlNode, "useval", rng));
}

int64_t ValueGenerator::generateInt() {
    return valAsInt(generate());
};
double ValueGenerator::generateDouble() {
    return valAsDouble(generate());
};
std::string ValueGenerator::generateString() {
    return valAsString(generate());
}


std::unique_ptr<ValueGenerator> makeUniqueValueGenerator(YAML::Node yamlNode,
                                                         std::mt19937_64& rng) {
    return std::unique_ptr<ValueGenerator>(makeValueGenerator(yamlNode, rng));
}
std::shared_ptr<ValueGenerator> makeSharedValueGenerator(YAML::Node yamlNode,
                                                         std::mt19937_64& rng) {
    return std::unique_ptr<ValueGenerator>(makeValueGenerator(yamlNode, rng));
}

std::unique_ptr<ValueGenerator> makeUniqueValueGenerator(YAML::Node yamlNode,
                                                         std::string type,
                                                         std::mt19937_64& rng) {
    return std::unique_ptr<ValueGenerator>(makeValueGenerator(yamlNode, type, rng));
}
std::shared_ptr<ValueGenerator> makeSharedValueGenerator(YAML::Node yamlNode,
                                                         std::string type,
                                                         std::mt19937_64& rng) {
    return std::unique_ptr<ValueGenerator>(makeValueGenerator(yamlNode, type, rng));
}

// Check type cases and get a string out of it. Assumes it is getting a bson array of length 1.
std::string valAsString(view_or_value val) {
    auto elem = val.view()[0];
    switch (elem.type()) {
        case bsoncxx::type::k_int64:
            return (std::to_string(elem.get_int64().value));
            break;
        case bsoncxx::type::k_int32:
            return (std::to_string(elem.get_int32().value));
            break;
        case bsoncxx::type::k_double:
            return (std::to_string(elem.get_double().value));
            break;
        case bsoncxx::type::k_utf8:
            return (elem.get_utf8().value.to_string());
            break;
        case bsoncxx::type::k_document:
        case bsoncxx::type::k_array:
        case bsoncxx::type::k_binary:
        case bsoncxx::type::k_undefined:
        case bsoncxx::type::k_oid:
        case bsoncxx::type::k_bool:
        case bsoncxx::type::k_date:
        case bsoncxx::type::k_null:
        case bsoncxx::type::k_regex:
        case bsoncxx::type::k_dbpointer:
        case bsoncxx::type::k_code:
        case bsoncxx::type::k_symbol:
        case bsoncxx::type::k_timestamp:

            BOOST_LOG_TRIVIAL(fatal) << "valAsString with type unsuported type in list";
            exit(EXIT_FAILURE);
            break;
        default:
            BOOST_LOG_TRIVIAL(fatal) << "valAsString with type unsuported type not in list";
            exit(EXIT_FAILURE);
    }
    return ("");
}
int64_t valAsInt(view_or_value val) {
    auto elem = val.view()[0];
    switch (elem.type()) {
        case bsoncxx::type::k_int64:
            return (elem.get_int64().value);
            break;
        case bsoncxx::type::k_int32:
            return (static_cast<int64_t>(elem.get_int32().value));
            break;
        case bsoncxx::type::k_double:
            return (static_cast<int64_t>(elem.get_double().value));
            break;
        case bsoncxx::type::k_utf8:
        case bsoncxx::type::k_document:
        case bsoncxx::type::k_array:
        case bsoncxx::type::k_binary:
        case bsoncxx::type::k_undefined:
        case bsoncxx::type::k_oid:
        case bsoncxx::type::k_bool:
        case bsoncxx::type::k_date:
        case bsoncxx::type::k_null:
        case bsoncxx::type::k_regex:
        case bsoncxx::type::k_dbpointer:
        case bsoncxx::type::k_code:
        case bsoncxx::type::k_symbol:
        case bsoncxx::type::k_timestamp:

            BOOST_LOG_TRIVIAL(fatal) << "valAsInt with type unsuported type in list";
            exit(EXIT_FAILURE);
            break;
        default:
            BOOST_LOG_TRIVIAL(fatal) << "valAsInt with type unsuported type not in list";
            exit(EXIT_FAILURE);
    }
    return (0);
}
double valAsDouble(view_or_value val) {
    auto elem = val.view()[0];
    switch (elem.type()) {
        case bsoncxx::type::k_int64:
            return (static_cast<double>(elem.get_int64().value));
            break;
        case bsoncxx::type::k_int32:
            return (static_cast<double>(elem.get_int32().value));
            break;
        case bsoncxx::type::k_double:
            return (static_cast<double>(elem.get_double().value));
            break;
        case bsoncxx::type::k_utf8:
        case bsoncxx::type::k_document:
        case bsoncxx::type::k_array:
        case bsoncxx::type::k_binary:
        case bsoncxx::type::k_undefined:
        case bsoncxx::type::k_oid:
        case bsoncxx::type::k_bool:
        case bsoncxx::type::k_date:
        case bsoncxx::type::k_null:
        case bsoncxx::type::k_regex:
        case bsoncxx::type::k_dbpointer:
        case bsoncxx::type::k_code:
        case bsoncxx::type::k_symbol:
        case bsoncxx::type::k_timestamp:

            BOOST_LOG_TRIVIAL(fatal) << "valAsInt with type unsuported type in list";
            exit(EXIT_FAILURE);
            break;
        default:
            BOOST_LOG_TRIVIAL(fatal) << "valAsInt with type unsuported type not in list";
            exit(EXIT_FAILURE);
    }
    return (0);
}

UseValueGenerator::UseValueGenerator(YAML::Node& node, std::mt19937_64& rng)
    : ValueGenerator(node, rng) {
    // add in error checking
    if (node.IsScalar()) {
        value = yamlToValue(node);
    } else {
        value = yamlToValue(node["value"]);
    }
}

bsoncxx::array::value UseValueGenerator::generate() {
    // probably should actually return a view or a copy of the value
    return (bsoncxx::array::value(*value));
}

RandomIntGenerator::RandomIntGenerator(const YAML::Node& node, std::mt19937_64& rng)
    : ValueGenerator(node, rng), generator(GeneratorType::UNIFORM), min(0), max(100), t(10) {
    // It's okay to have a scalar for the templating. Just use defaults
    if (node.IsMap()) {
        if (auto distributionNode = node["distribution"]) {
            auto distributionString = distributionNode.Scalar();
            if (distributionString == "uniform")
                generator = GeneratorType::UNIFORM;
            else if (distributionString == "binomial")
                generator = GeneratorType::BINOMIAL;
            else if (distributionString == "negative_binomial")
                generator = GeneratorType::NEGATIVE_BINOMIAL;
            else if (distributionString == "geometric")
                generator = GeneratorType::GEOMETRIC;
            else if (distributionString == "poisson")
                generator = GeneratorType::POISSON;
            else {
                BOOST_LOG_TRIVIAL(fatal)
                    << "In RandomIntGenerator and have unknown distribution type "
                    << distributionString;
                exit(EXIT_FAILURE);
            }
        }
        // now read in parameters based on the distribution type
        switch (generator) {
            case GeneratorType::UNIFORM:
                if (auto minimum = node["min"]) {
                    min = IntOrValue(minimum, rng);
                }
                if (auto maximum = node["max"]) {
                    max = IntOrValue(maximum, rng);
                }
                break;
            case GeneratorType::BINOMIAL:
                if (auto trials = node["t"])
                    t = IntOrValue(trials, rng);
                else
                    BOOST_LOG_TRIVIAL(warning)
                        << "Binomial distribution in random int, but no t parameter";
                if (auto probability = node["p"])
                    p = makeUniqueValueGenerator(probability, rng);
                else {
                    BOOST_LOG_TRIVIAL(fatal)
                        << "Binomial distribution in random int, but no p parameter";
                    exit(EXIT_FAILURE);
                }
                break;
            case GeneratorType::NEGATIVE_BINOMIAL:
                if (auto kval = node["k"])
                    t = IntOrValue(kval, rng);
                else
                    BOOST_LOG_TRIVIAL(warning)
                        << "Negative binomial distribution in random int, not no k parameter";
                if (auto probability = node["p"])
                    p = makeUniqueValueGenerator(probability, rng);
                else {
                    BOOST_LOG_TRIVIAL(fatal)
                        << "Binomial distribution in random int, but no p parameter";
                    exit(EXIT_FAILURE);
                }
                break;
            case GeneratorType::GEOMETRIC:
                if (auto probability = node["p"])
                    p = makeUniqueValueGenerator(probability, rng);
                else {
                    BOOST_LOG_TRIVIAL(fatal)
                        << "Geometric distribution in random int, but no p parameter";
                    exit(EXIT_FAILURE);
                }
                break;
            case GeneratorType::POISSON:
                if (auto meannode = node["mean"])
                    mean = makeUniqueValueGenerator(meannode, rng);
                else {
                    BOOST_LOG_TRIVIAL(fatal)
                        << "Geometric distribution in random int, but no p parameter";
                    exit(EXIT_FAILURE);
                }
                break;
            default:
                BOOST_LOG_TRIVIAL(fatal)
                    << "Unknown generator type in RandomIntGenerator in switch statement";
                exit(EXIT_FAILURE);
                break;
        }
    }
}

int64_t RandomIntGenerator::generateInt() {
    switch (generator) {
        case GeneratorType::UNIFORM: {
            uniform_int_distribution<int64_t> distribution(min.getInt(), max.getInt());
            return (distribution(_rng));
        } break;
        case GeneratorType::BINOMIAL: {
            binomial_distribution<int64_t> distribution(t.getInt(), p->generateDouble());
            return (distribution(_rng));
        } break;
        case GeneratorType::NEGATIVE_BINOMIAL: {
            negative_binomial_distribution<int64_t> distribution(t.getInt(), p->generateDouble());
            return (distribution(_rng));
        } break;
        case GeneratorType::GEOMETRIC: {
            geometric_distribution<int64_t> distribution(p->generateDouble());
            return (distribution(_rng));
        } break;
        case GeneratorType::POISSON: {
            poisson_distribution<int64_t> distribution(mean->generateDouble());
            return (distribution(_rng));
        } break;
        default:
            BOOST_LOG_TRIVIAL(fatal)
                << "Unknown generator type in RandomIntGenerator in switch rngment";
            exit(EXIT_FAILURE);
            break;
    }
    BOOST_LOG_TRIVIAL(fatal)
        << "Reached end of RandomIntGenerator::generateInt. Should have returned earlier";
    exit(EXIT_FAILURE);
}
std::string RandomIntGenerator::generateString() {
    return (std::to_string(generateInt()));
}
bsoncxx::array::value RandomIntGenerator::generate() {
    return (bsoncxx::builder::stream::array{} << generateInt()
                                              << bsoncxx::builder::stream::finalize);
}

IntOrValue::IntOrValue(YAML::Node yamlNode, std::mt19937_64& rng) : myInt(0), myGenerator(nullptr) {
    if (yamlNode.IsScalar()) {
        // Read in just a number
        isInt = true;
        myInt = yamlNode.as<int64_t>();
    } else {
        // use a value generator
        isInt = false;
        myGenerator = makeUniqueValueGenerator(yamlNode, rng);
    }
}
FastRandomStringGenerator::FastRandomStringGenerator(const YAML::Node& node, std::mt19937_64& rng)
    : ValueGenerator(node, rng) {
    if (node["length"]) {
        length = IntOrValue(node["length"], rng);
    } else {
        length = IntOrValue(10);
    }
}
bsoncxx::array::value FastRandomStringGenerator::generate() {
    std::string str;
    auto thisLength = length.getInt();
    str.resize(thisLength);
    auto randomnum = _rng();
    int bits = 64;
    for (int i = 0; i < thisLength; i++) {
        if (bits < 6) {
            bits = 64;
            randomnum = _rng();
        }
        str[i] = fastAlphaNum[(randomnum & 0x2f) % fastAlphaNumLength];
        randomnum >>= 6;
        bits -= 6;
    }
    return (bsoncxx::builder::stream::array{} << str << bsoncxx::builder::stream::finalize);
}

RandomStringGenerator::RandomStringGenerator(YAML::Node& node, std::mt19937_64& rng)
    : ValueGenerator(node, rng) {
    if (node["length"]) {
        length = IntOrValue(node["length"], rng);
    } else {
        length = IntOrValue(10);
    }
    if (node["alphabet"]) {
        alphabet = node["alphabet"].Scalar();
    } else {
        alphabet = alphaNum;
    }
}
bsoncxx::array::value RandomStringGenerator::generate() {
    std::string str;
    auto alphabetLength = alphabet.size();
    uniform_int_distribution<int> distribution(0, alphabetLength - 1);
    auto thisLength = length.getInt();
    str.resize(thisLength);
    for (int i = 0; i < thisLength; i++) {
        str[i] = alphabet[distribution(_rng)];
    }
    return (bsoncxx::builder::stream::array{} << str << bsoncxx::builder::stream::finalize);
}


}  // namespace genny
