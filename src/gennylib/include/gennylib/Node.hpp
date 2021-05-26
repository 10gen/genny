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

#ifndef HEADER_17681835_40A0_443E_939D_3679A1A6B5DD_INCLUDED
#define HEADER_17681835_40A0_443E_939D_3679A1A6B5DD_INCLUDED

#include <functional>
#include <optional>
#include <type_traits>
#include <utility>
#include <variant>
#include <vector>

#include <boost/exception/all.hpp>
#include <boost/exception/error_info.hpp>
#include <boost/exception/exception.hpp>
#include <boost/log/trivial.hpp>
#include <boost/throw_exception.hpp>

#include <yaml-cpp/yaml.h>

namespace genny {

using UnusedNodes = std::vector<std::string>;

/**
 * Source of all `genny::Node` instances.
 * This must outlive all `Node&`s handed out.
 */
class NodeSource final {
public:
    ~NodeSource();

    /**
     * @return
     *   The root node that represents the whole YAML file/document.
     */
    const class Node& root() const& {
        return *_root;
    }

    // disallow `NodeSource{"",""}.root();`
    void root() const&& = delete;

    /**
     * @param yaml
     *   The full yaml document
     * @param path
     *   Path information. Used in error messages.
     *   Likely a file-path from where the yaml was loaded.
     */
    NodeSource(std::string yaml, std::string path);

    [[nodiscard]]
    UnusedNodes unused() const;

private:
    const YAML::Node _yaml;
    const std::unique_ptr<class Node> _root;
};

/**
 * Specialize this type if you wish to provide
 * a conversion function for `O` but you can't
 * create a new constructor on `O` that takes in
 * a `const Node&` as its first parameter.
 *
 * See documentation on `genny::Node`.
 */
template <typename T>
struct NodeConvert {};

/**
 * @namespace v1
 *   This is not intended to be used directly you should never be typing `v1::`
 *   in Actor or test code.
 */
namespace v1 {

/**
 * The key of a node in the YAML.
 *
 * If you have `const Node& foo = node["Foo"]`, then
 * the key for `foo` is `Foo`. Can be a `long` for
 * sequence types.
 */
class NodeKey final {
public:
    using Path = std::vector<NodeKey>;

    explicit NodeKey(std::string key) : _value{std::move(key)} {};

    explicit NodeKey(long key) : _value{key} {};

    // Needed for storage in std::map
    bool operator<(const NodeKey& rhs) const {
        return _value < rhs._value;
    }

    [[nodiscard]]
    std::string toString() const;

    friend std::ostream& operator<<(std::ostream& out, const ::genny::v1::NodeKey& key) {
        try {
            return out << std::get<std::string>(key._value);
        } catch (const std::bad_variant_access&) {
            return out << std::get<long>(key._value);
        }
    }

private:
    using ValueType = std::variant<long, std::string>;
    const ValueType _value;
};
}  // namespace v1


/**
 * Throw this to indicate a bad conversion.
 */
class InvalidConversionException : public boost::exception, public std::exception {
public:
    InvalidConversionException(const class Node* node,
                               const YAML::BadConversion& yamlException,
                               const std::type_info& destType);

    const char* what() const noexcept override {
        return _what.c_str();
    }

private:
    using Message = boost::error_info<struct Message, std::string>;
    const std::string _what;
};

/**
 * Throw this to indicate bad input yaml syntax.
 */
class InvalidYAMLException : public boost::exception, public std::exception {
public:
    InvalidYAMLException(const std::string& path, const YAML::ParserException& yamlException);

    const char* what() const noexcept override {
        return _what.c_str();
    }

private:
    using Message = boost::error_info<struct Message, std::string>;
    const std::string _what;
};

/**
 * Throw this to indicate a bad path.
 */
class InvalidKeyException : public boost::exception, public std::exception {
public:
    InvalidKeyException(const std::string& msg, const std::string& key, const class Node* node);

