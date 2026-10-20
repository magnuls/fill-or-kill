#ifndef OB_DIFF_HPP
#define OB_DIFF_HPP

#include "ob/Bench.hpp"
#include "ob/ReferenceBook.hpp"
#include "ob/Types.hpp"

#include <sstream>
#include <string>
#include <vector>

namespace ob {

struct DiffReport {
    bool ok = true;
    u64 failed_at = 0;
    std::string failure;
    u64 updates = 0;
    u64 full_compares = 0;
};

template<class Book>
bool CompareTop(const ReferenceBook& ref, const Book& book,
                LevelIdx depth, std::string& why) {
    for (Side side : {Side::Bid, Side::Ask}) {
        if (ref.LevelCount(side) != book.LevelCount(side)) {
            std::ostringstream os;
            os << (side == Side::Bid ? "bid" : "ask")
               << " level count " << book.LevelCount(side)
               << " expected " << ref.LevelCount(side);
            why = os.str();
            return false;
        }
        LevelIdx n = std::min<LevelIdx>(depth, ref.LevelCount(side));
        for (LevelIdx i = 0; i < n; ++i) {
            if (ref.VolumeAtLevel(side, i) !=
                book.VolumeAtLevel(side, i)) {
                std::ostringstream os;
                os << (side == Side::Bid ? "bid" : "ask") << " level "
                   << i << " volume "
                   << book.VolumeAtLevel(side, i).lots << " expected "
                   << ref.VolumeAtLevel(side, i).lots;
                why = os.str();
                return false;
            }
        }
    }
    if (ref.BestBid() != book.BestBid()) {
        why = "best bid differs";
        return false;
    }
    if (ref.BestAsk() != book.BestAsk()) {
        why = "best ask differs";
        return false;
    }
    return true;
}

template<class Book>
bool CompareFull(const ReferenceBook& ref, const Book& book,
                 std::string& why) {
    for (Side side : {Side::Bid, Side::Ask}) {
        auto a = ref.Snapshot(side, ref.LevelCount(side));
        auto b = book.Snapshot(side, book.LevelCount(side));
        if (a.size() != b.size()) {
            std::ostringstream os;
            os << (side == Side::Bid ? "bid" : "ask")
               << " snapshot size " << b.size() << " expected "
               << a.size();
            why = os.str();
            return false;
        }
        for (std::size_t i = 0; i < a.size(); ++i) {
            if (a[i] != b[i]) {
                std::ostringstream os;
                os << (side == Side::Bid ? "bid" : "ask") << " level "
                   << i << " is " << b[i].first.ticks << "x"
                   << b[i].second.lots << " expected "
                   << a[i].first.ticks << "x" << a[i].second.lots;
                why = os.str();
                return false;
            }
        }
    }
    if (ref.OrderCount() != book.OrderCount()) {
        std::ostringstream os;
        os << "order count " << book.OrderCount() << " expected "
           << ref.OrderCount();
        why = os.str();
        return false;
    }
    return true;
}

template<class Book>
DiffReport DiffFeed(const Update* updates, u64 count, u64 every,
                    LevelIdx top_depth = 8,
                    u64 order_reserve = DEFAULT_ORDER_RESERVE) {
    DiffReport r;
    ReferenceBook ref;
    Book book(order_reserve);
    if (every == 0)
        every = 1;
    std::string why;
    for (u64 i = 0; i < count; ++i) {
        const Update& u = updates[i];
        ApplyUpdate(ref, u);
        ApplyUpdate(book, u);
        r.updates++;
        if (!CompareTop(ref, book, top_depth, why)) {
            r.ok = false;
            r.failed_at = i;
            r.failure = why;
            return r;
        }
        if (ref.Contains(u.id) != book.Contains(u.id)) {
            r.ok = false;
            r.failed_at = i;
            r.failure = "order presence differs for the touched id";
            return r;
        }
        if ((i + 1) % every == 0 || i + 1 == count) {
            r.full_compares++;
            if (!CompareFull(ref, book, why)) {
                r.ok = false;
                r.failed_at = i;
                r.failure = why;
                return r;
            }
        }
    }
    return r;
}

}

#endif
