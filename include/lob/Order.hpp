#ifndef LOB_ORDER_HPP
#define LOB_ORDER_HPP

#include "lob/Types.hpp"

#include <vector>

struct Order {
    Order(Id orderId, Price level, bool isBuy, Quantity quantity);

    Id id_;
    Price p_;
    bool isBuy_;
    Quantity q_;
};

using Orders = std::vector<Order>;

#endif // LOB_ORDER_HPP
