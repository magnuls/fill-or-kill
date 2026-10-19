#ifndef OB_REFERENCEBOOK_HPP
#define OB_REFERENCEBOOK_HPP

#include "ob/Types.hpp"

#include <functional>
#include <map>
#include <unordered_map>
#include <utility>
#include <vector>

namespace ob {

class ReferenceBook {
  public:
    static constexpr const char* NAME = "ref";
    static constexpr bool NO_ALLOCATIONS = false;
    static constexpr bool IS_STUB = false;
    static constexpr const char* TABLE_NAME = "";

    explicit ReferenceBook(u64 = 0) {}

    struct Lookup {
        Side side;
        Price price;
        Volume volume;
    };

    struct LevelOrders {
        Volume total{0};
        std::map<OrderId, Volume> orders;
    };

    template<class Cmp>
    using SideMap = std::map<Price, LevelOrders, Cmp>;

    void OnAdd(OrderId id, Side side, Price price, Volume volume) {
        if (orders_.contains(id))
            return;
        if (side == Side::Bid)
            insert(bids_, price, id, volume);
        else
            insert(asks_, price, id, volume);
        orders_[id] = Locator{side, price};
    }

    void OnModify(OrderId id, Volume new_volume) {
        auto it = orders_.find(id);
        if (it == orders_.end())
            return;
        if (new_volume.lots <= 0) {
            OnDelete(id);
            return;
        }
        if (it->second.side == Side::Bid)
            modify(bids_, it->second.price, id, new_volume);
        else
            modify(asks_, it->second.price, id, new_volume);
    }

    void OnDelete(OrderId id) {
        auto it = orders_.find(id);
        if (it == orders_.end())
            return;
        if (it->second.side == Side::Bid)
            erase(bids_, it->second.price, id);
        else
            erase(asks_, it->second.price, id);
        orders_.erase(it);
    }

    Price BestBid() const {
        return bids_.empty() ? Price{0} : bids_.begin()->first;
    }
    Price BestAsk() const {
        return asks_.empty() ? Price{0} : asks_.begin()->first;
    }

    LevelIdx LevelCount(Side side) const {
        return static_cast<LevelIdx>(
            side == Side::Bid ? bids_.size() : asks_.size());
    }

    Volume VolumeAtLevel(Side side, LevelIdx level) const {
        return side == Side::Bid ? volume_at(bids_, level)
                                 : volume_at(asks_, level);
    }

    bool Contains(OrderId id) const {
        return orders_.contains(id);
    }

    Lookup DebugLookup(OrderId id) const {
        auto it = orders_.find(id);
        if (it == orders_.end())
            return Lookup{Side::Bid, Price{0}, Volume{0}};
        const Locator& loc = it->second;
        Volume v = loc.side == Side::Bid
                       ? bids_.at(loc.price).orders.at(id)
                       : asks_.at(loc.price).orders.at(id);
        return Lookup{loc.side, loc.price, v};
    }

    std::vector<std::pair<Price, Volume>>
    Snapshot(Side side, LevelIdx depth) const {
        return side == Side::Bid ? snapshot(bids_, depth)
                                 : snapshot(asks_, depth);
    }

    u64 OrderCount() const {
        return orders_.size();
    }

    void Prefetch(OrderId) const {}

    u64 OrderCount(Side side) const {
        return side == Side::Bid ? order_count(bids_)
                                 : order_count(asks_);
    }

    Volume TotalVolume(Side side) const {
        return side == Side::Bid ? total(bids_) : total(asks_);
    }

  private:
    struct Locator {
        Side side;
        Price price;
    };

    template<class M>
    static void insert(M& book, Price price, OrderId id, Volume v) {
        LevelOrders& lvl = book[price];
        lvl.orders[id] = v;
        lvl.total += v;
    }

    template<class M>
    static void modify(M& book, Price price, OrderId id, Volume v) {
        LevelOrders& lvl = book.at(price);
        Volume& cur = lvl.orders.at(id);
        lvl.total += v - cur;
        cur = v;
    }

    template<class M>
    static void erase(M& book, Price price, OrderId id) {
        auto lvl = book.find(price);
        if (lvl == book.end())
            return;
        auto it = lvl->second.orders.find(id);
        if (it != lvl->second.orders.end()) {
            lvl->second.total -= it->second;
            lvl->second.orders.erase(it);
        }
        if (lvl->second.orders.empty())
            book.erase(lvl);
    }

    template<class M>
    static Volume volume_at(const M& book, LevelIdx level) {
        if (level >= book.size())
            return Volume{0};
        auto it = book.begin();
        std::advance(it, level);
        return it->second.total;
    }

    template<class M>
    static std::vector<std::pair<Price, Volume>>
    snapshot(const M& book, LevelIdx depth) {
        std::vector<std::pair<Price, Volume>> out;
        for (const auto& [price, lvl] : book) {
            if (out.size() >= depth)
                break;
            out.emplace_back(price, lvl.total);
        }
        return out;
    }

    template<class M>
    static u64 order_count(const M& book) {
        u64 n = 0;
        for (const auto& [price, lvl] : book)
            n += lvl.orders.size();
        return n;
    }

    template<class M>
    static Volume total(const M& book) {
        Volume sum{0};
        for (const auto& [price, lvl] : book)
            for (const auto& [id, v] : lvl.orders)
                sum += v;
        return sum;
    }

    SideMap<std::greater<Price>> bids_;
    SideMap<std::less<Price>> asks_;
    std::unordered_map<OrderId, Locator> orders_;
};

}

#endif
