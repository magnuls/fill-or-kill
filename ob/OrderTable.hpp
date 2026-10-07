#ifndef OB_ORDERTABLE_HPP
#define OB_ORDERTABLE_HPP

#include "ob/Types.hpp"

#include <unordered_map>
#include <utility>

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

template<class Rec>
class OrderTable {
  public:
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

  private:
    std::unordered_map<OrderId, Rec> map_;
};

}

#endif
