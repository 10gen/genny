#include <gennylib/Node.hpp>

#include <catch2/catch.hpp>

#include <map>
#include <vector>

namespace genny {

struct EmptyStruct {};

struct ExtractsMsg {
    std::string msg;

    ExtractsMsg(const Node& node) : msg{node["msg"].to<std::string>()} {}
};

struct TakesEmptyStructAndExtractsMsg {
    std::string msg;

    TakesEmptyStructAndExtractsMsg(const Node& node, EmptyStruct*)
        : msg{node["msg"].to<std::string>()} {}
};

struct RequiresParamToEqualNodeX {
    RequiresParamToEqualNodeX(const Node& node, int x) {
        REQUIRE(node["x"].to<int>() == x);
    }

    RequiresParamToEqualNodeX(int any) {}
};

struct HasConversionSpecialization {
    int x;
};

// namespace genny {

TEST_CASE("Unused Values for strict mode") {
    const NodeSource n{R"(
a: [1, 2, 3]
b: false
c: []
n: { ested: [v, alue] }
t: { value: 11 }
)", ""};
    const auto& r = n.root();

    const auto noneUsed = UnusedNodes{// We do depth-first.
        "/a/0", "/a/1", "/a/2", "/b", "/c", "/n/ested/0", "/n/ested/1", "/t/value"
    };

    const auto onlyUsed = [=](std::initializer_list<std::string> ks) -> UnusedNodes {
        auto out = noneUsed;
        for (auto&& k : ks) {
            out.erase(std::remove_if(
                          out.begin(), out.end(), [&](const std::string& c) { return c == k; }),
                      out.end());
        }
        return out;
    };

    SECTION("Only Root Used") {
        REQUIRE(n.unused() == noneUsed);
    }
    SECTION("List used but no items used") {
        REQUIRE(r["a"]);
        REQUIRE(n.unused() == noneUsed);
    }
    SECTION("List of ints used via convert structs only uses the list not the items") {
        // This is arguably a bug, but working around it is tedious.
        //
        // In the case of .to<X>, yaml-cpp's built-in conversions
        // for std containers doesn't consult with genny::Node.
        REQUIRE(r["a"].to<std::vector<int>>() == std::vector<int>{1, 2, 3});
        // We'd really like this to be the same REQUIRE as in the "All items in a list used" case.
        REQUIRE(n.unused() == onlyUsed({"/a"}));
    }
    SECTION("Multiple items in a list used") {
        REQUIRE(r["a"][0].to<int>() == 1);
        REQUIRE(r["a"][1].to<int>() == 2);
        // Note we still didn't use "/a" despite using some of its children
        REQUIRE(n.unused() == onlyUsed({"/a/0", "/a/1"}));
    }
    SECTION("All items in a list used") {
        REQUIRE(r["a"][0].to<int>() == 1);
        REQUIRE(r["a"][1].to<int>() == 2);
        REQUIRE(r["a"][2].to<int>() == 3);
        // We used all the children so we used "a" as well.
        REQUIRE(n.unused() == onlyUsed({"/a/0", "/a/1", "/a/2", "/a"}));
    }
    SECTION("Use a false value") {
        REQUIRE(r["b"].to<bool>() == false);
        REQUIRE(n.unused() == onlyUsed({"/b"}));
    }
    SECTION("Use an empty list") {
        REQUIRE(r["c"].to<std::vector<int>>().empty());
        REQUIRE(n.unused() == onlyUsed({"/c"}));
    }
    SECTION("Use one nested value") {
        REQUIRE(r["n"]["ested"][1].to<std::string>() == "alue");
        REQUIRE(n.unused() == onlyUsed({"/n/ested/1"}));
    }
    SECTION("Use all nested values") {
        REQUIRE(r["n"]["ested"][0].to<std::string>() == "v");
        REQUIRE(r["n"]["ested"][1].to<std::string>() == "alue");
        REQUIRE(n.unused() == onlyUsed({"/n/ested/0", "/n/ested/1", "/n/ested", "/n"}));
    }
    SECTION("Use entire doc") {
        REQUIRE(r["a"][0].to<int>() == 1);
        REQUIRE(r["a"][1].to<int>() == 2);
        REQUIRE(r["a"][2].to<int>() == 3);
        REQUIRE(r["b"].to<bool>() == false);
        REQUIRE(r["c"].to<std::vector<int>>().empty());
        REQUIRE(r["n"]["ested"][0].to<std::string>() == "v");
        REQUIRE(r["n"]["ested"][1].to<std::string>() == "alue");
        REQUIRE(r["t"]["value"].to<int>() == 11);
        REQUIRE(r.unused().empty());
    }
    SECTION("Non-existent key used") {
        auto m = r["does not exist"].maybe<int>();
        REQUIRE(!m);
        REQUIRE(n.unused() == noneUsed);
    }
    SECTION("Non-existent nested key used") {
        auto m = r["does"]["not"]["ex"][1]["st"].maybe<int>();
        REQUIRE(!m);
        REQUIRE(n.unused() == noneUsed);
    }
    SECTION("Not unwrapping a maybe is fine.") {
        auto m = r["b"].maybe<bool>();
        REQUIRE(m);
        REQUIRE(n.unused() == onlyUsed({"/b"}));
    }
    struct MyType {
        int value;
        explicit MyType(const Node& n, int mult = 1)
            : value{(n["value"].maybe<int>().value_or(93) + 7) * mult} {}
    };
    SECTION("Using custom conversion counts as being used") {
        auto t = r["t"].to<MyType>(3);
        REQUIRE(t.value == 54);  // (11 + 7) * 3
        REQUIRE(n.unused() == onlyUsed({"/t/value", "/t"}));
    }
    SECTION("Maybes also work") {
        auto t = r["t"].maybe<MyType>(3);
        REQUIRE(t->value == 54);  // (11 + 7) * 3
        REQUIRE(n.unused() == onlyUsed({"/t/value", "/t"}));
    }

    SECTION("Maybes also work pt2") {
        auto t = r["t"].maybe<MyType>();
        REQUIRE(t->value == 18);  // (11 + 7) * (mult=1)
        REQUIRE(n.unused() == onlyUsed({"/t/value", "/t"}));
    }
    SECTION("Maybes that fail to use the value don't use the value") {
        // Use the 'n' structure (n:{ested:[v,alue]}) which doesn't have
        // the "value" key that MyStruct wants to see.
        auto t = r["n"].maybe<MyType>(5);
        REQUIRE(t->value == 500);  // (93 + 7) * (mult=5)
        REQUIRE(n.unused() == noneUsed);
    }
}

TEST_CASE("Nested sequence like map") {
    NodeSource nodeSource("a: []", "");
    const auto& yaml = nodeSource.root();
    REQUIRE(bool(yaml["a"]["wtf"]["even_deeper"]) == false);
}

TEST_CASE("Out of Range") {
    SECTION("Out of list bounds") {
        NodeSource ns{"[100]", ""};
        const auto& node = ns.root();
        REQUIRE(node);
        REQUIRE(node.isSequence());
        REQUIRE(node[0]);
        REQUIRE(bool(node[1]) == false);
        REQUIRE(bool(node[-1]) == false);
    }
}


template <>
struct NodeConvert<HasConversionSpecialization> {
    using type = HasConversionSpecialization;

