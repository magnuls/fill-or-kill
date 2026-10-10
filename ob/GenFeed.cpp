#include "ob/Bench.hpp"
#include "ob/Feed.hpp"

#include <cstdlib>
#include <iostream>
#include <string>

namespace {

void Usage() {
    std::cerr
        << "usage: ob_genfeed --out PATH [--seed N] [--count N]\n"
           "                  [--new-level-rate F] [--levels N]\n"
           "                  [--max-depth N] [--depth-exponent F]\n"
           "                  [--mid N] [--orders-per-level N]\n"
           "                  [--add PCT --modify PCT --delete PCT]\n"
           "                  [--max-volume N]\n";
}

}

int main(int argc, char** argv) {
    ob::FeedConfig cfg;
    std::string out;
    for (int i = 1; i < argc; ++i) {
        std::string a = argv[i];
        auto next = [&](const char* name) -> std::string {
            if (i + 1 >= argc) {
                std::cerr << name << " needs a value\n";
                std::exit(1);
            }
            return argv[++i];
        };
        if (a == "--out")
            out = next("--out");
        else if (a == "--seed")
            cfg.seed = std::stoull(next("--seed"));
        else if (a == "--count")
            cfg.update_count = std::stoull(next("--count"));
        else if (a == "--new-level-rate")
            cfg.new_level_rate = std::stod(next("--new-level-rate"));
        else if (a == "--levels")
            cfg.levels_per_side =
                static_cast<ob::u32>(std::stoul(next("--levels")));
        else if (a == "--max-depth")
            cfg.max_depth =
                static_cast<ob::u32>(std::stoul(next("--max-depth")));
        else if (a == "--depth-exponent")
            cfg.depth_exponent = std::stod(next("--depth-exponent"));
        else if (a == "--mid")
            cfg.mid_price =
                static_cast<ob::i32>(std::stol(next("--mid")));
        else if (a == "--orders-per-level")
            cfg.orders_per_level_seed = static_cast<ob::u32>(
                std::stoul(next("--orders-per-level")));
        else if (a == "--add")
            cfg.add_pct =
                static_cast<ob::u32>(std::stoul(next("--add")));
        else if (a == "--modify")
            cfg.modify_pct =
                static_cast<ob::u32>(std::stoul(next("--modify")));
        else if (a == "--delete")
            cfg.delete_pct =
                static_cast<ob::u32>(std::stoul(next("--delete")));
        else if (a == "--max-volume")
            cfg.max_volume = static_cast<ob::u32>(
                std::stoul(next("--max-volume")));
        else if (a == "--help" || a == "-h") {
            Usage();
            return 0;
        } else {
            std::cerr << "unknown argument " << a << '\n';
            Usage();
            return 1;
        }
    }
    if (out.empty()) {
        Usage();
        return 1;
    }
    try {
        std::cout << "config: " << ob::DescribeConfig(cfg) << '\n';
        ob::u64 t0 = ob::MonotonicNs();
        std::vector<ob::Update> feed = ob::GenerateFeed(cfg);
        ob::u64 t1 = ob::MonotonicNs();
        ob::WriteFeed(out, cfg, feed, "synthetic");
        ob::u64 t2 = ob::MonotonicNs();
        std::cout << "generated " << feed.size() << " updates in "
                  << static_cast<double>(t1 - t0) / 1e9
                  << " s, wrote " << out << " ("
                  << sizeof(ob::FeedHeader) +
                         feed.size() * sizeof(ob::Update)
                  << " bytes) in "
                  << static_cast<double>(t2 - t1) / 1e9 << " s\n";
    } catch (const std::exception& e) {
        std::cerr << "error: " << e.what() << '\n';
        return 1;
    }
    return 0;
}