    const char* what() const noexcept override {
        return _what.c_str();
    }

private:
    using Message = boost::error_info<struct Message, std::string>;
    const std::string _what;
};


/**
 * Access YAML configuration
 *
 * Example usage:
 *
 * ```c++
 * // use [] to traverse
 * auto bar = node["foo"]["bar"][0];
 *
 * // treat as boolean to see if the value
 * // was specified in the yaml:
 * if (bar) {
 *   // use .to<T>() to convert
 *   std::cout << "bar = " << bar.to<int>();
 * }
 *
 * // or use .maybe<int>().value_or:
 * int w = node["w"].maybe<int>().value_or(1);
 *
 * // convert to built-in APIs like std::vector and std::map:
 * auto ns = node["ns"].to<std::vector<int>>();
 *
 * // iterate over a sequence
 * // e.g. given yaml "ns: [1,2,3]"
 * for(auto [k,v] : node["ns"]) { ... }
 *
 * // or iterate over a map
 * // e.g. given yaml "vals: {a: A, b: B}"
 * for(auto [k,v] : node["vals"]) {
 *     auto key = k.toString();
 *     auto val = v.to<std::string>();
 * }
 *
 * // Or support syntax-sugar for plural values:
 * std::vector<int> nums = node.getPlural<int>("num", "nums");
 * // this allows the user to specify either `num:7` or `nums:[1,2,3]`.
 * // See docs on `getPlural` for more info.
 * ```
 *
 * The API for `genny::Node` is strongly inspired by that of `YAML::Node`,
 * but it provides better error-reporting in the case of invalid
 * configuration and it allows conversion functions to pass in additional
 * arguments.
 *
 * To convert to non-primitive/built-in types you have two options:
 *
 * 1.  Add a constructor to your type that takes a `const Node&` as the
 *     first parameter. You can pass additional constructor-args in the
 *     call to `.to<T>()` or `.maybe<T>()`:
 *
 *     ```c++
 *     struct MyFoo {
 *       MyFoo(const Node& n, int x) {...};
 *     };
 *     MyFoo mf = node["foo"].to<MyFoo>(7);
 *     // calls MyFoo(node["foo"], 7)
 *     ```
 *
 * 2.  Specialize the `genny::NodeConvert` struct for the type:
 *
 *     ```c++
 *     namespace genny {
 *     template<>
 *     struct NodeConvert<MyFoo2> {
 *       // required due to SFINAE
 *       using type = MyFoo2;
 *       static type convert(const Node& node, int x) {
 *         ...
 *       };
 *     };
 *     }  // namespace genny
 *
 *     MyFoo2 mf2 = node["foo2"].to<MyFoo2>(7);
 *     // calls NodeConvert<MyFoo2>::convert(node["foo2"], 7);
 *     ```
 *
 * Whenever possible, prefer the first method of creating a constructor.
 * This allows you to keep all your logic for how to construct your type
 * with your type itself.
 *
 * Note that it is intentionally impossible to convert `genny::Node` into
 * `YAML::Node`.
 */
class Node final {
public:
    // No move or copy
    Node(const Node&) = delete;
    void operator=(const Node&) = delete;
    Node(Node&&) = delete;
    void operator=(Node&&) = delete;

    /** Destruct */
    ~Node();

    /** What type of node are we */
    enum class Type {
        Undefined,
        Null,
        Scalar,
        Sequence,
        Map,
    };

    /**
     * Access children of a sequence.
     *
     * @param index
     *   index of child to access
     * @return
     *   the child node at that key.
     *   If there is no child at the key (e.g. if this Node is a scalar
     *   or the key is outside the list of valid indexes) then will
     *   return a `Node&` that evaluates to false according to `operator bool`.
     */
    const Node& operator[](long index) const;

    /**
     * Access children of a map.
     *
     * @param key
     *   key of child to access
     * @return
     *   the child node at that key.
     *   If there is no child at the key (e.g. if this Node is a scalar
     *   or the Node's keyset doesn't contain `key`) then will
     *   return a `Node&` that evaluates to false according to `operator bool`.
     */
    const Node& operator[](const std::string& key) const;

    /**
     * @return
     *   If this Node is defined.
     *   A node that is defined to be false or even null is still defined.
     */
    explicit operator bool() const;

    /**
     * @return
     *   The type of this Node.
     */
    Type type() const;

