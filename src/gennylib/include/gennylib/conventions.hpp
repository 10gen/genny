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

#ifndef HEADER_CC9B7EF0_9FB9_4AD4_B64C_DC7AE48F72A6_INCLUDED
#define HEADER_CC9B7EF0_9FB9_4AD4_B64C_DC7AE48F72A6_INCLUDED

#include <chrono>
#include <climits>
#include <cmath>
#include <sstream>

#include <mongocxx/read_concern.hpp>
#include <mongocxx/read_preference.hpp>
#include <mongocxx/write_concern.hpp>

#include <gennylib/InvalidConfigurationException.hpp>
#include <gennylib/Node.hpp>
#include <gennylib/Orchestrator.hpp>

namespace genny {

/**
 * This function converts a genny::Node into a given type and uses a given fallback.
 * It simplifies a common pattern where you have a member variable that needs to be assigned either
 * the value in a node or a fallback value (traditionally, this involves at least a decltype).
 */
template <typename T, typename S>
void decodeNodeInto(T& out, const genny::Node& node, const S& fallback) {
    out = node.maybe<T>().value_or(fallback);
}

/**
 * Intermediate state for converting YAML syntax into a native integer type of your choice.
 *
 * int64_t is used by default; smaller types can be explicitly converted to as needed.
 */
struct IntegerSpec {
    IntegerSpec() = default;
    ~IntegerSpec() = default;

    IntegerSpec(int64_t v) : value{v} {}
    // int64_t is used by default, you can explicitly cast to another type if needed.
    int64_t value;

    operator int64_t() const {  // NOLINT(google-explicit-constructor)
        return value;
    }
};

inline bool operator==(const IntegerSpec& lhs, const IntegerSpec& rhs) {
    return lhs.value == rhs.value;
}

/**
 * Intermediate state for converting YAML syntax into a native std::chrono::duration type of your
 * choice.
 */
struct TimeSpec {
public:  // Typedefs/Structs
    // Use the highest precision internally.
    using ValueT = std::chrono::nanoseconds;

public:  // Ctors/Dtors/Generators
    ~TimeSpec() = default;

    explicit constexpr TimeSpec(ValueT v) : value{v} {}

    // Allow construction with integers for testing.
    explicit constexpr TimeSpec(int64_t v) : TimeSpec{ValueT(v)} {}

    // Default construct as a zero value
    constexpr TimeSpec() : TimeSpec(ValueT::zero()) {}

public:  // Members
    ValueT value;

public:  // Operators
    /* not explicit */ operator ValueT() const {
        return value;
    }

    [[nodiscard]] constexpr auto count() const {
        return value.count();
    }

    explicit constexpr operator std::chrono::seconds() const {
        return std::chrono::duration_cast<std::chrono::seconds>(value);
    }

    explicit constexpr operator std::chrono::milliseconds() const {
        return std::chrono::duration_cast<std::chrono::milliseconds>(value);
    }

    explicit constexpr operator bool() const {
        return count() != 0;
    }
};

inline bool operator==(const TimeSpec& lhs, const TimeSpec& rhs) {
    return lhs.value == rhs.value;
}

// Use the underlying type in TimeSpec as the default Duration type
using Duration = typename TimeSpec::ValueT;

/**
 * BaseRateSpec defined as X operations per Y duration.
 */
struct BaseRateSpec {
    BaseRateSpec() = default;
    ~BaseRateSpec() = default;

    BaseRateSpec(TimeSpec t, IntegerSpec i) : per{t.count()}, operations{i.value} {}

    // Allow construction with integers for testing.
    BaseRateSpec(int64_t t, int64_t i) : per{t}, operations{i} {}
    std::chrono::nanoseconds per;
    int64_t operations;
};

inline bool operator==(const BaseRateSpec& lhs, const BaseRateSpec& rhs) {
    return (lhs.per == rhs.per) && (lhs.operations == rhs.operations);
}

/**
 * PercentileRateSpec defined as X% of max throughput, where X is a whole number.
 */
struct PercentileRateSpec {
    PercentileRateSpec() = default;
    ~PercentileRateSpec() = default;

