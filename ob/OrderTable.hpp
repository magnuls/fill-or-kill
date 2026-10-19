#ifndef OB_ORDERTABLE_HPP
#define OB_ORDERTABLE_HPP

#include "ob/Types.hpp"

#include <algorithm>
#include <stdexcept>
#include <unordered_map>
#include <utility>
#include <vector>

namespace ob {

struct Lookup {
    Side side;
    Price price;
    Volume volume;
};

struct Level {
    Price price;
    Volume volume;
};

struct BidBetter {
    bool operator()(Price a, Price b) const {
        return a > b;
    }
};

struct AskBetter {
    bool operator()(Price a, Price b) const {
        return a < b;
    }
};

struct TableStats {
    u64 size = 0;
    u64 capacity = 0;
    f64 load = 0;
    f64 avg_probe = 0;
    u64 max_probe = 0;
};

constexpr u64 DEFAULT_ORDER_RESERVE = 1 << 16;

template<class Rec>
class StdOrderTable {
  public:
    static constexpr const char* NAME = "std";
    static constexpr bool NO_ALLOCATIONS = false;

    explicit StdOrderTable(u64 n = DEFAULT_ORDER_RESERVE) {
        Reserve(n);
    }

    void Reserve(u64 n) {
        map_.reserve(n);
    }

    Rec* Find(OrderId id) {
        auto it = map_.find(id);
        return it == map_.end() ? nullptr : &it->second;
    }

    const Rec* Find(OrderId id) const {
        auto it = map_.find(id);
        return it == map_.end() ? nullptr : &it->second;
    }

    std::pair<Rec*, bool> TryInsert(OrderId id) {
        auto [it, inserted] = map_.try_emplace(id);
        return {&it->second, inserted};
    }

    void Erase(OrderId id) {
        map_.erase(id);
    }

    bool Contains(OrderId id) const {
        return map_.contains(id);
    }

    u64 Size() const {
        return map_.size();
    }

    void Prefetch(OrderId) const {}

    TableStats Stats() const {
        TableStats t;
        t.size = map_.size();
        t.capacity = map_.bucket_count();
        t.load = map_.load_factor();
        return t;
    }

  private:
    std::unordered_map<OrderId, Rec> map_;
};

struct IdentityHash {
    static constexpr const char* NAME = "identity";
    static u64 Mix(OrderId id) {
        return id;
    }
};

struct MixHash {
    static constexpr const char* NAME = "mix";
    static u64 Mix(OrderId id) {
        u64 x = id * 0x9E3779B97F4A7C15ull;
        return x ^ (x >> 32);
    }
};

template<class Rec, class Hash>
class FlatOrderTable {
  public:
    static constexpr const char* NAME = Hash::NAME;
    static constexpr bool NO_ALLOCATIONS = true;

    struct Slot {
        OrderId key;
        Rec value;
    };

    explicit FlatOrderTable(u64 n = DEFAULT_ORDER_RESERVE) {
        Reserve(n);
    }

    void Reserve(u64 n) {
        u64 cap = 16;
        while (cap < 2 * n)
            cap <<= 1;
        slots_.assign(cap, Slot{});
        mask_ = cap - 1;
        limit_ = cap / 2;
        size_ = 0;
    }

    Rec* Find(OrderId id) {
        if (id == 0)
            return nullptr;
        u64 i = Home(id);
        while (true) {
            Slot& s = slots_[i];
            if (s.key == id)
                return &s.value;
            if (s.key == 0)
                return nullptr;
            i = (i + 1) & mask_;
        }
    }

    const Rec* Find(OrderId id) const {
        return const_cast<FlatOrderTable*>(this)->Find(id);
    }

    std::pair<Rec*, bool> TryInsert(OrderId id) {
        if (id == 0)
            return {nullptr, false};
        u64 i = Home(id);
        while (true) {
            Slot& s = slots_[i];
            if (s.key == id)
                return {&s.value, false};
            if (s.key == 0) {
                if (size_ == limit_)
                    throw std::length_error(
                        "FlatOrderTable is at half load, a rehash "
                        "inside the run is a hard error");
                s.key = id;
                ++size_;
                return {&s.value, true};
            }
            i = (i + 1) & mask_;
        }
    }

    void Erase(OrderId id) {
        if (id == 0)
            return;
        u64 hole = Home(id);
        while (true) {
            OrderId k = slots_[hole].key;
            if (k == id)
                break;
            if (k == 0)
                return;
            hole = (hole + 1) & mask_;
        }
        u64 j = (hole + 1) & mask_;
        while (slots_[j].key != 0) {
            u64 h = Home(slots_[j].key);
            bool stays = hole < j ? (hole < h && h <= j)
                                  : (hole < h || h <= j);
            if (!stays) {
                slots_[hole] = slots_[j];
                hole = j;
            }
            j = (j + 1) & mask_;
        }
        slots_[hole].key = 0;
        --size_;
    }

    bool Contains(OrderId id) const {
        return Find(id) != nullptr;
    }

    u64 Size() const {
        return size_;
    }

    u64 Capacity() const {
        return slots_.size();
    }

    void Prefetch(OrderId id) const {
        __builtin_prefetch(&slots_[Home(id)]);
    }

    TableStats Stats() const {
        TableStats t;
        t.size = size_;
        t.capacity = slots_.size();
        t.load =
            static_cast<f64>(size_) / static_cast<f64>(slots_.size());
        u64 total = 0;
        for (u64 i = 0; i < slots_.size(); ++i) {
            OrderId k = slots_[i].key;
            if (k == 0)
                continue;
            u64 d = (i - Home(k)) & mask_;
            total += d;
            t.max_probe = std::max(t.max_probe, d);
        }
        t.avg_probe =
            size_ ? static_cast<f64>(total) / static_cast<f64>(size_)
                  : 0.0;
        return t;
    }

  private:
    u64 Home(OrderId id) const {
        return Hash::Mix(id) & mask_;
    }

    std::vector<Slot> slots_;
    u64 mask_ = 0;
    u64 limit_ = 0;
    u64 size_ = 0;
};

template<class Rec>
using FlatIdentityTable = FlatOrderTable<Rec, IdentityHash>;

template<class Rec>
using FlatMixTable = FlatOrderTable<Rec, MixHash>;

constexpr const char* TABLE_NAMES[] = {"std", "identity", "mix"};

}

#endif
