#include "lob/books/ProRataOrderBook.hpp"

#include <iostream>

int main() {
    ProRataOrderBook book;
    book.AddOrder(Order{1, 100, false, 60});
    book.AddOrder(Order{2, 100, false, 30});
    book.AddOrder(Order{3, 100, false, 10});

    for (const Trade& t : book.AddOrder(Order{10, 100, true, 50})) {
        std::cout << "resting=" << t.OrderIdA << " aggressor=" << t.AggressorOrderId
                  << (t.AggressorIsBuy ? " BUY " : " SELL ") << t.Size << " @ " << t.Level << '\n';
    }
    return 0;
}
