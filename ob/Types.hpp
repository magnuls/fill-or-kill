#ifndef OB_TYPES_HPP
#define OB_TYPES_HPP

#include <compare>
#include <cstdint>
#include <type_traits>

namespace ob {

using i8 = std::int8_t;
using i16 = std::int16_t;
using i32 = std::int32_t;
using i64 = std::int64_t;
using u8 = std::uint8_t;
using u16 = std::uint16_t;
using u32 = std::uint32_t;
using u64 = std::uint64_t;
using f64 = double;

enum class Side : u8 { Bid = 0, Ask = 1 };
enum class MsgType : u8 { Add = 0, Modify = 1, Delete = 2 };

using OrderId = u64;
using LevelIdx = u32;

struct Price {
    i32 ticks;

    Price() = default;
    explicit constexpr Price(i32 t) : ticks(t) {}

    constexpr auto operator<=>(const Price&) const = default;
};

struct Volume {
    i64 lots;

    Volume() = default;
    explicit constexpr Volume(i64 l) : lots(l) {}

    constexpr auto operator<=>(const Volume&) const = default;

    constexpr Volume& operator+=(Volume o) {
        lots += o.lots;
        return *this;
    }
    constexpr Volume& operator-=(Volume o) {
        lots -= o.lots;
        return *this;
    }
};

constexpr Volume operator+(Volume a, Volume b) {
    return Volume{a.lots + b.lots};
}
constexpr Volume operator-(Volume a, Volume b) {
    return Volume{a.lots - b.lots};
}
constexpr Volume operator-(Volume a) {
    return Volume{-a.lots};
}

constexpr Side Opposite(Side s) {
    return s == Side::Bid ? Side::Ask : Side::Bid;
}

struct Update {
    MsgType type;
    Side side;
    u16 pad;
    Price price;
    OrderId id;
    Volume volume;
    u64 timestamp;
};

static_assert(sizeof(Price) == 4);
static_assert(sizeof(Volume) == 8);
static_assert(sizeof(Update) == 32);
static_assert(alignof(Update) == 8);
static_assert(std::is_trivially_copyable_v<Update>);
static_assert(std::is_trivially_copyable_v<Price>);
static_assert(std::is_trivially_copyable_v<Volume>);
static_assert(!std::is_convertible_v<Price, Volume>);
static_assert(!std::is_convertible_v<Volume, Price>);
static_assert(!std::is_convertible_v<i32, Price>);
static_assert(!std::is_convertible_v<i64, Volume>);

} // namespace ob

#endif
