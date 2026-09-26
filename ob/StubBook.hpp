#ifndef OB_STUBBOOK_HPP
#define OB_STUBBOOK_HPP

#include "ob/Types.hpp"

namespace ob {

class StubBook {
  public:
    static constexpr const char* kName = "stub";
    static constexpr bool kNoAllocations = true;
    static constexpr bool kIsStub = true;

    void OnAdd(OrderId, Side, Price, Volume) {
        ++counter_;
    }
    void OnModify(OrderId, Volume) {
        ++counter_;
    }
    void OnDelete(OrderId) {
        ++counter_;
    }

    Price BestBid() const {
        return Price{0};
    }
    Price BestAsk() const {
        return Price{0};
    }
    Volume VolumeAtLevel(Side, LevelIdx) const {
        return Volume{0};
    }
    LevelIdx LevelCount(Side) const {
        return 0;
    }

    u64 Count() const {
        return counter_;
    }

  private:
    u64 counter_ = 0;
};

} // namespace ob

#endif
