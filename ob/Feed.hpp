#ifndef OB_FEED_HPP
#define OB_FEED_HPP

#include "ob/Types.hpp"

#include <string>
#include <vector>

namespace ob {

struct FeedConfig {
    u64 seed = 1;
    u64 update_count = 10'000'000;
    u32 levels_per_side = 1000;
    u32 max_depth = 1000;
    i32 mid_price = 100000;
    u32 orders_per_level_seed = 4;
    f64 depth_exponent = 1.5;
    f64 new_level_rate = 0.05;
    u32 add_pct = 40;
    u32 modify_pct = 30;
    u32 delete_pct = 30;
    u32 max_volume = 500;

    u64 SeedMessageCount() const {
        return static_cast<u64>(levels_per_side) *
               orders_per_level_seed * 2;
    }
};

static_assert(std::is_trivially_copyable_v<FeedConfig>);
static_assert(sizeof(FeedConfig) == 64);

constexpr u64 FEED_MAGIC = 0x4f42464545443031ull;
constexpr u32 FEED_VERSION = 2;
constexpr std::size_t FEED_SOURCE_BYTES = 32;

struct FeedHeader {
    u64 magic;
    u32 version;
    u32 pad;
    FeedConfig config;
    u64 count;
    char source[FEED_SOURCE_BYTES];
};

static_assert(std::is_trivially_copyable_v<FeedHeader>);
static_assert(sizeof(FeedHeader) == 120);

std::vector<Update>
GenerateFeed(const FeedConfig& cfg,
             std::vector<u32>* depth_out = nullptr);

void WriteFeed(const std::string& path, const FeedConfig& cfg,
               const std::vector<Update>& updates,
               const std::string& source = "synthetic");

class MappedFeed {
  public:
    explicit MappedFeed(const std::string& path);
    ~MappedFeed();
    MappedFeed(const MappedFeed&) = delete;
    MappedFeed& operator=(const MappedFeed&) = delete;

    const FeedHeader& header() const {
        return *header_;
    }
    const FeedConfig& config() const {
        return header_->config;
    }
    const Update* data() const {
        return data_;
    }
    u64 count() const {
        return header_->count;
    }
    u64 bytes() const {
        return bytes_;
    }
    std::string source() const;

  private:
    void* base_ = nullptr;
    u64 bytes_ = 0;
    const FeedHeader* header_ = nullptr;
    const Update* data_ = nullptr;
};

std::string DescribeConfig(const FeedConfig& cfg);

}

#endif