    PercentileRateSpec(IntegerSpec i) : percent{i.value} {}

    // Allow construction with integers for testing.
    PercentileRateSpec(int64_t i) : percent{i} {}
    int64_t percent;
};

inline bool operator==(const PercentileRateSpec& lhs, const PercentileRateSpec& rhs) {
    return (lhs.percent == rhs.percent);
}

/**
 * RateSpec defined as either X operations per Y duration or Z% of max throughput each phase.
 */
class RateSpec {
public:
    RateSpec() = default;
    ~RateSpec() = default;

    RateSpec(BaseRateSpec s) : _spec{s} {}

    RateSpec(PercentileRateSpec s) : _spec{s} {}

    [[nodiscard]] std::optional<BaseRateSpec> getBaseSpec() const {
        if (auto pval = std::get_if<BaseRateSpec>(&_spec)) {
            return *pval;
        } else {
            return std::nullopt;
        }
    }

    [[nodiscard]] std::optional<PercentileRateSpec> getPercentileSpec() const {
        if (auto pval = std::get_if<PercentileRateSpec>(&_spec)) {
            return *pval;
        } else {
            return std::nullopt;
        }
    }

    bool operator==(const RateSpec& rhs) {
        // Equality is well-behaved for variants if it is for their contents.
        return _spec == rhs._spec;
    }

private:
    std::variant<std::monostate, BaseRateSpec, PercentileRateSpec> _spec;
};


struct PhaseRangeSpec {
    PhaseRangeSpec() = default;
    ~PhaseRangeSpec() = default;

    PhaseRangeSpec(genny::IntegerSpec s, genny::IntegerSpec e)
        : start{static_cast<genny::PhaseNumber>(s.value)},
          end{static_cast<genny::PhaseNumber>(e.value)} {
        if (!(s.value >= 0 && s.value <= UINT_MAX)) {
            std::stringstream msg;
            msg << "Invalid start value for genny::PhaseRangeSpec: '" << s.value << "'."
                << " The value must be of type 'u_int32_t";
            throw genny::InvalidConfigurationException(msg.str());
        }
        if (!(e.value >= 0 && e.value <= UINT_MAX)) {
            std::stringstream msg;
            msg << "Invalid end value for genny::PhaseRangeSpec: '" << e.value << "'."
                << " The value must be of type 'u_int32_t";
            throw genny::InvalidConfigurationException(msg.str());
        }
    }
    PhaseRangeSpec(genny::IntegerSpec s) : PhaseRangeSpec(s, s) {}

    genny::PhaseNumber start;
    genny::PhaseNumber end;
};

using genny::decodeNodeInto;

template <>
struct NodeConvert<mongocxx::read_preference> {
    using type = mongocxx::read_preference;
    using ReadMode = mongocxx::read_preference::read_mode;

    static type convert(const Node& node) {
        type rhs{};
        auto readMode = node["ReadMode"].to<std::string>();
        if (readMode == "primary") {
            rhs.mode(ReadMode::k_primary);
        } else if (readMode == "primaryPreferred") {
            rhs.mode(ReadMode::k_primary_preferred);
        } else if (readMode == "secondary") {
            rhs.mode(ReadMode::k_secondary);
        } else if (readMode == "secondaryPreferred") {
            rhs.mode(ReadMode::k_secondary_preferred);
        } else if (readMode == "nearest") {
            rhs.mode(ReadMode::k_nearest);
        } else {
            std::stringstream msg;
            msg << "Unknown read mode " << readMode;
            BOOST_THROW_EXCEPTION(InvalidConfigurationException(msg.str()));
        }
        if (node["MaxStaleness"]) {
            auto maxStaleness = node["MaxStaleness"].to<genny::TimeSpec>();
            rhs.max_staleness(std::chrono::seconds{maxStaleness});
        }
        return rhs;
    }
};

template <>
struct NodeConvert<mongocxx::write_concern> {
    using type = mongocxx::write_concern;