    static type convert(const Node& node, int delta) {
        return {node["x"].to<int>() + delta};
    }
};

TEST_CASE("Static Failures") {
    NodeSource ns{"{}", ""};
    const Node& node = ns.root();

    STATIC_REQUIRE(std::is_same_v<decltype(node.to<int>()), int>);
    STATIC_REQUIRE(std::is_same_v<decltype(node.to<HasConversionSpecialization>()),
                                  HasConversionSpecialization>);
}

TEST_CASE("YAML::Node Equivalency") {

    //
    // The assertions on YAML::Node aren't strictly necessary, but
    // having the API shown here justifies the behavior of genny::Node.
    //

    SECTION("Boolean Conversions") {
        {
            YAML::Node yaml = YAML::Load("foo: false");
            REQUIRE(yaml);
            REQUIRE(yaml["foo"]);
            REQUIRE(yaml["foo"].IsScalar());
            REQUIRE(yaml["foo"].as<bool>() == false);
        }
    }

    SECTION("Invalid Access") {
        {
            YAML::Node yaml = YAML::Load("foo: a");
            // test of the test
            REQUIRE(yaml["foo"].as<std::string>() == "a");
            // YAML::Node doesn't barf when treating a map like a sequence
            REQUIRE(bool(yaml[0]) == false);
            // ...but it does barf when treating a scalar like a sequence
            REQUIRE_THROWS_WITH([&]() { yaml["foo"][0]; }(),
                                Catch::Matches("operator\\[\\] call on a scalar"));
        }

        {
            NodeSource nodeSource{"foo: a", ""};
            const Node& node{nodeSource.root()};
            // test of the test
            REQUIRE(node["foo"].to<std::string>() == "a");
            // don't barf when treating a map like a sequence
            REQUIRE(bool(node[0]) == false);
            // ...or when treating a scalar like a sequence (this is arguably incorrect)
            REQUIRE(bool(node["foo"][0]) == false);
        }

        {
            YAML::Node yaml = YAML::Load("foos: [{a: 1}]");
            // YAML::Node doesn't barf when treating a map like a sequence
            REQUIRE(bool(yaml["foos"]["a"]) == false);
            // ...but it does barf when treating a scalar like a sequence
            REQUIRE_THROWS_WITH([&]() { yaml["foos"]["a"].as<int>(); }(),
                                Catch::Matches("bad conversion"));
        }
        {
            NodeSource nodeSource("foos: [{a: 1}]", "");
            auto& yaml{nodeSource.root()};
            REQUIRE(bool(yaml["foos"]["a"]) == false);
            REQUIRE_THROWS_WITH([&]() { yaml["foos"]["a"].to<int>(); }(),
                                Catch::Matches("Invalid key 'a': Tried to access node that doesn't "
                                               "exist. On node with path '/foos/a': "));
            // this is arguably "incorrect" but it's at least consistent with YAML::Node's behavior
            REQUIRE(yaml["foos"]["a"].maybe<int>().value_or(7) == 7);
        }
    }

    SECTION("iteration over sequences") {
        {
            YAML::Node yaml = YAML::Load("ns: [1,2,3]");
            int sum = 0;
            for (auto n : yaml["ns"]) {
                REQUIRE(n.first.IsDefined() == false);
                REQUIRE(n.second.IsDefined() == false);
                sum += n.as<int>();
            }
            REQUIRE(sum == 6);
        }

        {
            NodeSource ns{"ns: [1,2,3]", ""};
            auto& node = ns.root();
            int sum = 0;
            for (auto& [k, v] : node["ns"]) {
                REQUIRE(bool(v) == true);
                sum += v.to<int>();
                if (sum == 1) {
                    REQUIRE(v.path() == "/ns/0");
                }
            }
            REQUIRE(sum == 6);
        }
    }

    SECTION("iteration over maps") {
        {
            YAML::Node yaml = YAML::Load("foo: bar");
            int seen = 0;
            for (auto kvp : yaml) {
                ++seen;
                REQUIRE(kvp.first.as<std::string>() == "foo");
                REQUIRE(kvp.second.as<std::string>() == "bar");
            }
            REQUIRE(seen == 1);
        }
        {
            NodeSource ns("foo: bar", "");
            auto& node = ns.root();
            int seen = 0;
            for (auto& [k, v] : node) {
                ++seen;
                REQUIRE(k.toString() == "foo");
                REQUIRE(v.to<std::string>() == "bar");
                REQUIRE(v.path() == "/foo");
            }
            REQUIRE(seen == 1);
        }
    }

    SECTION("Default-constructed is valid") {
        auto defaultConstructed = YAML::Node{};
        REQUIRE(bool(defaultConstructed) == true);
        REQUIRE(defaultConstructed.Type() == YAML::NodeType::Null);
    }

    SECTION("we're equivalent to YAML::Node's handling of null and missing values") {
        {
            YAML::Node yaml = YAML::Load("foo: null");

            REQUIRE(yaml["foo"].IsDefined() == true);
            REQUIRE(yaml["foo"].IsNull() == true);
            REQUIRE(bool(yaml["foo"]) == true);

            REQUIRE(yaml["bar"].IsDefined() == false);
            REQUIRE(yaml["bar"].IsNull() == false);
            REQUIRE(bool(yaml["bar"]) == false);
        }

        {
            NodeSource nodeSource{"foo: null", ""};
            auto& node{nodeSource.root()};
            REQUIRE(node["foo"].isNull() == true);
            REQUIRE(bool(node["foo"]) == true);

            REQUIRE(node["bar"].isNull() == false);
            REQUIRE(bool(node["bar"]) == false);
        }
    }

    SECTION("Can convert to map<str,str>") {
        {
            YAML::Node yaml = YAML::Load("{a: A, b: B}");
            REQUIRE(yaml.as<std::map<std::string, std::string>>() ==
                    std::map<std::string, std::string>{{"a", "A"}, {"b", "B"}});
        }
        {
            NodeSource ns("{a: A, b: B}", "");
            auto& yaml = ns.root();
            REQUIRE(yaml.to<std::map<std::string, std::string>>() ==
                    std::map<std::string, std::string>{{"a", "A"}, {"b", "B"}});
        }
    }

    SECTION("isNull and fallback") {
        {
            YAML::Node yaml = YAML::Load("a: null");
            REQUIRE(yaml["a"].IsNull());
            REQUIRE(yaml["a"].as<int>(7) == 7);
        }
        {
            NodeSource ns("a: null", "");
            const Node& yaml = ns.root();

            REQUIRE(yaml["a"].isNull());
            // .maybe and .to provide stronger guarantees:
            // we throw rather than returning the fallback if the conversion fails
            REQUIRE_THROWS_WITH(
                [&]() { yaml["a"].maybe<int>(); }(),
                Catch::Matches("Couldn't convert to 'int': 'bad conversion' at "
                               "\\(Line:Column\\)=\\(0:3\\). On node with path '/a': ~"));
            REQUIRE_THROWS_WITH(
                [&]() { yaml["a"].to<int>(); }(),
                Catch::Matches("Couldn't convert to 'int': 'bad conversion' at "
                               "\\(Line:Column\\)=\\(0:3\\). On node with path '/a': ~"));
        }
    }

    SECTION("Missing values are boolean false") {
        {
            YAML::Node yaml = YAML::Load("{}");
            REQUIRE(bool(yaml) == true);
            auto dne = yaml["doesntexist"];
            REQUIRE(bool(dne) == false);
            REQUIRE((!dne) == true);
            if (dne) {
                FAIL("doesn't exist is boolean false");
            }
        }
        {
            NodeSource nodeSource("{}", "");
            auto& node{nodeSource.root()};
            REQUIRE(bool(node) == true);
            auto& dne = node["doesntexist"];
            REQUIRE(bool(dne) == false);
            REQUIRE((!dne) == true);
            if (dne) {
                FAIL("doesn't exist is boolean false");
            }
            REQUIRE(dne.maybe<int>() == std::nullopt);
            REQUIRE(dne.maybe<int>().value_or(9) == 9);
        }
    }

    SECTION("Accessing a Sequence like a Map") {
        {
            YAML::Node yaml = YAML::Load("a: [0,1]");
            REQUIRE(yaml["a"][0].as<int>() == 0);
            REQUIRE(bool(yaml["a"]) == true);
            REQUIRE(bool(yaml["a"][0]) == true);
            // out of range
            REQUIRE(bool(yaml["a"][2]) == false);
            REQUIRE(bool(yaml["a"][-1]) == false);

            REQUIRE(bool(yaml["a"]["wtf"]) == false);
            REQUIRE(bool(yaml["a"]["wtf"]["even_deeper"]) == false);
            REQUIRE_THROWS_WITH([&]() { yaml["a"]["wtf"]["even_deeper"].as<int>(); }(),
                                Catch::Matches("bad conversion"));
        }
        {
            NodeSource nodeSource("a: [0,1]", "");
            auto& yaml = nodeSource.root();
            REQUIRE(yaml["a"][0].to<int>() == 0);
            REQUIRE(bool(yaml["a"]) == true);
            REQUIRE(bool(yaml["a"][0]) == true);
            // out of range
            REQUIRE(bool(yaml["a"][2]) == false);
            REQUIRE(bool(yaml["a"][-1]) == false);

            REQUIRE(bool(yaml["a"]["wtf"]) == false);
            REQUIRE(bool(yaml["a"]["wtf"]["even_deeper"]) == false);
            REQUIRE_THROWS_WITH(
                [&]() {
                    yaml["a"]["wtf"]["even_deeper"].to<int>();
                    // We could do a better job at reporting that 'a' is a sequence
                }(),
                Catch::Matches("Invalid key 'even_deeper': Tried to access node that doesn't "
                               "exist. On node with path '/a/wtf/even_deeper': "));
        }
    }
}

TEST_CASE("Order is preserved") {
    NodeSource ns(R"(
insert: testCollection
documents: [{rating: 10}]
)",
                  "");
    auto& node = ns.root();

