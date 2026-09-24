# LOB

Currently a Pro-Rata Order book

## What exists

- `include/lob/Types.hpp`: `Id`, `Price`, `Quantity` aliases plus fixed-width integer aliases.
- `include/lob/Order.hpp`, `include/lob/Trade.hpp`: the order and trade records. `Trades` is a `std::vector<Trade>`.
- `include/lob/OrderBook.hpp`: the virtual interface every book implements and that strategies will program against: `AddOrder`, `CancelOrder`, `LevelTotal`, `Contains`.
- `include/lob/books/ProRataOrderBook.hpp`, `src/books/ProRataOrderBook.cpp`: the pro-rata book.
  - Price priority across levels. An aggressor sweeps levels at or better than its limit.
  - Within a level, each resting order gets `floor(rem * q_i / total)`. The rounding leftover is handed out one lot at a time to the largest resting orders first, ties broken by lower id.
  - Any unfilled remainder rests on its own side.
  - Orders with a duplicate id or non-positive quantity are ignored. Cancelling an unknown id is a no-op.
- `tests/ProRataOrderBookTest.cpp`: six scenario tests (exact split, leftover lots, sweeping a level, cancel, duplicate id, sell side) using `assert`.
- `main.cpp`: seeds three asks and prints the trades from one crossing buy.
- `strategies/`: empty, reserved for trading strategies.

## Build and run

```sh
cmake -S . -B build
cmake --build build
./build/lob          # demo
./build/lob_tests    # tests
ctest --test-dir build
```

The tests rely on `assert`, so build without `-DNDEBUG` (the default or `Debug` configuration) for them to check anything.

## Layout

```
include/lob/        Types, Order, Trade, OrderBook interface
include/lob/books/  Book implementations (ProRataOrderBook)
src/                Out-of-line definitions
tests/              Test executable
strategies/         Empty
main.cpp            Demo entry point
CMakeLists.txt      Static library `lob`, executables `lob` and `lob_tests`
```

## Not implemented

- FIFO price-time priority book.
- Additional order types (IOC, FOK, AON, GTC, GFD).
- Order modification.
- Latency benchmarking and price-level book variants.
