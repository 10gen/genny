#include <utility>

#include <map>

#include <gennylib/Node.hpp>

namespace genny {

//
// Helper Functions
//
namespace {
using Child = std::unique_ptr<class Node>;
using Children = std::map<v1::NodeKey, Child>;
using ChildKeys = std::vector<v1::NodeKey>;

std::string joinPath(const v1::NodeKey::Path& path) {
    std::stringstream out;
    for (size_t i = 0; i < path.size(); ++i) {
        auto key = path[i].toString();
        out << key;
        if (i < path.size() - 1) {
            out << "/";
        }
    }
    return out.str();
}

Node::Type determineType(const YAML::Node node) {
    auto yamlTyp = node.Type();
    switch (yamlTyp) {
        case YAML::NodeType::Undefined:
            return Node::Type::Undefined;
        case YAML::NodeType::Null:
            return Node::Type::Null;
        case YAML::NodeType::Scalar:
            return Node::Type::Scalar;
        case YAML::NodeType::Sequence:
            return Node::Type::Sequence;
        case YAML::NodeType::Map:
            return Node::Type::Map;
    }
    throw std::logic_error("impossible");
}

// Like YAML::Load but throw more useful exception
YAML::Node parse(std::string yaml, const std::string& path) {
    try {
        return YAML::Load(yaml);
    } catch (const YAML::ParserException& x) {
        BOOST_THROW_EXCEPTION(InvalidYAMLException(path, x));
    }
}

}  // namespace


//
// NodeSource
//

NodeSource::NodeSource(std::string yaml, std::string path)
    : _yaml{parse(std::move(yaml), path)},
      _root{std::make_unique<Node>(v1::NodeKey::Path{v1::NodeKey{path}}, _yaml)} {}

NodeSource::~NodeSource() = default;

UnusedNodes NodeSource::unused() const {
    return this->_root->unused();
}

//
// v1::NodeKey
//

std::string v1::NodeKey::toString() const {
    std::stringstream out;
    out << *this;
    return out.str();
}


//
// Exception Types
//

namespace {
std::string invalidYamlExceptionWhat(const std::string& path,
                                     const YAML::ParserException& yamlException) {
    std::stringstream out;
    out << "Invalid YAML: ";
    out << "'" << yamlException.msg << "' ";
    out << "at (Line:Column)=(" << yamlException.mark.line << ":" << yamlException.mark.column
        << "). ";
    out << "On node with path '" << path << "'.";

    return out.str();
}
std::string invalidConversionExceptionWhat(const struct Node* node,
                                           const YAML::BadConversion& yamlException,
                                           const std::type_info& destType) {
    std::stringstream out;
    out << "Couldn't convert to '" << boost::core::demangle(destType.name()) << "': ";
    out << "'" << yamlException.msg << "' at (Line:Column)=(" << yamlException.mark.line << ":"
        << yamlException.mark.column << "). ";
    out << "On node with path '" << node->path() << "': ";
    out << *node;
    return out.str();
}
std::string invalidKeyExceptionWhat(const std::string& msg,
                                    const std::string& key,
                                    const Node* node) {
    std::stringstream out;
    out << "Invalid key '" << key << "': ";
    out << msg << " ";
    out << "On node with path '" << node->path() << "': ";
    out << *node;
    return out.str();
}
}  // namespace

InvalidYAMLException::InvalidYAMLException(const std::string& path,
                                           const YAML::ParserException& yamlException)
    : _what{invalidYamlExceptionWhat(path, yamlException)} {
    *this << InvalidYAMLException::Message{_what};
}

InvalidConversionException::InvalidConversionException(const struct Node* node,
                                                       const YAML::BadConversion& yamlException,
                                                       const std::type_info& destType)
    : _what{invalidConversionExceptionWhat(node, yamlException, destType)} {
    *this << InvalidConversionException::Message{_what};
}

InvalidKeyException::InvalidKeyException(const std::string& msg,
                                         const std::string& key,
                                         const Node* node)
    : _what{invalidKeyExceptionWhat(msg, key, node)} {
    *this << InvalidKeyException::Message{_what};
}


//
// NodeImpl
//

class NodeImpl {
public:
    NodeImpl(const Node* self, const YAML::Node yaml, const v1::NodeKey::Path& path)
        : _self{self},
          _keyOrder{},
          _children{constructChildren(path, _keyOrder, yaml)},
          _used{false},
          _yaml{yaml},
          _path{path} {}

    const Node& get(const v1::NodeKey& key) const {
        auto&& it = _children.find(key);
        if (it == _children.end()) {
            v1::NodeKey::Path childPath = _self->_impl->_path;
            childPath.push_back(key);
            _children.emplace(key, std::make_unique<Node>(std::move(childPath), _zombie));
        }
        return *_children.at(key);
    }

    const YAML::Node yaml() const {
        this->_used = true;
        return _yaml;
    }

    UnusedNodes unused() const {
        // Ignore cases where we did something like node["does not exist"].maybe<int>()
        // since technically there is a "zombie" node that the yaml-cpp internals creates.
        if (_self->_impl->_yaml == _zombie) {
            return {};
        }

        // Depth-first.
        UnusedNodes out{};
        for (auto&& [_, child] : this->_children) {
            const auto childUnused = child->unused();
            for (auto&& u : childUnused) {
                out.push_back(u);
            }
        }

        // Base-case: no children and unused.
        if (this->_children.empty() && !this->_used) {
            out.push_back(this->path());
        }

        return out;
    }

    Node::Type type() const {
        return determineType(_yaml);
    }