    int index = 0;
    for (auto&& [k, _] : node) {
        if (index == 0) {
            REQUIRE(k.toString() == "insert");
        } else {
            REQUIRE(k.toString() == "documents");
        }
        ++index;
    }
    REQUIRE(index == 2);
}

TEST_CASE("NodeKey") {
    SECTION("Comparison") {
        v1::NodeKey a{"a"};
        v1::NodeKey m1{-1};
        REQUIRE(m1 < a);
        REQUIRE(!(m1 < m1));
        REQUIRE(!(a < a));
    }
    SECTION("As Map Key") {
        std::map<v1::NodeKey, long> actual{
            {v1::NodeKey{1}, 7}, {v1::NodeKey{2}, 17}, {v1::NodeKey{-1}, 100}};
        REQUIRE(actual.find(v1::NodeKey{1}) != actual.end());
        REQUIRE(actual.find(v1::NodeKey{1})->second == 7);
        REQUIRE(actual.find(v1::NodeKey{-1})->second == 100);
    }
    SECTION("When missing") {
        std::map<v1::NodeKey, long> actual{
            {v1::NodeKey{1}, 7},
        };
        REQUIRE(actual.find(v1::NodeKey{-1}) == actual.end());
    }
}

TEST_CASE("List out of bounds") {
    NodeSource nodeSource("a: [0,1]", "");
    auto& node = nodeSource.root();
    auto& a = node["a"];
    REQUIRE(bool(a) == true);
    auto& two = a[2];
    REQUIRE(bool(two) == false);
    auto& minusOne = a[-1];
    REQUIRE(bool(minusOne) == false);
}

