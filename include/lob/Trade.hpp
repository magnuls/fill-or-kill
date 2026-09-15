#ifndef LOB_TRADE_HPP
#define LOB_TRADE_HPP

#include "lob/Types.hpp"

#include <vector>

struct Trade {
    Id OrderIdA;
    Id OrderIdB; // Aggressor's OrderId
    Id AggressorOrderId;
    bool AggressorIsBuy;
    Price Level;
    Quantity Size;
    bool operator==(const Trade&) const = default;
};

using Trades = std::vector<Trade>;

#endif // LOB_TRADE_HPP
