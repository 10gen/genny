#ifndef HEADER_C9EBE412_325B_4614_BD86_F7026AA772C1_INCLUDED
#define HEADER_C9EBE412_325B_4614_BD86_F7026AA772C1_INCLUDED

#include <functional>
#include <string>
#include <type_traits>
#include <vector>

#include <yaml-cpp/yaml.h>

namespace genny {

class ErrorBag {

public:
    explicit operator bool() const {
        return !this->errors.empty();
    }

    void report(std::ostream& out) const {
        for (const auto& error : errors) {
            out << u8"😱 ";
            out << error;
        }
    }

    template <class N, class K = std::string, class E = std::string>
    void require(const N& node,
                 const K& key,
                 const E& expect,
                 const std::string& path = "") {
        auto val = node.operator[](key);
        if (!val) {
            add("Key " + path + key + " not found");
            return;
        }
        auto asType = val.template as<E>(); // ugh the C++ grammar is unpleasant
        if (expect != asType) {
            add("Key " +  write(path) + write(key) + " expect [" + write(expect) + "] but is [" + write(asType) + "]");
        }
    }

private:
    void add(std::string error) {
        errors.push_back(std::move(error));
    }

    std::vector<std::string> errors;

    template<class T>
    static std::string write(const T& val) {
        std::stringstream out;
        out << val;
        return out.str();
    }
};

}  // namespace genny

#endif  // HEADER_C9EBE412_325B_4614_BD86_F7026AA772C1_INCLUDED
