#ifndef LOB_ORDERBOOK_HPP
#define LOB_ORDERBOOK_HPP

#include "lob/Order.hpp"
#include "lob/Trade.hpp"
#include "lob/Types.hpp"

// Interface every book implementation exposes
// Strategies program against this
class OrderBook {
  public:
    virtual ~OrderBook() = default;

    virtual Trades AddOrder(const Order& order) = 0;
    virtual void CancelOrder(Id orderId) = 0;

    // Resting quantity at a price on the given side
    // 0 if the level is empty
    virtual Quantity LevelTotal(Price p, bool bidSide) const = 0;
    virtual bool Contains(Id id) const = 0;
};

#endif
