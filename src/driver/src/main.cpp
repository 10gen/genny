#include <experimental/optional>

#include <iostream>
#include <string>

#include <gennylib/version.hpp>

int main() {
    // basically just a test that we're using c++17
    auto v { std::experimental::make_optional(genny::get_version()) };
    std::cout << u8"🧞 Genny" << " Version " << v.value_or("ERROR") << u8" 💝🐹🌇⛔" << std::endl;
    return 0;
}