    static type convert(const Node& node) {
        type rhs{};
        try {
            auto level = node["Level"].to<int>();
            rhs.nodes(level);
        } catch (const InvalidConversionException& e) {
            auto level = node["Level"].to<std::string>();
            if (level == "majority") {
                rhs.majority(std::chrono::milliseconds{0});
            } else {
                std::stringstream msg;
                msg << "Unknown writeConcern " << level;
                BOOST_THROW_EXCEPTION(InvalidConfigurationException(msg.str()));
            }
        }
        if (node["Timeout"]) {
            auto timeout = node["Timeout"].to<genny::TimeSpec>();
            rhs.timeout(std::chrono::milliseconds{timeout});
        }
        if (node["Journal"]) {
            auto journal = node["Journal"].to<bool>();
            rhs.journal(journal);
        }
        return rhs;
    }
};

template <>
struct NodeConvert<mongocxx::read_concern> {
    using type = mongocxx::read_concern;

    static bool isValidReadConcernString(std::string_view rcString) {
        return (rcString == "local" || rcString == "majority" || rcString == "linearizable" ||
                rcString == "snapshot" || rcString == "available");
    }

    static type convert(const Node& node) {
        type rhs{};
        auto level = node["Level"].to<std::string>();
        if (!isValidReadConcernString(level)) {
            std::stringstream msg;
            msg << "Unknown read read concern " << level;
            BOOST_THROW_EXCEPTION(InvalidConfigurationException(msg.str()));
        }

        return rhs;
    }
};

/**
 * Convert between YAML and genny::PhaseRange
 *
 * The YAML syntax accepts "[genny::Integer]..[genny::Integer]".
 * This is used to stipulate repeating a phase N number of times
 */
template <>
struct NodeConvert<genny::PhaseRangeSpec> {
    using type = genny::PhaseRangeSpec;

    static type convert(const Node& node) {
        type rhs{};
        auto strRepr = node.to<std::string>();

        // use '..' as delimiter.
        constexpr std::string_view delimiter = "..";
        auto delimPos = strRepr.find(delimiter);

        if (delimPos == std::string::npos) {
            // check if user inputted an integer
            try {
                auto phaseNumberYaml = node.to<genny::IntegerSpec>();
                rhs = genny::PhaseRangeSpec(phaseNumberYaml);
                return rhs;
            } catch (const genny::InvalidConfigurationException& e) {
                std::stringstream msg;
                msg << "Invalid value for genny::PhaseRangeSpec: '" << strRepr << "'."
                    << " The correct syntax is either a single integer or two integers delimited "
                       "by '..'";
                BOOST_THROW_EXCEPTION(genny::InvalidConfigurationException(msg.str()));
            }
        }

        genny::IntegerSpec start{};
        genny::IntegerSpec end{};

        auto startYaml = NodeSource(strRepr.substr(0, delimPos), node.path());
        auto endYaml = NodeSource(strRepr.substr(delimPos + delimiter.size()), node.path());

        try {
            start = startYaml.root().to<genny::IntegerSpec>();
            end = endYaml.root().to<genny::IntegerSpec>();
        } catch (const genny::InvalidConfigurationException& e) {
            std::stringstream msg;
            msg << "Invalid value for genny::PhaseRangeSpec: '" << strRepr << "'."
                << " The correct syntax is two integers delimited by '..'";
            BOOST_THROW_EXCEPTION(genny::InvalidConfigurationException(msg.str()));
        }

        if (start > end) {
            std::stringstream msg;
            msg << "Invalid value for genny::PhaseRangeSpec: '" << strRepr << "'."
                << " The start value cannot be greater than the end value.";
            BOOST_THROW_EXCEPTION(genny::InvalidConfigurationException(msg.str()));
        }
        rhs = genny::PhaseRangeSpec(start, end);

        return rhs;
    }
};

/**
 * Convert between YAML and genny::BaseRateSpec
 *
 * The YAML syntax accepts [genny::Integer] per [genny::Time]
 * The syntax is interpreted as operations per unit of time.
 */
template <>
struct NodeConvert<genny::BaseRateSpec> {
    using type = genny::BaseRateSpec;

