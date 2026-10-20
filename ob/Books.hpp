#ifndef OB_BOOKS_HPP
#define OB_BOOKS_HPP

#include "ob/MapBook.hpp"
#include "ob/OrderTable.hpp"
#include "ob/ReferenceBook.hpp"
#include "ob/ReverseVectorBook.hpp"
#include "ob/StubBook.hpp"
#include "ob/TableBook.hpp"
#include "ob/VectorBook.hpp"

#include <string>
#include <type_traits>

namespace ob {

constexpr const char* BOOK_NAMES[] = {"stub",
                                      "ref",
                                      "table",
                                      "map",
                                      "vector",
                                      "reverse_lb",
                                      "reverse_branchless",
                                      "reverse_linear"};

constexpr const char* VARIANT_NAMES[] = {
    "map", "vector", "reverse_lb", "reverse_branchless",
    "reverse_linear"};

template<template<class> class Table, class F>
bool DispatchBookWith(const std::string& name, F&& f) {
    if (name == "stub") {
        f(std::type_identity<StubBook>{});
        return true;
    }
    if (name == "ref") {
        f(std::type_identity<ReferenceBook>{});
        return true;
    }
    if (name == "table") {
        f(std::type_identity<TableOnlyBook<Table>>{});
        return true;
    }
    if (name == "map") {
        f(std::type_identity<MapBookT<Table>>{});
        return true;
    }
    if (name == "vector") {
        f(std::type_identity<VectorBookT<Table>>{});
        return true;
    }
    if (name == "reverse_lb") {
        f(std::type_identity<
            ReverseVectorBook<LowerBoundSearch, Table>>{});
        return true;
    }
    if (name == "reverse_branchless") {
        f(std::type_identity<
            ReverseVectorBook<BranchlessSearch, Table>>{});
        return true;
    }
    if (name == "reverse_linear") {
        f(std::type_identity<
            ReverseVectorBook<LinearSearch, Table>>{});
        return true;
    }
    return false;
}

template<class F>
bool DispatchBook(const std::string& name, const std::string& table,
                  F&& f) {
    if (table == "std")
        return DispatchBookWith<StdOrderTable>(name, f);
    if (table == "identity")
        return DispatchBookWith<FlatIdentityTable>(name, f);
    if (table == "mix")
        return DispatchBookWith<FlatMixTable>(name, f);
    return false;
}

template<class F>
bool DispatchBook(const std::string& name, F&& f) {
    return DispatchBook(name, "std", f);
}

inline std::string JoinNames(const char* const* names,
                             std::size_t n) {
    std::string s;
    for (std::size_t i = 0; i < n; ++i) {
        if (!s.empty())
            s += ", ";
        s += names[i];
    }
    return s;
}

inline std::string KnownBooks() {
    return JoinNames(BOOK_NAMES, std::size(BOOK_NAMES));
}

inline std::string KnownTables() {
    return JoinNames(TABLE_NAMES, std::size(TABLE_NAMES));
}

}

#endif