TEST_CASE("value_or") {
    NodeSource ns{"{}", ""};
    auto& node = ns.root();
    REQUIRE(node["foo"].maybe<int>() == std::nullopt);
}

TEST_CASE("invalid access") {
    auto yaml = std::string(R"(
seven: 7
bee: b
intList: [1,2,3]
stringMap: {a: A, b: B}
nothing: null
sure: true
nope: false
)");
    NodeSource ns{yaml, ""};
    auto& node = ns.root();

    //    REQUIRE_THROWS_WITH([&](){}(), Catch::Matches(""));
    REQUIRE_THROWS_WITH(
        [&]() { node[0].to<int>(); }(),
        Catch::Matches(
            "Invalid key '0': Tried to access node that doesn't exist. On node with path '/0': "));
    REQUIRE_THROWS_WITH([&]() { node["seven"][0].to<int>(); }(),
                        Catch::Matches("Invalid key '0': Tried to access node that doesn't exist. "
                                       "On node with path '/seven/0': "));

    REQUIRE_THROWS_WITH([&]() { node["bee"].to<int>(); }(),
                        Catch::Matches("Couldn't convert to 'int': 'bad conversion' at "
                                       "\\(Line:Column\\)=\\(2:5\\). On node with path '/bee': b"));
}

TEST_CASE("Invalid YAML") {
    REQUIRE_THROWS_WITH(
        [&]() { NodeSource n("foo: {", "foo.yaml"); }(),
        Catch::Matches("Invalid YAML: 'end of map flow not found' at \\(Line:Column\\)=\\(0:0\\). "
                       "On node with path 'foo.yaml'."));
}


TEST_CASE("No Inheritance") {
    NodeSource ns{R"(
Coll: Test
Phases:
- Doc: foo
)",
                  ""};
    auto& node = ns.root();
    {
        auto& phases = node["Phases"];
        auto& zero = phases[0];
        auto& coll = zero["Coll"];
        REQUIRE(bool(coll) == false);
    }
    REQUIRE(bool(node["Phases"][0]["Coll"]) == false);
}

TEST_CASE("no nested inheritance") {
    NodeSource ns{"children: {seven: 7}", ""};
    auto& node = ns.root();

    auto& children = node["children"];
    auto& childrenfoo = children["foo"];
    auto& childrenfooseven = childrenfoo["seven"];
    auto childrenfoosevenmaybe = childrenfooseven.maybe<int>();
    REQUIRE(childrenfoosevenmaybe.value_or(8) == 8);
    REQUIRE(bool(childrenfooseven) == false);
}

TEST_CASE("more lack of inheritance") {
    {
        NodeSource ns{"seven: 7", ""};
        auto& node = ns.root();

        {
            auto& foo = node["foo"];
            auto& seven = foo["seven"];
            auto maybeSeven = seven.maybe<int>();
            REQUIRE(maybeSeven.value_or(8) == 8);
            REQUIRE(bool(seven) == false);
        }

        REQUIRE(node["foo"]["bar"][0]["seven"].maybe<int>().value_or(8) == 8);
        REQUIRE(node["seven"].to<int>() == 7);
        REQUIRE(bool(node["foo"]["bar"][0]["seven"]) == false);
    }

    NodeSource ns{R"(
Coll: Test
Phases:
- Doc: foo
- Coll: Bar
- Another:
  - Nested: {Coll: Baz}
)",
                  ""};
    auto& node = ns.root();

    REQUIRE(node["Coll"].to<std::string>() == "Test");
    REQUIRE(node["Coll"].maybe<std::string>().value_or("Or") == "Test");

    // Arguably this should throw? we're treating a sequence like a map
    REQUIRE(bool(node["Phases"]["Coll"]) == false);
    REQUIRE(node["Phases"]["Coll"].maybe<std::string>().value_or("Or") == "Or");

    REQUIRE(bool(node["Phases"][0]["Coll"]) == false);
    REQUIRE(node["Phases"][0]["Coll"].maybe<std::string>().value_or("Or") == "Or");

    REQUIRE(node["Phases"][1]["Coll"].to<std::string>() == "Bar");
    REQUIRE(node["Phases"][1]["Coll"].maybe<std::string>().value_or("Or") == "Bar");

    REQUIRE(node["Phases"][2]["Coll"].maybe<std::string>().value_or("Or") == "Or");

    REQUIRE(node["Phases"][2]["Another"]["Coll"].maybe<std::string>().value_or("Or") == "Or");

    REQUIRE(node["Phases"][2]["Another"][0]["Nested"]["Coll"].maybe<std::string>().value_or("Or") ==
            "Baz");
}