    static type convert(const Node& node) {
        type rhs{};
        auto strRepr = node.to<std::string>();

        // Use space as the delimiter.
        const std::string delimiter = " per ";
        auto spacePos = strRepr.find(delimiter);

        if (spacePos == std::string::npos) {
            std::stringstream msg;
            msg << "Invalid value for genny::TimeSpec field, expected a space separated integer "
                   "and "
                   "time unit. Saw: "
                << strRepr;
            throw genny::InvalidConfigurationException(msg.str());
        }

        auto opCountYaml = NodeSource(strRepr.substr(0, spacePos), node.path());
        auto opCount = opCountYaml.root().to<genny::IntegerSpec>();

        auto timeUnitYaml = NodeSource(strRepr.substr(spacePos + delimiter.size()), node.path());
        auto timeUnit = timeUnitYaml.root().to<genny::TimeSpec>();

        rhs = genny::BaseRateSpec(timeUnit, opCount);

        return rhs;
    }
};

/**
 * Convert between YAML and genny::PercentileRateSpec
 *
 * The YAML syntax accepts [genny::Integer]%
 * The syntax is interpreted as a percentage of the max throughput.
 */
template <>
struct NodeConvert<genny::PercentileRateSpec> {
    using type = genny::PercentileRateSpec;

    static type convert(const Node& node) {
        type rhs{};
        auto strRepr = node.to<std::string>();

        // Use percent as the delimiter.
        const std::string delimiter = "%";
        auto percentPos = strRepr.find(delimiter);

        if (percentPos == std::string::npos || percentPos != strRepr.size() - 1) {
            std::stringstream msg;
            msg << "Invalid value for PercentileRateSpec field, expected an integer followed by %."
                   " Saw: "
                << strRepr;
            BOOST_THROW_EXCEPTION(genny::InvalidConfigurationException(msg.str()));
        }

        auto percentYaml = NodeSource(strRepr.substr(0, percentPos), node.path());
        auto percent = percentYaml.root().to<genny::IntegerSpec>();

        if (percent.value > 100) {
            std::stringstream msg;
            msg << "Invalid value for PercentileRateSpec field, integer must be between 0 and 100, "
                   "inclusive."
                   " Saw: "
                << percent.value;
            BOOST_THROW_EXCEPTION(genny::InvalidConfigurationException(msg.str()));
        }

        rhs = genny::PercentileRateSpec(percent);

        return rhs;
    }
};

/**
 * Convert between YAML and genny::RateSpec
 *
 * The YAML syntax accepts either [genny::Integer] per [genny::Time]
 * or [genny::Integer]%
 *
 * The syntax is interpreted as operations per unit of time or
 * percentage of max throughput.
 */
template <>
struct NodeConvert<genny::RateSpec> {
    using type = genny::RateSpec;