    /**
     * @return
     *   If this node is a scalar.
     */
    bool isScalar() const;

    /**
     * @return
     *   If this node is null.
     */
    bool isNull() const;

    /**
     * @return
     *   If this node is a map.
     */
    bool isMap() const;

    /**
     * @return
     *   If this node is a sequence (array).
     */
    bool isSequence() const;

    /**
     * @return
     *   How many children this node has.
     *   Scalar nodes have size zero.
     */
    size_t size() const;

    /**
     * @return
     *   The key that was used to access this Node.
     *   Note that we always return a "stringified"
     *   version of the key even if this Node was
     *   accessed from a sequence. E.g. `node[0]` would
     *   have key `"0"`.
     */
    std::string key() const;

    /**
     * @return
     *   The full path to this Node. Path
     *   elements are separated by `/`.
     */
    std::string path() const;

    /**
     * @return the YAML tag associated with this node.
     */
    std::string tag() const;

    /**
     * Iterate over child elements. If this Node is a scalar, undefined,
     * or an empty map/sequence then `begin() == end()`.
     *
     * Example iteration:
     *
     * ```c++
     * NodeSource ns{"foo:[10,20,30]", "example.yaml"};
     * for(auto& [k,v] : ns["foo"]) {
     *   // k = 0, 1, 2
     *   // v = Node{10}, Node{20}, Node{30}
     * }
     * ```
     *
     * @return start iterator
     */
    class NodeIterator begin() const;

    /**
     * @return end iterator
     */
    class NodeIterator end() const;

    /**
     * @tparam O
     *   output type. Cannot convert to `YAML::Node` (we enforce this at compile-time).
     * @tparam Args
     *   any additional arguments to forward to the `O` constructor in addition to `*this` *or* to
     * pass to the `template<> class genny::NodeConvert<O> { using type=O; static O convert(const
     *   genny::Node& node, Args...) {...} };` impl.
     * @param args
     *   any additional arguments to forward to the `O` constructor in addition to `*this` *or* to
     * pass to the `template<> class genny::NodeConvert<O> { using type=O; static O convert(const
     *   genny::Node& node, Args...) {...} };` impl.
     * @return
     *   A `nullopt` if this node isn't defined.
     *   Else the result of converting this node to O either via its constructor or via the
     *   `NodeConvert<O>::convert` function.
     *   If the value is not specified, this function will return an empty optional
     * (`std::nullopt`).
     */
    template <typename O = Node, typename... Args>
    std::optional<O> maybe(Args&&... args) const {
        // Doesn't seem possible to test these static asserts since you'd get a compiler error
        // (by design) when trying to call node.to<YAML::Node>().
        static_assert(!std::is_same_v<std::decay_t<YAML::Node>, std::decay_t<O>>,
                      "Cannot convert to YAML::Node");
        static_assert(
            // This isn't the most reliable static_assert but hopefully this block
            // makes debugging compiler-errors easier.
            std::is_same_v<decltype(_maybeImpl<O, Args...>(std::forward<Args>(args)...)),
                           std::optional<O>>,
            "Destination type must satisfy at least *one* of the following:\n"
            "\n"
            "1.  is constructible from `const Node&` and the given arguments\n"
            "2.  has a `NodeConvert` struct like the following:\n"
            "\n"
            "        namespace genny {\n"
            "        template<> struct NodeConvert<Foo> {\n"
            "            using type = Foo;\n"
            "            static Foo convert(const Node& node, other, args) { ... }\n"
            "        };\n"
            "        }  // namesace genny\n"
            "\n"
            "3.  is a type built into YAML::Node e.g. int, string, vector<built-in-type>"
            "    etc.");
        if (!*this) {
            return std::nullopt;
        }
        try {
            return _maybeImpl<O, Args...>(std::forward<Args>(args)...);
        } catch (const YAML::BadConversion& x) {
            BOOST_THROW_EXCEPTION(InvalidConversionException(this, x, typeid(O)));
        }
    }