TEST_CASE(".maybe and value_or") {
    auto yaml = std::string(R"(
seven: 7
bee: b
intList: [1,2,3]
stringMap: {a: A, b: B}
nothing: null
sure: true
nope: false
)");
    NodeSource ns{yaml, ""};
    const auto& node = ns.root();

    REQUIRE(node["seven"].maybe<int>().value_or(8) == 7);
    REQUIRE(bool(node["eight"]) == false);
    REQUIRE(node["eight"].maybe<int>().value_or(8) == 8);
    REQUIRE(node["intList"].maybe<std::vector<int>>().value_or(std::vector<int>{}) ==
            std::vector<int>{1, 2, 3});
    REQUIRE(node["intList2"].maybe<std::vector<int>>().value_or(std::vector<int>{1, 2}) ==
            std::vector<int>{1, 2});
    REQUIRE(node["stringMap"].maybe<std::map<std::string, std::string>>().value_or(
                std::map<std::string, std::string>{}) ==
            std::map<std::string, std::string>{{"a", "A"}, {"b", "B"}});
    REQUIRE(node["stringMap"][0].maybe<int>().value_or(7) == 7);

    REQUIRE(node["sure"].maybe<bool>().value_or(false) == true);
    REQUIRE(node["sure"].maybe<bool>().value_or(true) == true);
    REQUIRE(node["nope"].maybe<bool>().value_or(false) == false);
    REQUIRE(node["nope"].maybe<bool>().value_or(true) == false);
    REQUIRE(node["doesntExist"].maybe<bool>().value_or(true) == true);
    REQUIRE(node["doesntExist"].maybe<bool>().value_or(false) == false);

    REQUIRE(node["bee"].maybe<std::string>().value_or<std::string>("foo") == "b");
    REQUIRE(node["baz"].maybe<std::string>().value_or<std::string>("foo") == "foo");


    REQUIRE(node["stringMap"]["a"].maybe<std::string>().value_or<std::string>("7") == "A");
}

TEST_CASE("Node Type") {
    auto yaml = std::string(R"(
seven: 7
bee: b
mixedList: [1,2,"a", [inner]]
mixedMap: {seven: 7, bees: [b]}
nothing: null
sure: true
nope: false
)");
    NodeSource nodeSource{yaml, ""};
    const Node& node{nodeSource.root()};
    REQUIRE(node["nonexistant"].type() == Node::Type::Undefined);

    REQUIRE(node.type() == Node::Type::Map);
    REQUIRE(node.isMap());

    REQUIRE(node["seven"].isScalar());
    REQUIRE(node["seven"].type() == Node::Type::Scalar);

    REQUIRE(node["bee"].isScalar());
    REQUIRE(node["bee"].type() == Node::Type::Scalar);

    REQUIRE(node["mixedList"].isSequence());
    REQUIRE(node["mixedList"].type() == Node::Type::Sequence);

    REQUIRE(node["mixedList"][0].isScalar());
    REQUIRE(node["mixedList"][0].type() == Node::Type::Scalar);

    REQUIRE(node["mixedList"][3].isSequence());
    REQUIRE(node["mixedList"][3].type() == Node::Type::Sequence);

    REQUIRE(node["mixedMap"].isMap());
    REQUIRE(node["mixedMap"].type() == Node::Type::Map);

    REQUIRE(node["mixedMap"]["seven"].isScalar());
    REQUIRE(node["mixedMap"]["seven"].type() == Node::Type::Scalar);

    REQUIRE(node["mixedMap"]["bees"].isSequence());
    REQUIRE(node["mixedMap"]["bees"].type() == Node::Type::Sequence);

    REQUIRE(node["nothing"].isNull());
    REQUIRE(node["nothing"].type() == Node::Type::Null);

    REQUIRE(node["sure"].isScalar());
    REQUIRE(node["sure"]);
    REQUIRE(!!node["sure"]);
    REQUIRE(node["sure"].to<bool>());

    auto sure = node["sure"].maybe<bool>();
    REQUIRE(sure);
    REQUIRE(*sure);
    REQUIRE(node["sure"].to<bool>());

    REQUIRE(node["nope"].isScalar());
    auto foo = node["nope"].maybe<bool>();
    REQUIRE(foo);
    REQUIRE(*foo == false);
    REQUIRE(node["nope"].to<bool>() == false);
}

// Mickey-mouse versions of structs from context.hpp
struct WLCtx;
struct ACtx;
struct PCtx;
struct Actr;

struct WLCtx {
    const Node& node;
    std::vector<std::unique_ptr<ACtx>> actxs;
    std::vector<std::unique_ptr<Actr>> actrs;

    explicit WLCtx(const Node& node) : node{node} {
        // Make a bunch of actor contexts
        for (auto& [k, actor] : node["Actors"]) {
            actxs.emplace_back(std::make_unique<ACtx>(actor, *this));
        }
        for (auto& actx : actxs) {
            // don't go thru a "ActorProducer" it shouldn't matter
            // cuz it just passes in the ActorContext& to the ctor
            actrs.push_back(std::make_unique<Actr>(*actx));
        }
    }
};

struct ACtx {
    const Node& node;
    std::unique_ptr<WLCtx> wlc;
    std::vector<std::unique_ptr<PCtx>> pcs;

    ACtx(const Node& node, WLCtx& wlctx) : node{node} {
        pcs = constructPhaseContexts(node, this);
    }

