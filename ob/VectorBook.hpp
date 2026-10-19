#ifndef OB_VECTORBOOK_HPP
#define OB_VECTORBOOK_HPP

#include "ob/OrderTable.hpp"
#include "ob/Types.hpp"

#include <algorithm>
#include <cstddef>
#include <utility>
#include <vector>

namespace ob {

constexpr std::size_t LEVEL_RESERVE = 8192;

template<class Better>
class ForwardSide {
  public:
    ForwardSide() {
        levels_.reserve(LEVEL_RESERVE);
    }

    std::size_t Find(Price p) const {
        auto it = std::lower_bound(levels_.begin(), levels_.end(), p,
                                   [](const Level& l, Price x) {
                                       return Better{}(l.price, x);
                                   });
        return static_cast<std::size_t>(it - levels_.begin());
    }

    void Add(Price p, Volume v) {
        std::size_t i = Find(p);
        if (i < levels_.size() && levels_[i].price == p) {
            levels_[i].volume += v;
            return;
        }
        levels_.insert(levels_.begin() +
                           static_cast<std::ptrdiff_t>(i),
                       Level{p, v});
    }

    void Adjust(Price p, Volume delta) {
        std::size_t i = Find(p);
        levels_[i].volume += delta;
        if (levels_[i].volume.lots <= 0)
            levels_.erase(levels_.begin() +
                          static_cast<std::ptrdiff_t>(i));
    }

    Price Best() const {
        return levels_.empty() ? Price{0} : levels_.front().price;
    }

    LevelIdx Count() const {
        return static_cast<LevelIdx>(levels_.size());
    }

    Volume At(LevelIdx level) const {
        return level < levels_.size() ? levels_[level].volume
                                      : Volume{0};
    }

    std::vector<std::pair<Price, Volume>>
    Snapshot(LevelIdx depth) const {
        std::vector<std::pair<Price, Volume>> out;
        for (std::size_t i = 0; i < levels_.size() && i < depth; ++i)
            out.emplace_back(levels_[i].price, levels_[i].volume);
        return out;
    }

  private:
    std::vector<Level> levels_;
};

template<template<class> class Table>
class VectorBookT {
  public:
    static constexpr const char* NAME = "vector";
    static constexpr bool IS_STUB = false;

    struct Rec {
        Side side;
        Price price;
        Volume volume;
    };

    static constexpr const char* TABLE_NAME = Table<Rec>::NAME;
    static constexpr bool NO_ALLOCATIONS = Table<Rec>::NO_ALLOCATIONS;

    explicit VectorBookT(u64 order_reserve = DEFAULT_ORDER_RESERVE)
        : orders_(order_reserve) {}

    void OnAdd(OrderId id, Side side, Price price, Volume volume) {
        auto [rec, inserted] = orders_.TryInsert(id);
        if (!inserted)
            return;
        rec->side = side;
        rec->price = price;
        rec->volume = volume;
        if (side == Side::Bid)
            bids_.Add(price, volume);
        else
            asks_.Add(price, volume);
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
            bids_.Adjust(rec->price, delta);
        else
            asks_.Adjust(rec->price, delta);
        rec->volume = new_volume;
    }

    void OnDelete(OrderId id) {
        Rec* rec = orders_.Find(id);
        if (!rec)
            return;
        if (rec->side == Side::Bid)
            bids_.Adjust(rec->price, -rec->volume);
        else
            asks_.Adjust(rec->price, -rec->volume);
        orders_.Erase(id);
    }

    Price BestBid() const {
        return bids_.Best();
    }
    Price BestAsk() const {
        return asks_.Best();
    }

    LevelIdx LevelCount(Side side) const {
        return side == Side::Bid ? bids_.Count() : asks_.Count();
    }

    Volume VolumeAtLevel(Side side, LevelIdx level) const {
        return side == Side::Bid ? bids_.At(level) : asks_.At(level);
    }

    std::vector<std::pair<Price, Volume>>
    Snapshot(Side side, LevelIdx depth) const {
        return side == Side::Bid ? bids_.Snapshot(depth)
                                 : asks_.Snapshot(depth);
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
    ForwardSide<BidBetter> bids_;
    ForwardSide<AskBetter> asks_;
    Table<Rec> orders_;
};

using VectorBook = VectorBookT<StdOrderTable>;

}

#endif