    /**
     * @tparam O
     *   output type
     * @tparam Args
     *   any additional arguments to forward to the `O` constructor in addition to `*this` *or* to
     * pass to the `template<> class genny::NodeConvert<O> { using type=O; static O convert(const
     *   genny::Node& node, Args...) {...} };` impl.
     * @param args
     *   any additional arguments to forward to the `O` constructor in addition to `*this` *or* to
     *   pass to the `template<> class genny::NodeConvert<O> { using type=O; static O convert(const
     *   genny::Node& node, Args...) {...} };` impl.
     * @return
     *   the result of converting this node to O either via its constructor or via the
     *   `NodeConvert<O>::convert` function. If you want to allow a value to be missing, use
     *   `.maybe<O>()`.
     * @throws InvalidKeyException
     *   if key not found
     * @throws InvalidConversionException
     *   if cannot convert to O or if value is not specified
     */
    template <typename O, typename... Args>
    O to(Args&&... args) const {
        auto out = maybe<O, Args...>(std::forward<Args>(args)...);
        if (!out) {
            auto x =
                InvalidKeyException("Tried to access node that doesn't exist.", this->key(), this);
            BOOST_LOG_TRIVIAL(fatal) << x.what();
            BOOST_THROW_EXCEPTION(x);
        }
        return std::move(*out);
    }

    /**
     * Extract a vector of items by supporting both singular and plural keys.
     *
     * Example YAML that this supports:
     *
     * ```yaml
     * # Calling getPlural<int>("Number","Numbers") returns [7]
     * Foo:
     *   Number: 7
     *
     * # Calling getPlural<int>("Number","Numbers") returns [1,2]
     * Bar:
     *   Numbers: [1,2]
     * ```
     *
     * The node cannot have both keys present. The following
     * will error:
     *
     * ```yaml
     * # Calling getPlural<int>("Bad","Bads")
     * # will throw because the node must have
     * # exactly one of the keys
     * BadExample:
     *   Bad: 7
     *   Bads: [1,2]
     * ```
     *
     * If the value at the plural key isn't a sequence we also barf:
     *
     * ```yaml
     * # Calling getPlural<int>("Bad","Bads") will fail
     * # because Bads isn't a sequence.
     * AnotherBadExample:
     *   Bads: 3
     * ```
     *
     * @tparam T
     *   the returned type. Must align with the return-type of `F`.
     * @tparam F
     *   type of the callback/mapping function that maps from `Node` to `T`.
     * @param singular
     *   the singular version of the key e.g. "Number"
     * @param plural
     *   the plural version of the key e.g. "Numbers"
     * @param f
     *   callback function mapping from the found node to a `T` instance. If not specified,
     *   will use `to<T>()`
     * @return
     *   a `vector<T>()` formed by applying `f` to each item in the sequence found at `this[plural]`
     *   or, if that is not defined, by applying `f` to the single-item sequence `[ this[singular]
     * ]`.
     */
    // The implementation is below because we require the full class definition
    // within the implementation.
    template <typename T, typename F = std::function<T(const Node&)>>
    std::vector<T> getPlural(
        const std::string& singular,
        const std::string& plural,
        // Default conversion function is `node.to<T>()`.
        F&& f = [](const Node& n) { return n.to<T>(); }) const;

    friend std::ostream& operator<<(std::ostream& out, const Node& node);

    // Only intended to be used internally
    explicit Node(const v1::NodeKey::Path& path, const YAML::Node yaml);


    [[nodiscard]]
    UnusedNodes unused() const;

private:
    friend class NodeImpl;

    const YAML::Node yaml() const;

    template <typename O, typename... Args>
    static constexpr bool isNodeConstructible() {
        // exclude is_trivially_constructible_v values because for some reason `int` and
        // other primitives report as is_constructible here
        return !std::is_trivially_constructible_v<O> &&
            std::is_constructible_v<O, const Node&, Args...>;
    }

    // Simple case where we have `O(const Node&,Args...)` constructor.
    template <typename O,
              typename... Args,
              typename = std::enable_if_t<isNodeConstructible<O, Args...>()>>
    std::optional<O> _maybeImpl(Args&&... args) const {
        // If you get compiler errors in _maybeImpl, see the note on the public maybe() function.
        return std::make_optional<O>(*this, std::forward<Args>(args)...);
    }

