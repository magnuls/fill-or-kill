#ifndef OB_MAPBOOK_HPP
#define OB_MAPBOOK_HPP

#include "ob/OrderTable.hpp"
#include "ob/Types.hpp"

#include <functional>
#include <iterator>
#include <map>
#include <utility>
#include <vector>

namespace ob {

template<template<class> class Table>
class MapBookT {
  public:
    static constexpr const char* NAME = "map";
    static constexpr bool NO_ALLOCATIONS = false;
    static constexpr bool IS_STUB = false;

    using Bids = std::map<Price, Volume, std::greater<Price>>;
    using Asks = std::map<Price, Volume, std::less<Price>>;

    struct Rec {
        Side side;
        Price price;
        Volume volume;
        Bids::iterator bid;
        Asks::iterator ask;
    };

    static constexpr const char* TABLE_NAME = Table<Rec>::NAME;

    explicit MapBookT(u64 order_reserve = DEFAULT_ORDER_RESERVE)
        : orders_(order_reserve) {}

    void OnAdd(OrderId id, Side side, Price price, Volume volume) {
        auto [rec, inserted] = orders_.TryInsert(id);
        if (!inserted)
            return;
        rec->side = side;
        rec->price = price;
        rec->volume = volume;
        if (side == Side::Bid) {
            auto [it, fresh] = bids_.try_emplace(price, Volume{0});
            it->second += volume;
            rec->bid = it;
        } else {
            auto [it, fresh] = asks_.try_emplace(price, Volume{0});
            it->second += volume;
            rec->ask = it;
        }
    }

    void OnModify(OrderId id, Volume new_volume) {
        Rec* rec = orders_.Find(id);
        if (!rec)
            return;
        if (new_volume.lots <= 0) {
            OnDelete(id);
            return;
        }
        Volume delta = new_volume - rec->volume;
        if (rec->side == Side::Bid)
            rec->bid->second += delta;
        else
            rec->ask->second += delta;
        rec->volume = new_volume;
    }

    void OnDelete(OrderId id) {
        Rec* rec = orders_.Find(id);
        if (!rec)
            return;
        if (rec->side == Side::Bid) {
            rec->bid->second -= rec->volume;
            if (rec->bid->second.lots <= 0)
                bids_.erase(rec->bid);
        } else {
            rec->ask->second -= rec->volume;
            if (rec->ask->second.lots <= 0)
                asks_.erase(rec->ask);
        }
        orders_.Erase(id);
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
        return side == Side::Bid ? at(bids_, level)
                                 : at(asks_, level);
    }

    std::vector<std::pair<Price, Volume>>
    Snapshot(Side side, LevelIdx depth) const {
        return side == Side::Bid ? snap(bids_, depth)
                                 : snap(asks_, depth);
    }

    bool Contains(OrderId id) const {
        return orders_.Contains(id);
    }

    Lookup DebugLookup(OrderId id) const {
        const Rec* rec = orders_.Find(id);
        if (!rec)
            return Lookup{Side::Bid, Price{0}, Volume{0}};
        return Lookup{rec->side, rec->price, rec->volume};
    }

    u64 OrderCount() const {
        return orders_.Size();
    }

    void Prefetch(OrderId id) const {
        orders_.Prefetch(id);
    }

    TableStats OrderTableStats() const {
        return orders_.Stats();
    }

  private:
    template<class M>
    static Volume at(const M& m, LevelIdx level) {
        if (level >= m.size())
            return Volume{0};
        auto it = m.begin();
        std::advance(it, level);
        return it->second;
    }

    template<class M>
    static std::vector<std::pair<Price, Volume>>
    snap(const M& m, LevelIdx depth) {
        std::vector<std::pair<Price, Volume>> out;
        for (const auto& [p, v] : m) {
            if (out.size() >= depth)
                break;
            out.emplace_back(p, v);
        }
        return out;
    }

    Bids bids_;
    Asks asks_;
    Table<Rec> orders_;
};

using MapBook = MapBookT<StdOrderTable>;

}

#endif
