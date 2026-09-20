#ifndef LOB_BOOKS_PRORATAORDERBOOK_HPP
#define LOB_BOOKS_PRORATAORDERBOOK_HPP

#include "lob/OrderBook.hpp"

#include <functional>
#include <iterator>
#include <map>
#include <unordered_map>

// Pro-rata limit order book.
//
// Price priority across levels is unchanged from a FIFO book
// Within a level an aggressor is allocated across resting orders in
// proportion to their size
//   fill_i = floor(rem * q_i / total)
// The rounding remainder (< number of orders) is handed out one lot
// at a time to the largest resting orders first.
// ties broken by time priority (smaller Id)
class ProRataOrderBook final : public OrderBook {
    struct Level {
        Quantity total_ = 0;
        std::map<Id, Order> queue_; // Id is monotonically assigned so
                                    // this is time order
    };

    template<typename Cmp>
    using Book = std::map<Price, Level, Cmp>;

    struct Locator {
        Price p_;
        bool isBuy_;
    };

  public:
    Trades AddOrder(const Order& order) override;
    void CancelOrder(Id orderId) override;
    Quantity LevelTotal(Price p, bool bidSide) const override;
    bool Contains(Id id) const override;

  private:
    // Opp is the side the aggressor trades against
    // Same is where its remainder rests
    // Templated on the side type bodies are below the class since
    // templates must be visible wherever they are instantiate
    // fillLevel_ is not a template and lives in the .cpp
    template<typename Opp, typename Same>
    Trades match_(Order order, Opp& opp, Same& same);

    void fillLevel_(Order& order, Price px, Level& level,
                    Trades& trades);

    template<typename Same>
    void rest_(const Order& order, Same& same);

    template<typename B>
    void remove_(B& book, Price p, Id id);

    Book<std::greater<Price>> bids_;
    Book<std::less<Price>> asks_;
    std::unordered_map<Id, Locator> index_;
};

// templated member definitions

template<typename Opp, typename Same>
Trades ProRataOrderBook::match_(Order order, Opp& opp, Same& same) {
    Trades trades;
    Quantity& rem = order.q_;

    for (auto lvl = opp.begin(); lvl != opp.end() && rem > 0;) {
        const Price px = lvl->first;
        if (order.isBuy_ ? px > order.p_ : px < order.p_)
            break; // equal prices cross
                   // only strictly worse stops the sweep

        fillLevel_(order, px, lvl->second, trades);
        lvl = lvl->second.queue_.empty() ? opp.erase(lvl)
                                         : std::next(lvl);
    }

    if (rem > 0)
        rest_(order, same);
    return trades;
}

template<typename Same>
void ProRataOrderBook::rest_(const Order& order, Same& same) {
    Level& level = same[order.p_];
    level.queue_.emplace(order.id_, order);
    level.total_ += order.q_;
    index_.emplace(order.id_, Locator{order.p_, order.isBuy_});
}

template<typename B>
void ProRataOrderBook::remove_(B& book, Price p, Id id) {
    auto lvl = book.find(p);
    if (lvl == book.end())
        return;
    auto it = lvl->second.queue_.find(id);
    if (it == lvl->second.queue_.end())
        return;
    lvl->second.total_ -= it->second.q_;
    lvl->second.queue_.erase(it);
    if (lvl->second.queue_.empty())
        book.erase(lvl);
}

#endif
