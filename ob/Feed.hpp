#ifndef OB_FEED_HPP
#define OB_FEED_HPP

#include "ob/Types.hpp"

#include <string>
#include <vector>

namespace ob {

struct FeedConfig {
    u64 seed = 1;
    u64 updateCount = 10'000'000;
    u32 levelsPerSide = 1000;
    u32 maxDepth = 1000;
    i32 midPrice = 100000;
    u32 ordersPerLevelSeed = 4;
    f64 depthExponent = 1.5;
    f64 newLevelRate = 0.05;
    u32 addPct = 40;
    u32 modifyPct = 30;
    u32 deletePct = 30;
    u32 maxVolume = 500;

    u64 SeedMessageCount() const {
        return static_cast<u64>(levelsPerSide) * ordersPerLevelSeed *
               2;
    }
};

static_assert(std::is_trivially_copyable_v<FeedConfig>);
static_assert(sizeof(FeedConfig) == 64);

constexpr u64 kFeedMagic = 0x4f42464545443031ull;
constexpr u32 kFeedVersion = 1;

struct FeedHeader {
    u64 magic;
    u32 version;
    u32 pad;
    FeedConfig config;
    u64 count;
};

static_assert(std::is_trivially_copyable_v<FeedHeader>);
static_assert(sizeof(FeedHeader) == 88);

std::vector<Update>
GenerateFeed(const FeedConfig& cfg,
             std::vector<u32>* depthOut = nullptr);

void WriteFeed(const std::string& path, const FeedConfig& cfg,
               const std::vector<Update>& updates);

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

  private:
    void* base_ = nullptr;
    u64 bytes_ = 0;
    const FeedHeader* header_ = nullptr;
    const Update* data_ = nullptr;
};

std::string DescribeConfig(const FeedConfig& cfg);

} // namespace ob

#endif
