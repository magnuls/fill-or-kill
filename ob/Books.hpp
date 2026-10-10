#ifndef OB_BOOKS_HPP
#define OB_BOOKS_HPP

#include "ob/MapBook.hpp"
#include "ob/ReferenceBook.hpp"
#include "ob/ReverseVectorBook.hpp"
#include "ob/StubBook.hpp"
#include "ob/VectorBook.hpp"

#include <string>
#include <type_traits>

namespace ob {

constexpr const char* BOOK_NAMES[] = {
    "stub",          "ref",        "map",
    "vector",        "reverse_lb", "reverse_branchless",
    "reverse_linear"};

constexpr const char* VARIANT_NAMES[] = {
    "map", "vector", "reverse_lb", "reverse_branchless",
    "reverse_linear"};

template<class F>
bool DispatchBook(const std::string& name, F&& f) {
    if (name == "stub") {
        f(std::type_identity<StubBook>{});
        return true;
    }
    if (name == "ref") {
        f(std::type_identity<ReferenceBook>{});
        return true;
    }
    if (name == "map") {
        f(std::type_identity<MapBook>{});
        return true;
    }
    if (name == "vector") {
        f(std::type_identity<VectorBook>{});
        return true;
    }
    if (name == "reverse_lb") {
        f(std::type_identity<ReverseLowerBoundBook>{});
        return true;
    }
    if (name == "reverse_branchless") {
        f(std::type_identity<ReverseBranchlessBook>{});
        return true;
    }
    if (name == "reverse_linear") {
        f(std::type_identity<ReverseLinearBook>{});
        return true;
    }
    return false;
}

inline std::string KnownBooks() {
    std::string s;
    for (const char* n : BOOK_NAMES) {
        if (!s.empty())
            s += ", ";
        s += n;
    }
    return s;
}

}

#endif
