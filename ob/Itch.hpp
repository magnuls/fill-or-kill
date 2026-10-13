#ifndef OB_ITCH_HPP
#define OB_ITCH_HPP

#include "ob/Types.hpp"

#include <algorithm>
#include <array>
#include <cstddef>
#include <cstring>
#include <limits>
#include <string>
#include <unordered_map>
#include <unordered_set>
#include <vector>

namespace ob::itch {

constexpr u16 REASON_PLAIN = 0;
constexpr u16 REASON_EXECUTION = 1;
constexpr u16 REASON_CANCEL = 2;
constexpr u16 REASON_REPLACE = 3;

constexpr u64 NS_PER_HOUR = 3'600'000'000'000ull;
constexpr u64 NS_PER_MINUTE = 60'000'000'000ull;

inline u16 BE16(const u8* p) {
    return static_cast<u16>((p[0] << 8) | p[1]);
}

inline u32 BE32(const u8* p) {
    return (static_cast<u32>(p[0]) << 24) |
           (static_cast<u32>(p[1]) << 16) |
           (static_cast<u32>(p[2]) << 8) | static_cast<u32>(p[3]);
}

inline u64 BE48(const u8* p) {
    return (static_cast<u64>(BE16(p)) << 32) | BE32(p + 2);
}

inline u64 BE64(const u8* p) {
    return (static_cast<u64>(BE32(p)) << 32) | BE32(p + 4);
}

inline std::string StockField(const u8* p) {
    std::string s(reinterpret_cast<const char*>(p), 8);
    while (!s.empty() && s.back() == ' ')
        s.pop_back();
    return s;
}

inline u64 ParseClockNs(const std::string& hhmm) {
    auto colon = hhmm.find(':');
    u64 h = std::stoull(hhmm.substr(0, colon));
    u64 m = colon == std::string::npos
                ? 0
                : std::stoull(hhmm.substr(colon + 1));
    return h * NS_PER_HOUR + m * NS_PER_MINUTE;
}

struct ShadowOrder {
    Side side;
    Price price;
    i64 shares;
    bool emitted;
};

struct SymbolState {
    std::string symbol;
    std::vector<Update> updates;
    std::unordered_map<OrderId, ShadowOrder> orders;
    u64 unknown_ids = 0;
    u64 dropped = 0;
    u64 live_high_water = 0;
    bool window_seeded = false;
};

struct Stats {
    std::array<u64, 256> by_type{};
    u64 messages = 0;
    u64 bytes = 0;
    u64 first_ts = std::numeric_limits<u64>::max();
    u64 last_ts = 0;
    u64 symbols_in_directory = 0;
};

class Converter {
  public:
    Converter(const std::vector<std::string>& symbols,
              u64 from_ns = 0,
              u64 to_ns = std::numeric_limits<u64>::max())
        : from_ns_(from_ns), to_ns_(to_ns) {
        for (const std::string& s : symbols) {
            wanted_.insert(s);
            states_.push_back(SymbolState{s, {}, {}, 0, 0, 0, false});
        }
    }

    void OnMessage(const u8* m, std::size_t len) {
        stats_.messages++;
        stats_.bytes += len;
        if (len < 11)
            return;
        u8 type = m[0];
        stats_.by_type[type]++;
        u64 ts = BE48(m + 5);
        stats_.first_ts = std::min(stats_.first_ts, ts);
        stats_.last_ts = std::max(stats_.last_ts, ts);
        if (type == 'R' && len >= 39) {
            stats_.symbols_in_directory++;
            std::string stock = StockField(m + 11);
            if (wanted_.contains(stock)) {
                for (std::size_t i = 0; i < states_.size(); ++i)
                    if (states_[i].symbol == stock)
                        locate_[BE16(m + 1)] = i;
            }
            return;
        }
        auto loc = locate_.find(BE16(m + 1));
        if (loc == locate_.end())
            return;
        SymbolState& st = states_[loc->second];
        switch (type) {
        case 'A':
            if (len >= 36)
                add(st, ts, BE64(m + 11), m[19], BE32(m + 20),
                    BE32(m + 32), REASON_PLAIN);
            break;
        case 'F':
            if (len >= 40)
                add(st, ts, BE64(m + 11), m[19], BE32(m + 20),
                    BE32(m + 32), REASON_PLAIN);
            break;
        case 'E':
            if (len >= 31)
                reduce(st, ts, BE64(m + 11), BE32(m + 19),
                       REASON_EXECUTION);
            break;
        case 'C':
            if (len >= 36)
                reduce(st, ts, BE64(m + 11), BE32(m + 19),
                       REASON_EXECUTION);
            break;
        case 'X':
            if (len >= 23)
                reduce(st, ts, BE64(m + 11), BE32(m + 19),
                       REASON_CANCEL);
            break;
        case 'D':
            if (len >= 19)
                remove(st, ts, BE64(m + 11), REASON_PLAIN);
            break;
        case 'U':
            if (len >= 35)
                replace(st, ts, BE64(m + 11), BE64(m + 19),
                        BE32(m + 27), BE32(m + 31));
            break;
        default:
            break;
        }
    }

