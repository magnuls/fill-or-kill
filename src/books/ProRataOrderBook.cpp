#include "lob/books/ProRataOrderBook.hpp"

#include <algorithm>
#include <cstddef>
#include <vector>

namespace {

struct Alloc {
    Id id;
    Quantity resting;
    Quantity fill;
};

} // namespace

Trades ProRataOrderBook::AddOrder(const Order& order) {
    if (order.q_ <= 0 || index_.contains(order.id_))
        return {};
    return order.isBuy_ ? match_(order, asks_, bids_) : match_(order, bids_, asks_);
}

void ProRataOrderBook::CancelOrder(const Id orderId) {
    auto it = index_.find(orderId);
    if (it == index_.end())
        return;
    const Locator loc = it->second;
    index_.erase(it);
    loc.isBuy_ ? remove_(bids_, loc.p_, orderId) : remove_(asks_, loc.p_, orderId);
}

Quantity ProRataOrderBook::LevelTotal(Price p, bool bidSide) const {
    if (bidSide) {
        auto it = bids_.find(p);
        return it == bids_.end() ? 0 : it->second.total_;
    }
    auto it = asks_.find(p);
    return it == asks_.end() ? 0 : it->second.total_;
}

bool ProRataOrderBook::Contains(Id id) const {
    return index_.contains(id);
}

void ProRataOrderBook::fillLevel_(Order& order, Price px, Level& level, Trades& trades) {
    Quantity& rem = order.q_;
    auto& queue = level.queue_;

    // Aggressor swallows the whole level: no allocation math needed.
    if (rem >= level.total_) {
        for (auto& [id, resting] : queue) {
            trades.push_back(Trade{id, order.id_, order.id_, order.isBuy_, px, resting.q_});
            index_.erase(id);
        }
        rem -= level.total_;
        level.total_ = 0;
        queue.clear();
        return;
    }

    // Pro-rata: floor allocation, then distribute the rounding leftover.
    std::vector<Alloc> allocs;
    allocs.reserve(queue.size());

    Quantity allocated = 0;
    for (const auto& [id, resting] : queue) {
        const Quantity share =
            static_cast<Quantity>(static_cast<i64>(rem) * resting.q_ / level.total_);
        allocs.push_back({id, resting.q_, share});
        allocated += share;
    }

    Quantity leftover = rem - allocated; // always < allocs.size()
    if (leftover > 0) {
        // Largest resting size first, ties by time priority (lower Id).
        std::vector<std::size_t> by_size(allocs.size());
        for (std::size_t i = 0; i < by_size.size(); ++i)
            by_size[i] = i;
        std::stable_sort(by_size.begin(), by_size.end(), [&](std::size_t a, std::size_t b) {
            return allocs[a].resting > allocs[b].resting;
        });
        for (std::size_t k = 0; k < by_size.size() && leftover > 0; ++k) {
            Alloc& a = allocs[by_size[k]];
            if (a.fill < a.resting) {
                ++a.fill;
                --leftover;
            }
        }
    }

    for (const Alloc& a : allocs) {
        if (a.fill == 0)
            continue;
        trades.push_back(Trade{a.id, order.id_, order.id_, order.isBuy_, px, a.fill});
        auto it = queue.find(a.id);
        it->second.q_ -= a.fill;
        level.total_ -= a.fill;
        if (it->second.q_ == 0) {
            index_.erase(a.id);
            queue.erase(it);
        }
    }
    rem = 0;
}