    static std::vector<std::unique_ptr<PCtx>> constructPhaseContexts(const Node& node, ACtx* actx) {
        std::vector<std::unique_ptr<PCtx>> out;
        auto& phases = (*actx).node["Phases"];
        for (const auto& [k, phase] : phases) {
            out.emplace_back(std::make_unique<PCtx>(phase, *actx));
        }
        return out;
    }
};

struct PCtx {
    const Node& node;
    ACtx* actx;

    bool isNop() {
        return node["Nop"].maybe<bool>().value_or(false);
    }

    PCtx(const Node& node, ACtx& actx) : node{node}, actx{std::addressof(actx)} {}
};

struct Actr {
    explicit Actr(ACtx& ctx) {
        REQUIRE(ctx.node["Nop"].maybe<bool>().value_or(false) == false);
    }
};

TEST_CASE("Mickey-mouse Use From context.hpp") {
    NodeSource yaml(R"(
    SchemaVersion: 2018-07-01
    Database: test
    Actors:
    - Name: MetricsNameTest
      Type: HelloWorld
      Threads: 1
      Phases:
      - Repeat: 1
    )",
                    "");
    WLCtx ctx{yaml.root()};
}


TEST_CASE("use values from iteration") {

    NodeSource ns{R"(
Actors:
- Name: Foo
  Phases:
  - Repeat: 1
)",
                  ""};
    auto& node = ns.root();

    std::optional<const Node*> phase0;
    {
        unsigned seen = 0;
        for (auto&& [k, actor] : node["Actors"]) {
            for (auto&& [p, phase] : actor["Phases"]) {
                phase0 = &phase;
                ++seen;
            }
        }
        REQUIRE(seen == 1);
    }

    const Node& phase0Node = (*(*phase0));
    REQUIRE(phase0Node["Repeat"].to<int>() == 1);
    REQUIRE(phase0Node["Repeat"].path() == "/Actors/0/Phases/0/Repeat");
    REQUIRE(phase0Node["Repeat"][".."].path() == "/Actors/0/Phases/0/Repeat/..");
    REQUIRE(bool(phase0Node["Name"]) == false);
    REQUIRE(phase0Node["Nop"].maybe<bool>().value_or(false) == false);
}

TEST_CASE("size") {
    {
        NodeSource ns{"foo: bar", ""};
        auto& node = ns.root();
        REQUIRE(node.size() == 1);
        // scalars have size 0
        REQUIRE(node["foo"].size() == 0);
    }
    {
        NodeSource ns{"{}", ""};
        auto& node = ns.root();
        REQUIRE(node.size() == 0);
    }
    {
        NodeSource ns{"a: null", ""};
        auto& node = ns.root();
        REQUIRE(node["a"].size() == 0);
    }
    {
        NodeSource ns{"[1,2,3]", ""};
        auto& node = ns.root();
        REQUIRE(node.size() == 3);
    }
    {
        NodeSource ns{"a: {b: {c: []}}", ""};
        auto& node = ns.root();
        REQUIRE(node.size() == 1);
        REQUIRE(node["a"].size() == 1);
        REQUIRE(node["a"]["b"].size() == 1);
        REQUIRE(node["a"]["b"]["c"].size() == 0);
    }
    {
        NodeSource ns{"", ""};
        auto& node = ns.root();
        REQUIRE(node.size() == 0);
    }
    {
        NodeSource ns{
            "foos: [1,2,3]\n"
            "children: {a: 7}",
            ""};
        auto& node = ns.root();
        REQUIRE(node.size() == 2);
        REQUIRE(node["foos"].size() == 3);
        REQUIRE(node["children"].size() == 1);
        // scalars have size 0
        REQUIRE(node["children"]["a"].size() == 0);
    }
}

TEST_CASE("Parent .. traversal isn't a thing") {
    NodeSource ns{"a: {b: { c: {d: D, e: E} } }", ""};
    auto& node = ns.root();
    REQUIRE(node["a"]["b"]["c"]["d"].to<std::string>() == "D");
    REQUIRE(node["a"]["b"]["c"]["e"].to<std::string>() == "E");
    REQUIRE(bool(node["a"]["b"]["c"]["d"][".."]["e"]) == false);
    REQUIRE(bool(node["a"]["b"]["c"]["d"][".."]["e"][".."]["d"]) == false);
}

TEST_CASE("Node inheritance") {
    auto yaml = std::string(R"(
a: 7
b: 900
Children:
  a: 100
  One: {}
  Two: {a: 9}
  Three: {b: 70}
  Four:
    FourChild:
      a: 11
)");
    NodeSource ns(yaml, "");
    auto& node = ns.root();

    SECTION("Parent traversal") {
        REQUIRE(node["a"].to<int>() == 7);
        REQUIRE(node["Children"]["a"].to<int>() == 100);
        REQUIRE(bool(node["Children"][".."]["a"]) == false);
    }

    SECTION("value_or") {
        {
            auto& c = node["c"];
            REQUIRE(c.maybe<int>().value_or(1) == 1);
            REQUIRE(node["a"].maybe<int>().value_or(100) == 7);
            REQUIRE(node["Children"]["a"].maybe<int>().value_or(42) == 100);
            REQUIRE(node["does"]["not"]["exist"].maybe<int>().value_or(90) == 90);
        }
    }

    SECTION("No inheritance") {
        {
            int a = node["a"].to<int>();
            REQUIRE(a == 7);
        }

        {
            int a = node["Children"]["a"].to<int>();
            REQUIRE(a == 100);
        }

        {
            int b = node["Children"]["Three"]["b"].to<int>();
            REQUIRE(b == 70);
        }
    }
}