    template <typename O,
              typename... Args,
              typename = std::enable_if_t<
                  // don't have an `O(const Node&, Args...)` constructor
                  !isNodeConstructible<O, Args...>() &&
                  // rely on sfinae to determine if we have a `NodeConvert<O>` definition.
                  std::is_same_v<O, typename NodeConvert<O>::type> &&
                  // a bit pedantic but also require that we can call
                  // `NodeConvert<O>::convert` and get back an O.
                  std::is_same_v<O,
                                 decltype(NodeConvert<O>::convert(std::declval<Node>(),
                                                                  std::declval<Args>()...))>>,
              typename = void>
    std::optional<O> _maybeImpl(Args&&... args) const {
        // If you get compiler errors in _maybeImpl, see the note on the public maybe() function.
        return std::make_optional<O>(NodeConvert<O>::convert(*this, std::forward<Args>(args)...));
    }

    // This will get used if the ↑ fails to instantiate due to SFINAE.
    template <
        typename O,
        typename... Args,
        typename = std::enable_if_t<
            // don't have an `O(const Node&, Args...)` constructor
            !isNodeConstructible<O, Args...>() &&
            // and the `YAML::convert<O>` struct has been defined.
            std::is_same_v<decltype(YAML::convert<O>::encode(std::declval<O>())), YAML::Node>>,
        typename = void,
        typename = void>
    std::optional<O> _maybeImpl(Args&&... args) const {
        static_assert(sizeof...(args) == 0,
                      "Cannot pass additional args when using built-in YAML conversion");
        return std::make_optional<O>(yaml().as<O>());
    }

    const std::unique_ptr<const class NodeImpl> _impl;
};

/**
 * The `[k,v]` used when doing `for(auto& [k,v] : node)`
 */
class NodeIteratorValue final : public std::pair<const v1::NodeKey&, const Node&> {
public:
    NodeIteratorValue(const v1::NodeKey& key, const Node& node);
};

/**
 * Iterates over nodes. See `Node::begin()` and `Node::end()`.
 */
class NodeIterator final {
public:
    ~NodeIterator();

    bool operator!=(const NodeIterator&) const;

    bool operator==(const NodeIterator&) const;

    void operator++();

    const NodeIteratorValue operator*() const;

private:
    friend Node;

    NodeIterator(const class NodeImpl*, bool end);

    const std::unique_ptr<class IteratorImpl> _impl;
};


// A bulk of this could probably be moved to the PiMPL version
// in Node.cpp. Would just need `vector<const Node&> Node::getPlural(s,p)`
// and then map f over the result.
template <typename T, typename F>
std::vector<T> Node::getPlural(const std::string& singular,
                               const std::string& plural,
                               F&& f) const {
    std::vector<T> out;
    const auto& pluralValue = (*this)[plural];
    const auto& singValue = (*this)[singular];
    if (pluralValue && singValue) {
        BOOST_THROW_EXCEPTION(
            // the `$plural(singular,plural)` key is kinda cheeky but hopefully
            // it helps to explain what the code tried to do in error-messages.
            InvalidKeyException("Can't have both '" + singular + "' and '" + plural + "'.",
                                "getPlural('" + singular + "', '" + plural + "')",
                                this));
    } else if (pluralValue) {
        if (!pluralValue.isSequence()) {
            BOOST_THROW_EXCEPTION(
                InvalidKeyException("Plural '" + plural + "' must be a sequence type.",
                                    "getPlural('" + singular + "', '" + plural + "')",
                                    this));
        }
        for (auto&& [k, v] : pluralValue) {
            auto&& created = std::invoke(f, v);
            out.emplace_back(std::move(created));
        }
    } else if (singValue) {
        auto&& created = std::invoke(f, singValue);
        out.emplace_back(std::move(created));
    } else if (!singValue && !pluralValue) {
        BOOST_THROW_EXCEPTION(
            InvalidKeyException("Either '" + singular + "' or '" + plural + "' required.",
                                "getPlural('" + singular + "', '" + plural + "')",
                                this));
    }
    return out;
}

}  // namespace genny

#endif  // HEADER_17681835_40A0_443E_939D_3679A1A6B5DD_INCLUDED