    std::string tag() const {
        return _yaml.Tag();
    }

    explicit operator bool() const {
        return bool(_yaml);
    }

    bool isScalar() const {
        return _yaml.IsScalar();
    }

    bool isMap() const {
        return _yaml.IsMap();
    }

    bool isSequence() const {
        return _yaml.IsSequence();
    }

    bool isNull() const {
        return _yaml.IsNull();
    }

    size_t size() const {
        return _yaml.size();
    }

    std::string key() const {
        return _path.empty() ? "/" : _path.back().toString();
    }

    std::string path() const {
        return joinPath(_path);
    }

    friend std::ostream& operator<<(std::ostream& out, const NodeImpl& impl) {
        return out << YAML::Dump(impl._yaml);
    }

private:
    friend NodeIterator;

    static Children constructChildren(const v1::NodeKey::Path& path,
                                      ChildKeys& keyOrder,
                                      const YAML::Node yaml) {
        Children out;
        if (!yaml.IsMap() && !yaml.IsSequence()) {
            return out;
        }

        int index = 0;
        auto extractKey = [&](auto& kvp, YAML::Node iteratee) -> v1::NodeKey {
            if (iteratee.IsMap()) {
                return v1::NodeKey{kvp.first.template as<std::string>()};
            } else {
                return v1::NodeKey{index++};
            }
        };
        auto extractValue = [&](auto& kvp, YAML::Node iteratee) -> YAML::Node {
            if (iteratee.IsMap()) {
                return kvp.second;
            } else {
                return kvp;
            }
        };

        for (auto&& kvp : yaml) {
            const auto key = extractKey(kvp, yaml);
            keyOrder.push_back(key);
            v1::NodeKey::Path childPath = path;
            childPath.push_back(key);
            out.emplace(key, std::make_unique<Node>(std::move(childPath), extractValue(kvp, yaml)));
        }

        return out;
    }

    static const YAML::Node _zombie;

    const Node* _self;
    // these 2 need to be mutable to generate placeholder nodes for non-existent keys.
    mutable ChildKeys _keyOrder;  // maintain insertion-order
    mutable Children _children;
    mutable bool _used;
    const YAML::Node _yaml;
    const v1::NodeKey::Path _path;
};

const YAML::Node NodeImpl::_zombie = YAML::Load("")["zombie"];


//
// Node
//

Node::~Node() = default;

Node::Node(const v1::NodeKey::Path& path, const YAML::Node yaml)
    : _impl{std::make_unique<NodeImpl>(this, yaml, path)} {}

std::string Node::key() const {
    return this->_impl->key();
}

std::string Node::path() const {
    return this->_impl->path();
}

std::ostream& operator<<(std::ostream& out, const Node& node) {
    return out << *(node._impl);
}

bool Node::isScalar() const {
    return _impl->isScalar();
}

Node::Type Node::type() const {
    return _impl->type();
}

bool Node::isNull() const {
    return _impl->isNull();
}

bool Node::isMap() const {
    return _impl->isMap();
}

bool Node::isSequence() const {
    return _impl->isSequence();
}

std::string Node::tag() const {
    return _impl->tag();
}

size_t Node::size() const {
    return _impl->size();
}

const YAML::Node Node::yaml() const {
    return _impl->yaml();
}

UnusedNodes Node::unused() const {
    return _impl->unused();
}

Node::operator bool() const {
    return _impl->operator bool();
}

const Node& Node::operator[](long key) const {
    return _impl->get(v1::NodeKey{key});
}

const Node& Node::operator[](const std::string& key) const {
    return _impl->get(v1::NodeKey{key});
}

class NodeIterator Node::begin() const {
    return NodeIterator{&*this->_impl, false};
}

class NodeIterator Node::end() const {
    return NodeIterator{&*this->_impl, true};
}


//
// NodeIteratorValue
//

NodeIteratorValue::NodeIteratorValue(const v1::NodeKey& key, const Node& node)
    : std::pair<const v1::NodeKey&, const Node&>{key, node} {}


//
// IteratorImpl
//

class IteratorImpl {
public:
    void increment() {
        ++_keyOrder;
    }

    bool equal(const IteratorImpl& rhs) const {
        return _keyOrder == rhs._keyOrder;
    }

    bool notEqual(const IteratorImpl& rhs) const {
        return _keyOrder != rhs._keyOrder;
    }

    const NodeIteratorValue getValue() const {
        auto& key = *_keyOrder;
        return {key, *_children.at(key)};
    }

    explicit IteratorImpl(ChildKeys::const_iterator keyOrder, const Children& children)
        : _keyOrder{keyOrder}, _children{children} {}

private:
    ChildKeys::const_iterator _keyOrder;
    const Children& _children;
};


//
// NodeIterator
//

NodeIterator::NodeIterator(const NodeImpl* nodeImpl, bool end)
    : _impl{std::make_unique<IteratorImpl>(
          end ? nodeImpl->_keyOrder.end() : nodeImpl->_keyOrder.begin(), nodeImpl->_children)} {}
NodeIterator::~NodeIterator() = default;

void NodeIterator::operator++() {
    return _impl->increment();
}

bool NodeIterator::operator!=(const NodeIterator& rhs) const {
    return _impl->notEqual(*rhs._impl);
}

bool NodeIterator::operator==(const NodeIterator& rhs) const {
    return _impl->equal(*rhs._impl);
}

const NodeIteratorValue NodeIterator::operator*() const {
    return _impl->getValue();
}

}  // namespace genny
