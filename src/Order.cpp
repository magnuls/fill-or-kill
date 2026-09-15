#include "lob/Order.hpp"

Order::Order(Id orderId, Price level, bool isBuy, Quantity quantity)
    : id_(orderId), p_(level), isBuy_(isBuy), q_(quantity) {}
