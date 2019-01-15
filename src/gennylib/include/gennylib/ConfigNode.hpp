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

#ifndef HEADER_F4822E1D_5AE5_4A00_B6BF_F26F05C1AC55_INCLUDED
#define HEADER_F4822E1D_5AE5_4A00_B6BF_F26F05C1AC55_INCLUDED

#include <functional>
#include <iterator>
#include <optional>
#include <type_traits>
#include <vector>

#include <yaml-cpp/yaml.h>

#include <gennylib/InvalidConfigurationException.hpp>

namespace genny::V1 {

/**
 * If Required, type is Out, else it's optional<Out>
 */
template <class Out, bool Required>
using MaybeOptionalT = typename std::conditional<Required, Out, std::optional<Out>>::type;

/**
 * The "path" to a configured value. E.g. given the structure
 *
 * ```yaml
 * foo:
 *   bar:
 *     baz: [10,20,30]
 * ```
 *
 * The path to the 10 is "foo/bar/baz/0".
 *
 * This is used to report meaningful exceptions in the case of mis-configuration.
 */
class ConfigPath {
public:
    using value_type = std::function<void(std::ostream&)>;

    ConfigPath() = default;

    ConfigPath(ConfigPath&) = delete;
    void operator=(ConfigPath&) = delete;

    void add(const value_type& elt) {
        _elements.push_back(elt);
    }

    auto begin() const {
        return std::begin(_elements);
    }

    auto end() const {
        return std::end(_elements);
    }

private:
    /**
     * The parts of the path, so for this structure
     *
     * ```yaml
     * foo:
     *   bar: [bat, baz]
     * ```
     *
     * If this `ConfigPath` represents "baz", then `_elements`
     * will be `["foo", "bar", 1]`.
     *
     * To be "efficient" we only store a `function` that produces the
     * path component string; do this to avoid (maybe) expensive
     * string-formatting in the "happy case" where the ConfigPath is
     * never fully serialized to an exception.
     */
    std::vector<value_type> _elements;
};

// Support putting ConfigPaths onto ostreams
inline std::ostream& operator<<(std::ostream& out, const ConfigPath& path) {
    for (const auto& f : path) {
        f(out);
        out << "/";
    }
    return out;
}

class ConfigNode {
public:
    ConfigNode(YAML::Node node, const ConfigNode* delegateNode = nullptr)
        : _node(std::move(node)), _delegateNode(delegateNode) {}

    /**
     * Retrieve configuration values from the top-level `_node` configuration.
     * Returns `_node[arg1][arg2]...[argN]`.
     *
     * @tparam Out the output type required. Will forward to `YAML::Node.as<Out>()`
     * @tparam Required If true, will error if item not found. If false, will return an
     * `std::optional<Out>` that will be empty if not found.
     */
    template <class Out = YAML::Node, bool Required = true, class... Args>
    MaybeOptionalT<Out, Required> get_noinherit(Args&&... args) const {
        ConfigPath path;
        auto node = get_helper<Required>(path, _node, std::forward<Args>(args)...);

        if (!node.IsDefined()) {
            if constexpr (Required) {
                std::stringstream error;
                error << "Invalid key at path [" << path << "]";
                throw InvalidConfigurationException(error.str());
            } else {
                return std::nullopt;
            }
        }

        try {
            if constexpr (Required) {
                return node.template as<Out>();
            } else {
                return std::make_optional<Out>(node.template as<Out>());
            }
        } catch (const YAML::BadConversion& conv) {
            std::stringstream error;
            // typeid(Out).name() is kinda hokey but could be useful when debugging config issues.
            error << "Bad conversion of [" << node << "] to [" << typeid(Out).name() << "] "
                  << "at path [" << path << "]: " << conv.what();
            throw InvalidConfigurationException(error.str());
        }
    };

    /**
     * Retrieve configuration values from an inner `_node` configuration.
     * If `_node[arg1][arg2]...[argN]` isn't specified directly, then the value from
     * `_delegateNode.get(arg1, arg2, ..., argN)` is used, if present.
     *
     * @tparam Out the output type required. Will forward to `YAML::Node.as<Out>()`
     * @tparam Required If true, will error if item not found. If false, will return an
     * `std::optional<Out>` that will be empty if not found.
     */
    template <typename Out = YAML::Node, bool Required = true, class... Args>
    MaybeOptionalT<Out, Required> get(Args&&... args) const {
        if (!this->_delegateNode) {
            return get_noinherit<Out, Required>(std::forward<Args>(args)...);
        }

        // try to extract from own node
        auto fromSelf = get_noinherit<Out, false>(std::forward<Args>(args)...);
        if (fromSelf) {
            if constexpr (Required) {
                // unwrap from optional<T>
                return *fromSelf;
            } else {
                // don't unwrap, return the optional<T> itself
                return fromSelf;
            }
        }

        // fallback to delegate node
        return this->_delegateNode->template get<Out, Required>(std::forward<Args>(args)...);
    };

protected:
    const YAML::Node _node;

private:
    // This is the base-case when we're out of Args... expansions in the other helper below
    template <bool Required>
    static YAML::Node get_helper(const ConfigPath& parent, const YAML::Node& curr) {
        return curr;
    }

    // Recursive case where we pick off first item and recurse:
    //      get_helper(foo, a, b, c) // this fn
    //   -> get_helper(foo[a], b, c) // this fn
    //   -> get_helper(foo[a][b], c) // this fn
    //   -> get_helper(foo[a][b][c]) // "base case" fn above
    template <bool Required, class PathFirst, class... PathRest>
    static YAML::Node get_helper(ConfigPath& parent,
                                 const YAML::Node& curr,
                                 PathFirst&& pathFirst,
                                 PathRest&&... rest) {
        if (curr.IsScalar()) {
            std::stringstream error;
            error << "Wanted [" << parent << pathFirst << "] but [" << parent << "] is scalar: ["
                  << curr << "]";
            throw InvalidConfigurationException(error.str());
        }

        const auto& next = curr[std::forward<PathFirst>(pathFirst)];
        parent.add([&](std::ostream& out) { out << pathFirst; });

        if (!next.IsDefined()) {
            if constexpr (Required) {
                std::stringstream error;
                error << "Invalid key [" << pathFirst << "] at path [" << parent
                      << "]. Last accessed [" << curr << "].";
                throw InvalidConfigurationException(error.str());
            } else {
                return next;
            }
        }

        return get_helper<Required>(parent, next, std::forward<PathRest>(rest)...);
    }

    const ConfigNode* _delegateNode;
};

}  // namespace genny::V1

#endif  // HEADER_F4822E1D_5AE5_4A00_B6BF_F26F05C1AC55_INCLUDED
