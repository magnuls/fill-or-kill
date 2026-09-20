#include "lob/books/ProRataOrderBook.hpp"

#include <cassert>
#include <iostream>

static void show(const char* title, const Trades& trades) {
    std::cout << title << '\n';
    for (const Trade& t : trades) {
        std::cout << "  resting=" << t.OrderIdA
                  << " aggressor=" << t.AggressorOrderId
                  << (t.AggressorIsBuy ? " BUY " : " SELL ") << t.Size
                  << " @ " << t.Level << '\n';
    }
}

// Three resting asks at 100
// sizes 60 / 30 / 10
// ids 1, 2, 3
static ProRataOrderBook seeded() {
    ProRataOrderBook ob;
    assert(ob.AddOrder(Order{1, 100, false, 60}).empty());
    assert(ob.AddOrder(Order{2, 100, false, 30}).empty());
    assert(ob.AddOrder(Order{3, 100, false, 10}).empty());
    assert(ob.LevelTotal(100, false) == 100);
    return ob;
}

static void exact_split() {
    ProRataOrderBook ob = seeded();
    Trades t = ob.AddOrder(Order{10, 100, true, 50});
    show("buy 50 @ 100 -> 30/15/5", t);
    Trades want = {
        {1, 10, 10, true, 100, 30},
        {2, 10, 10, true, 100, 15},
        {3, 10, 10, true, 100, 5},
    };
    assert(t == want);
    assert(ob.LevelTotal(100, false) == 50);
    assert(!ob.Contains(10));
}

static void leftover_to_largest() {
    ProRataOrderBook ob = seeded();
    Trades t = ob.AddOrder(Order{11, 100, true, 7});
    show("buy 7 @ 100 -> floors 4/2/0, leftover 1 to id 1", t);
    Trades want = {
        {1, 11, 11, true, 100, 5},
        {2, 11, 11, true, 100, 2},
    };
    assert(t == want);
    assert(ob.LevelTotal(100, false) == 93);
}

static void sweep_and_rest() {
    ProRataOrderBook ob = seeded();
    assert(ob.AddOrder(Order{4, 101, false, 20}).empty());
    Trades t = ob.AddOrder(Order{12, 101, true, 150});
    show(
        "buy 150 @ 101 -> sweeps 100 fully, takes 20 @ 101, rests 30",
        t);
    Trades want = {
        {1, 12, 12, true, 100, 60},
        {2, 12, 12, true, 100, 30},
        {3, 12, 12, true, 100, 10},
        {4, 12, 12, true, 101, 20},
    };
    assert(t == want);
    assert(ob.LevelTotal(100, false) == 0);
    assert(ob.LevelTotal(101, false) == 0);
    assert(ob.LevelTotal(101, true) == 30);
    assert(ob.Contains(12));
    for (Id id : {1, 2, 3, 4})
        assert(!ob.Contains(id));
}

static void cancel() {
    ProRataOrderBook ob = seeded();
    ob.CancelOrder(1);
    assert(!ob.Contains(1));
    assert(ob.LevelTotal(100, false) == 40);
    Trades t = ob.AddOrder(Order{13, 100, true, 8});
    show("cancel id 1, buy 8 @ 100 -> 6/2 across ids 2,3", t);
    Trades want = {
        {2, 13, 13, true, 100, 6},
        {3, 13, 13, true, 100, 2},
    };
    assert(t == want);
    ob.CancelOrder(999); // unknown id is a no op
    assert(ob.LevelTotal(100, false) == 32);
}

static void duplicate_id() {
    ProRataOrderBook ob = seeded();
    assert(ob.AddOrder(Order{1, 100, true, 50}).empty());
    assert(ob.LevelTotal(100, false) == 100);
    assert(ob.LevelTotal(100, true) == 0);
}

static void sell_side() {
    ProRataOrderBook ob;
    assert(ob.AddOrder(Order{1, 100, true, 40}).empty());
    assert(ob.AddOrder(Order{2, 100, true, 10}).empty());
    Trades t = ob.AddOrder(Order{20, 99, false, 10});
    show("sell 10 @ 99 into bids 40/10 @ 100 -> 8/2", t);
    Trades want = {
        {1, 20, 20, false, 100, 8},
        {2, 20, 20, false, 100, 2},
    };
    assert(t == want);
}

int main() {
    exact_split();
    leftover_to_largest();
    sweep_and_rest();
    cancel();
    duplicate_id();
    sell_side();
    std::cout << "all checks passed\n";
    return 0;
}
