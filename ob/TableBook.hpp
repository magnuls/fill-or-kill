#ifndef OB_TABLEBOOK_HPP
#define OB_TABLEBOOK_HPP

#include "ob/OrderTable.hpp"
#include "ob/Types.hpp"

#include <utility>
#include <vector>

namespace ob {

template<template<class> class Table>
class TableOnlyBook {
  public:
    static constexpr const char* NAME = "table";
    static constexpr bool IS_STUB = true;

    struct Rec {
        Side side;
        Price price;
        Volume volume;
    };

    static constexpr const char* TABLE_NAME = Table<Rec>::NAME;
    static constexpr bool NO_ALLOCATIONS = Table<Rec>::NO_ALLOCATIONS;

    explicit TableOnlyBook(u64 order_reserve = DEFAULT_ORDER_RESERVE)
        : orders_(order_reserve) {}

    void OnAdd(OrderId id, Side side, Price price, Volume volume) {
        auto [rec, inserted] = orders_.TryInsert(id);
        if (!inserted)
            return;
        rec->side = side;
        rec->price = price;
        rec->volume = volume;
    }

    void OnModify(OrderId id, Volume new_volume) {
        Rec* rec = orders_.Find(id);
        if (!rec)
            return;
        if (new_volume.lots <= 0) {
            orders_.Erase(id);
            return;
        }
        rec->volume = new_volume;
    }

    void OnDelete(OrderId id) {
        if (orders_.Find(id))
            orders_.Erase(id);
    }

    Price BestBid() const {
        return Price{0};
    }
    Price BestAsk() const {
        return Price{0};
    }
    LevelIdx LevelCount(Side) const {
        return 0;
    }
    Volume VolumeAtLevel(Side, LevelIdx) const {
        return Volume{0};
    }
    std::vector<std::pair<Price, Volume>> Snapshot(Side,
                                                   LevelIdx) const {
        return {};
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
    Table<Rec> orders_;
};

}

#endif