TEST_CASE("Node Built-Ins Construction") {
    auto yaml = std::string(R"(
SomeString: some_string
IntList: [1,2,3]
ListOfMapStringString:
- {a: A}
- {b: B}
)");
    NodeSource ns(yaml, "");
    const auto& node = ns.root();

    { REQUIRE(node["SomeString"].to<std::string>() == "some_string"); }
    { REQUIRE((node["IntList"].to<std::vector<int>>() == std::vector<int>{1, 2, 3})); }
    {
        using ListMapStrStr = std::vector<std::map<std::string, std::string>>;
        auto expect = ListMapStrStr{{{"a", "A"}}, {{"b", "B"}}};
        auto actual = node["ListOfMapStringString"].to<ListMapStrStr>();

        REQUIRE(expect == actual);
    }
}

TEST_CASE("Specialization") {
    NodeSource ns("{x: 8}", "");
    const auto& node = ns.root();
    REQUIRE(node.to<HasConversionSpecialization>(3).x == 11);
}

TEST_CASE("Basic Sequence Node Iteration") {
    NodeSource ns("foo: [1]", "");
    auto& node = ns.root();
    REQUIRE(node.size() == 1);
    auto& foo = node["foo"];
    REQUIRE(foo.size() == 1);
    REQUIRE(foo.begin() != foo.end());
    REQUIRE(foo.begin() == foo.begin());
    REQUIRE(foo.end() == foo.end());

    {
        auto it = foo.begin();
        REQUIRE(it != foo.end());
        auto& [k, v] = *it;
        REQUIRE(k.toString() == "0");
        REQUIRE(v.to<long>() == 1);
        REQUIRE(it != foo.end());
        ++it;
        REQUIRE(it == foo.end());
    }
}


TEST_CASE("Simple Path 1") {
    NodeSource ns{"", "f.yml"};
    auto& node = ns.root();
    REQUIRE(node.path() == "f.yml");
    REQUIRE(node[0].path() == "f.yml/0");
}

TEST_CASE("Simple Path 2") {
    NodeSource ns{"", ""};
    auto& node = ns.root();
    REQUIRE(node.path() == "");
    REQUIRE(node["a"]["b"].path() == "/a/b");
}


TEST_CASE("Node Paths") {
    auto yaml = std::string(R"(
msg: bar
One: {msg: foo}
Two: {}
)");
    NodeSource ns(yaml, "");
    auto& node = ns.root();
    REQUIRE(node["One"][".."].path() == "/One/..");
    REQUIRE(node.path() == "");
    REQUIRE(node[0].path() == "/0");
    REQUIRE(node["msg"].path() == "/msg");
    REQUIRE(node["msg"][".."].path() == "/msg/..");
    REQUIRE(node["msg"][".."][".."][".."][".."].path() == "/msg/../../../..");
    REQUIRE(node["One"]["msg"].path() == "/One/msg");
    REQUIRE(node["One"]["msg"][".."].path() == "/One/msg/..");
    REQUIRE(node["One"]["msg"][".."]["msg"][".."]["msg"].path() == "/One/msg/../msg/../msg");
    REQUIRE(node["One"]["foo"][0][1]["bar"].path() == "/One/foo/0/1/bar");
    REQUIRE(node["One"]["foo"][0][1]["bar"][".."].path() == "/One/foo/0/1/bar/..");

    REQUIRE_THROWS_WITH(
        node["One"]["foo"].to<std::string>(),
        Catch::Matches(
            R"(Invalid key 'foo': Tried to access node that doesn't exist. On node with path '/One/foo': )"));
}

TEST_CASE("Node iteration path") {
    auto yaml = std::string(R"(
one: [1]
two: [1,2]
mapOneDeep: {a: A}
mapTwoDeep: {a: {A: aA}}
)");
    NodeSource ns(yaml, "");
    auto& node = ns.root();
    {
        int seen = 0;
        for (auto&& [k, v] : node["one"]) {
            REQUIRE(v.path() == "/one/0");
            ++seen;
        }
        REQUIRE(seen == 1);
    }
    {
        int seen = 0;
        for (auto& [k, v] : node["two"]) {
            REQUIRE(v.path() == "/two/" + std::to_string(seen));
            ++seen;
        }
        REQUIRE(seen == 2);
    }
    {
        int seen = 0;
        for (auto&& [k, v] : node["mapOneDeep"]) {
            REQUIRE(v.path() == "/mapOneDeep/a");
            ++seen;
        }
        REQUIRE(seen == 1);
    }
}

TEST_CASE("Node Simple User-Defined Conversions") {
    EmptyStruct context;

    auto yaml = std::string(R"(
msg: bar
One: {msg: foo}
Two: {}
)");
    NodeSource ns(yaml, "");
    const auto& node = ns.root();

    {
        TakesEmptyStructAndExtractsMsg one =
            node["One"].to<TakesEmptyStructAndExtractsMsg>(&context);
        REQUIRE(one.msg == "foo");
    }
}

TEST_CASE("operator-left-shift") {
    {
        NodeSource ns{"Foo: 7", ""};
        const auto& node = ns.root();
        std::stringstream str;
        str << node;
        REQUIRE(str.str() == "Foo: 7");
    }
    {
        NodeSource ns{"Foo: {Bar: Baz}", ""};
        const auto& node = ns.root();
        std::stringstream str;
        str << node;
        REQUIRE(str.str() == "Foo: {Bar: Baz}");
    }
    // rely on YAML::Dump so don't need to enforce much beyond this
}

