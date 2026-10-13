#ifndef OB_VALIDATE_HPP
#define OB_VALIDATE_HPP

#include "ob/Bench.hpp"
#include "ob/Itch.hpp"
#include "ob/ReferenceBook.hpp"
#include "ob/Types.hpp"

#include <algorithm>
#include <fstream>
#include <limits>
#include <string>
#include <vector>

namespace ob {

struct ValidateOptions {
    u64 every = 10'000;
    u64 inside_from_ns = 0;
    u64 inside_to_ns = std::numeric_limits<u64>::max();
    std::string depth_csv;
};

struct ValidateReport {
    bool ok = true;
    u64 failed_at = 0;
    std::string failure;
    u64 updates = 0;
    u64 adds = 0;
    u64 modifies = 0;
    u64 deletes = 0;
    u64 executions = 0;
    u64 executions_at_inside = 0;
    u64 max_bid_levels = 0;
    u64 max_ask_levels = 0;
    u64 median_bid_levels = 0;
    u64 median_ask_levels = 0;
    u64 live_high_water = 0;
    u64 live_at_end = 0;
    u64 sum_checks = 0;
};

inline ValidateReport ValidateFeed(const Update* updates, u64 count,
                                   const ValidateOptions& opt) {
    ValidateReport r;
    ReferenceBook book;
    std::vector<u64> bid_levels;
    std::vector<u64> ask_levels;
    std::ofstream depth;
    if (!opt.depth_csv.empty()) {
        depth.open(opt.depth_csv);
        depth << "index,timestamp,bid_levels,ask_levels,orders\n";
    }
    auto fail = [&](u64 i, const std::string& why) {
        r.ok = false;
        r.failed_at = i;
        r.failure = why;
    };
    for (u64 i = 0; i < count && r.ok; ++i) {
        const Update& u = updates[i];
        r.updates++;
        if (u.type == MsgType::Add) {
            r.adds++;
            if (book.Contains(u.id)) {
                fail(i, "add of an id that is already live");
                break;
            }
            if (u.volume.lots <= 0 || u.price.ticks <= 0) {
                fail(i, "add with non positive volume or price");
                break;
            }
        } else {
            if (u.type == MsgType::Modify)
                r.modifies++;
            else
                r.deletes++;
            if (!book.Contains(u.id)) {
                fail(i, "modify or delete of an unknown id");
                break;
            }
            ReferenceBook::Lookup lk = book.DebugLookup(u.id);
            if (lk.side != u.side || lk.price != u.price) {
                fail(i,
                     "modify or delete carries a side or price that "
                     "does not match the resting order");
                break;
            }
            if (u.type == MsgType::Modify && u.volume.lots <= 0) {
                fail(i, "modify to non positive volume");
                break;
            }
            if (u.type == MsgType::Delete && u.volume != lk.volume) {
                fail(i, "delete volume does not match the resting "
                        "volume");
                break;
            }
            if (u.pad == itch::REASON_EXECUTION &&
                u.timestamp >= opt.inside_from_ns &&
                u.timestamp <= opt.inside_to_ns) {
                r.executions++;
                Price best = lk.side == Side::Bid ? book.BestBid()
                                                  : book.BestAsk();
                if (best == lk.price)
                    r.executions_at_inside++;
            }
        }
        ApplyUpdate(book, u);
        if (book.LevelCount(Side::Bid) > 0 &&
            book.LevelCount(Side::Ask) > 0 &&
            book.BestBid() >= book.BestAsk()) {
            fail(i, "best bid at or above best ask");
            break;
        }
        r.live_high_water =
            std::max(r.live_high_water, book.OrderCount());
        if ((i + 1) % opt.every == 0 || i + 1 == count) {
            r.sum_checks++;
            for (Side side : {Side::Bid, Side::Ask}) {
                auto snap =
                    book.Snapshot(side, book.LevelCount(side));
                Volume sum{0};
                for (const auto& [price, v] : snap) {
                    if (v.lots <= 0) {
                        fail(i, "level with non positive volume");
                        break;
                    }
                    sum += v;
                }
                if (!r.ok)
                    break;
                if (sum != book.TotalVolume(side)) {
                    fail(i,
                         "sum of level volumes differs from sum of "
                         "order volumes");
                    break;
                }
            }
            if (!r.ok)
                break;
            if (book.OrderCount(Side::Bid) +
                    book.OrderCount(Side::Ask) !=
                book.OrderCount()) {
                fail(
                    i,
                    "order table size differs from orders in levels");
                break;
            }
            u64 b = book.LevelCount(Side::Bid);
            u64 a = book.LevelCount(Side::Ask);
            bid_levels.push_back(b);
            ask_levels.push_back(a);
            r.max_bid_levels = std::max(r.max_bid_levels, b);
            r.max_ask_levels = std::max(r.max_ask_levels, a);
            if (depth.is_open())
                depth << i << ',' << u.timestamp << ',' << b << ','
                      << a << ',' << book.OrderCount() << '\n';
        }
    }
    r.live_at_end = book.OrderCount();
    auto median = [](std::vector<u64>& v) -> u64 {
        if (v.empty())
            return 0;
        std::sort(v.begin(), v.end());
        return v[v.size() / 2];
    };
    r.median_bid_levels = median(bid_levels);
    r.median_ask_levels = median(ask_levels);
    return r;
}

}

#endif