    std::vector<SymbolState>& States() {
        return states_;
    }
    const Stats& GetStats() const {
        return stats_;
    }

  private:
    bool in_window(u64 ts) const {
        return ts >= from_ns_ && ts <= to_ns_;
    }

    void seed_window(SymbolState& st, u64 ts) {
        if (st.window_seeded || ts < from_ns_)
            return;
        st.window_seeded = true;
        std::vector<OrderId> ids;
        ids.reserve(st.orders.size());
        for (auto& [id, o] : st.orders)
            if (!o.emitted)
                ids.push_back(id);
        std::sort(ids.begin(), ids.end());
        for (OrderId id : ids) {
            ShadowOrder& o = st.orders[id];
            o.emitted = true;
            st.updates.push_back(Update{MsgType::Add, o.side,
                                        REASON_PLAIN, o.price, id,
                                        Volume{o.shares}, from_ns_});
        }
    }

    void emit(SymbolState& st, MsgType type, const ShadowOrder& o,
              OrderId id, Volume v, u64 ts, u16 reason) {
        if (!in_window(ts))
            return;
        seed_window(st, ts);
        st.updates.push_back(
            Update{type, o.side, reason, o.price, id, v, ts});
    }

    void add(SymbolState& st, u64 ts, OrderId id, u8 side_char,
             u32 shares, u32 price, u16 reason) {
        if (shares == 0 || price == 0 ||
            price >
                static_cast<u32>(std::numeric_limits<i32>::max())) {
            st.dropped++;
            return;
        }
        Side side = side_char == 'B' ? Side::Bid : Side::Ask;
        ShadowOrder o{side, Price{static_cast<i32>(price)},
                      static_cast<i64>(shares), false};
        if (in_window(ts)) {
            seed_window(st, ts);
            o.emitted = true;
        }
        st.orders[id] = o;
        st.live_high_water =
            std::max<u64>(st.live_high_water, st.orders.size());
        if (o.emitted)
            st.updates.push_back(Update{MsgType::Add, side, reason,
                                        o.price, id, Volume{o.shares},
                                        ts});
    }

    void reduce(SymbolState& st, u64 ts, OrderId id, u32 shares,
                u16 reason) {
        auto it = st.orders.find(id);
        if (it == st.orders.end()) {
            st.unknown_ids++;
            return;
        }
        ShadowOrder& o = it->second;
        ensure_emitted(st, id, o, ts);
        i64 before = o.shares;
        o.shares -= static_cast<i64>(shares);
        if (o.shares <= 0) {
            emit(st, MsgType::Delete, o, id, Volume{before}, ts,
                 reason);
            st.orders.erase(it);
            return;
        }
        emit(st, MsgType::Modify, o, id, Volume{o.shares}, ts,
             reason);
    }

    void remove(SymbolState& st, u64 ts, OrderId id, u16 reason) {
        auto it = st.orders.find(id);
        if (it == st.orders.end()) {
            st.unknown_ids++;
            return;
        }
        ShadowOrder& o = it->second;
        ensure_emitted(st, id, o, ts);
        emit(st, MsgType::Delete, o, id, Volume{o.shares}, ts,
             reason);
        st.orders.erase(it);
    }

    void replace(SymbolState& st, u64 ts, OrderId old_id,
                 OrderId new_id, u32 shares, u32 price) {
        auto it = st.orders.find(old_id);
        if (it == st.orders.end()) {
            st.unknown_ids++;
            return;
        }
        Side side = it->second.side;
        remove(st, ts, old_id, REASON_REPLACE);
        add(st, ts, new_id, side == Side::Bid ? 'B' : 'S', shares,
            price, REASON_REPLACE);
    }

    void ensure_emitted(SymbolState& st, OrderId id, ShadowOrder& o,
                       u64 ts) {
        if (o.emitted || !in_window(ts))
            return;
        seed_window(st, ts);
        if (o.emitted)
            return;
        o.emitted = true;
        st.updates.push_back(Update{MsgType::Add, o.side,
                                    REASON_PLAIN, o.price, id,
                                    Volume{o.shares}, ts});
    }

    u64 from_ns_;
    u64 to_ns_;
    std::unordered_set<std::string> wanted_;
    std::vector<SymbolState> states_;
    std::unordered_map<u16, std::size_t> locate_;
    Stats stats_;
};

bool ReadItchFile(const std::string& path, Converter& converter,
                  std::string& error);

}

#endif