TEST_CASE("Node getPlural") {
    {
        NodeSource ns{"Foo: 7", ""};
        auto& node = ns.root();

        REQUIRE(node.getPlural<int>("Foo", "Foos") == std::vector<int>{7});
    }
    {
        NodeSource ns{"Foos: [1,2,3]", ""};
        auto& node = ns.root();

        REQUIRE(node.getPlural<int>("Foo", "Foos") == std::vector<int>{1, 2, 3});
    }
    {
        NodeSource ns{"Foo: 712", ""};
        auto& node = ns.root();

        int calls = 0;
        REQUIRE(node.getPlural<HasConversionSpecialization>(
                        "Foo",
                        "Foos",
                        [&](const Node& node) -> HasConversionSpecialization {
                            ++calls;
                            // add one to the node value
                            return HasConversionSpecialization{node.to<int>() + 1};
                        })[0]
                    .x == 713);
        REQUIRE(calls == 1);
    }

    {
        NodeSource ns{"Foos: [1,2,3]", ""};
        auto& node = ns.root();

        int calls = 0;
        REQUIRE(node.getPlural<HasConversionSpecialization>("Foo",
                                                            "Foos",
                                                            [&](const Node& node) {
                                                                ++calls;
                                                                // subtract 1 from the node value
                                                                return HasConversionSpecialization{
                                                                    node.to<int>() - 1};
                                                            })[2]
                    .x == 2);
        REQUIRE(calls == 3);
    }

    {
        NodeSource ns{"{}", ""};
        auto& node = ns.root();

        REQUIRE_THROWS_WITH(
            [&]() { node.getPlural<int>("Foo", "Foos"); }(),
            Catch::Matches(
                R"(Invalid key 'getPlural\('Foo', 'Foos'\)': Either 'Foo' or 'Foos' required. On node with path '': \{\})"));
    }

    {
        NodeSource ns{"{Foos: 7}", ""};
        auto& node = ns.root();

        REQUIRE_THROWS_WITH(
            [&]() { node.getPlural<int>("Foo", "Foos"); }(),
            Catch::Matches(
                R"(Invalid key 'getPlural\('Foo', 'Foos'\)': Plural 'Foos' must be a sequence type. On node with path '': \{Foos: 7\})"));
    }

    {
        NodeSource ns{"{Foo: 8, Foos: [1,2]}", ""};
        auto& node = ns.root();

        REQUIRE_THROWS_WITH(
            [&]() { node.getPlural<int>("Foo", "Foos"); }(),
            Catch::Matches(
                R"(Invalid key 'getPlural\('Foo', 'Foos'\)': Can't have both 'Foo' and 'Foos'. On node with path '': \{Foo: 8, Foos: \[1, 2\]\})"));
    }
}

TEST_CASE("maybe") {
    auto yaml = std::string(R"(
Children:
  msg: inherited
  overrides: {msg: overridden}
  deep:
    nesting:
      can:
        still: {inherit: {}, override: {msg: deeply_overridden}}
)");
    NodeSource ns(yaml, "");
    const auto& node = ns.root();

    node["does"]["not"]["exist"].maybe<RequiresParamToEqualNodeX>(3);
    REQUIRE(!node["does"]["not"]["exist"].maybe<ExtractsMsg>());
    REQUIRE(node["Children"].maybe<ExtractsMsg>()->msg == "inherited");
    REQUIRE(node["Children"]["overrides"].maybe<ExtractsMsg>()->msg == "overridden");
    REQUIRE(bool(node["Children"]["deep"]["nesting"]["can"]["still"]["inherit"]) == true);
    REQUIRE(
        node["Children"]["deep"]["nesting"]["can"]["still"]["override"].maybe<ExtractsMsg>()->msg ==
        "deeply_overridden");
}

TEST_CASE("Configurable additional-ctor-params Conversions") {
    auto yaml = std::string(R"(
x: 9
a: {x: 7}
b: {}
)");
    NodeSource ns(yaml, "");
    const auto& node = ns.root();

    node.to<RequiresParamToEqualNodeX>(9);
    node["a"].to<RequiresParamToEqualNodeX>(7);
}

TEST_CASE("Iteration") {
    auto yaml = std::string(R"(
Scalar: foo
SimpleMap: {a: b}
ListOfScalars: [1,2]
ListOfMap:
- {a: A, b: B}
SingleItemList: [37]
)");
    NodeSource ns(yaml, "");
    auto& node = ns.root();

    SECTION("Scalar") {
        auto& a = node["Scalar"];
        REQUIRE(a);
        for (auto& kvp : a) {
            FAIL("nothing to iterate");
        }
    }
    SECTION("SimpleMap") {
        auto& mp = node["SimpleMap"];
        REQUIRE(mp);
        int seen = 0;
        for (auto& [k, v] : mp) {
            REQUIRE(k.toString() == "a");
            REQUIRE(v.to<std::string>() == "b");
            ++seen;
        }
        REQUIRE(seen == 1);
    }

    SECTION("ListOfScalars") {
        auto& lst = node["ListOfScalars"];
        REQUIRE(lst);
        int i = 1;
        for (auto& [k, v] : lst) {
            REQUIRE(v.to<int>() == i);
            ++i;
        }
        REQUIRE(i == 3);
    }

    SECTION("ListOfMap") {
        auto& lom = node["ListOfMap"];
        REQUIRE(lom);
        REQUIRE(lom.size() == 1);
        auto countMaps = 0;
        for (auto& [k, m] : lom) {
            ++countMaps;
            REQUIRE(m.size() == 2);

            auto countEntries = 0;
            for (auto& _ : m) {
                ++countEntries;
            }
            REQUIRE(countEntries == 2);

            REQUIRE(m["a"].to<std::string>() == "A");
            REQUIRE(m["b"].to<std::string>() == "B");
        }
        REQUIRE(countMaps == 1);
    }

    SECTION("SingleItemList") {
        auto& sil = node["SingleItemList"];
        REQUIRE(sil.size() == 1);
        REQUIRE(sil[0].to<int>() == 37);
        auto count = 0;
        for (auto& [k, v] : sil) {
            REQUIRE(v.to<int>() == 37);
            ++count;
        }
        REQUIRE(count == 1);
    }
}

}  // namespace genny