    static type convert(const Node& node) {
        type rhs{};
        auto strRepr = node.to<std::string>();
        auto nodeYaml = NodeSource(strRepr, node.path());

        // First treat as a BaseRateSpec, then try as a PercentileRateSpec.
        try {
            auto baseSpec = nodeYaml.root().to<genny::BaseRateSpec>();
            rhs = genny::RateSpec(baseSpec);
            return rhs;
        } catch (const genny::InvalidConfigurationException& e) {
        }

        try {
            auto percentileSpec = nodeYaml.root().to<genny::PercentileRateSpec>();
            rhs = genny::RateSpec(percentileSpec);
            return rhs;
        } catch (const genny::InvalidConfigurationException& e) {
        }

        std::stringstream msg;
        msg << "Invalid value for RateSpec field, expected a space separated integer and time unit,"
            << " or integer followed by %. Saw: " << strRepr;
        BOOST_THROW_EXCEPTION(genny::InvalidConfigurationException(msg.str()));
    }
};


/**
 * Convert between YAML and genny::Integer
 *
 * The YAML syntax accepts regular and scientific notation decimal values.
 */
template <>
struct NodeConvert<genny::IntegerSpec> {
    using type = genny::IntegerSpec;

    static type convert(const Node& node) {
        type rhs{};

        auto strRepr = node.to<std::string>();
        size_t pos = 0;

        // Use double here to support the scientific notation.
        double_t num = std::stod(strRepr, &pos);

        if (pos != strRepr.length() || std::llround(num) != num) {
            std::stringstream msg;
            msg << "Invalid value for genny::IntegerSpec field: " << strRepr
                << " from config: " << strRepr;
            BOOST_THROW_EXCEPTION(genny::InvalidConfigurationException(msg.str()));
        }

        if (num < 0) {
            std::stringstream msg;
            msg << "Value for genny::IntegerSpec can't be negative: " << num
                << " from config: " << strRepr;
            throw genny::InvalidConfigurationException(msg.str());
        }

        rhs = genny::IntegerSpec(std::llround(num));

        return rhs;
    }
};


/**
 * Convert between YAML and genny::Time.
 *
 * The YAML syntax looks like [genny::Integer] [milliseconds/microseconds/etc...]
 */
template <>
struct NodeConvert<genny::TimeSpec> {
    using type = genny::TimeSpec;

    static type convert(const Node& node) {
        type rhs{};
        auto strRepr = node.to<std::string>();

        // Use space as the delimiter.
        auto spacePos = strRepr.find_first_of(' ');

        if (spacePos == std::string::npos) {
            std::stringstream msg;
            msg << "Invalid value for genny::TimeSpec field, expected a space separated integer "
                   "and "
                   "time unit. Saw: "
                << strRepr;
            BOOST_THROW_EXCEPTION(genny::InvalidConfigurationException(msg.str()));
        }

        NodeSource timeCountYaml{strRepr.substr(0, spacePos), node.path()};
        auto timeCount = timeCountYaml.root().to<genny::IntegerSpec>().value;

        auto timeUnit = strRepr.substr(spacePos + 1);

        // Use string::find here so plurals get parsed correctly.
        if (timeUnit.find("nanosecond") == 0) {
            rhs = genny::TimeSpec{std::chrono::nanoseconds(timeCount)};
        } else if (timeUnit.find("microsecond") == 0) {
            rhs = genny::TimeSpec{std::chrono::microseconds(timeCount)};
        } else if (timeUnit.find("millisecond") == 0) {
            rhs = genny::TimeSpec{std::chrono::milliseconds(timeCount)};
        } else if (timeUnit.find("second") == 0) {
            rhs = genny::TimeSpec{std::chrono::seconds(timeCount)};
        } else if (timeUnit.find("minute") == 0) {
            rhs = genny::TimeSpec{std::chrono::minutes(timeCount)};
        } else if (timeUnit.find("hour") == 0) {
            rhs = genny::TimeSpec{std::chrono::hours(timeCount)};
        } else {
            std::stringstream msg;
            msg << "Invalid unit: " << timeUnit
                << " for genny::TimeSpec field in config: " << strRepr;
            BOOST_THROW_EXCEPTION(genny::InvalidConfigurationException(msg.str()));
        }

        return rhs;
    }
};

}  // namespace genny


#endif  // HEADER_CC9B7EF0_9FB9_4AD4_B64C_DC7AE48F72A6_INCLUDED
