#include "ob/Validate.hpp"
#include "ob/Bench.hpp"
#include "ob/Feed.hpp"

#include <cstdlib>
#include <iostream>
#include <string>

namespace {

void Usage() {
    std::cerr
        << "usage: ob_validate --feed PATH [--every N] [--depth-csv "
           "PATH]\n"
           "                   [--inside-from HH:MM] [--inside-to "
           "HH:MM]\n";
}

}

int main(int argc, char** argv) {
    std::string feed_path;
    ob::ValidateOptions opt;
    std::string inside_from;
    std::string inside_to;
    for (int i = 1; i < argc; ++i) {
        std::string a = argv[i];
        auto next = [&](const char* name) -> std::string {
            if (i + 1 >= argc) {
                std::cerr << name << " needs a value\n";
                std::exit(1);
            }
            return argv[++i];
        };
        if (a == "--feed")
            feed_path = next("--feed");
        else if (a == "--every")
            opt.every = std::stoull(next("--every"));
        else if (a == "--depth-csv")
            opt.depth_csv = next("--depth-csv");
        else if (a == "--inside-from")
            inside_from = next("--inside-from");
        else if (a == "--inside-to")
            inside_to = next("--inside-to");
        else if (a == "--help" || a == "-h") {
            Usage();
            return 0;
        } else {
            std::cerr << "unknown argument " << a << '\n';
            Usage();
            return 1;
        }
    }
    if (feed_path.empty() || opt.every == 0) {
        Usage();
        return 1;
    }
    try {
        ob::MappedFeed feed(feed_path);
        bool real = feed.source().rfind("itch:", 0) == 0;
        if (inside_from.empty() && real)
            inside_from = "09:30";
        if (inside_to.empty() && real)
            inside_to = "16:00";
        if (!inside_from.empty())
            opt.inside_from_ns = ob::itch::ParseClockNs(inside_from);
        if (!inside_to.empty())
            opt.inside_to_ns = ob::itch::ParseClockNs(inside_to);

        ob::u64 t0 = ob::MonotonicNs();
        ob::ValidateReport r =
            ob::ValidateFeed(feed.data(), feed.count(), opt);
        ob::u64 t1 = ob::MonotonicNs();

        std::cout << "feed:              " << feed_path << " ("
                  << feed.source() << ")\n"
                  << "updates checked:   " << r.updates << " of "
                  << feed.count() << " (add=" << r.adds
                  << " modify=" << r.modifies
                  << " delete=" << r.deletes << ") in "
                  << static_cast<double>(t1 - t0) / 1e9 << " s\n"
                  << "sum checks:        " << r.sum_checks
                  << " every "
                  << opt.every << " updates\n"
                  << "levels per side:   bid max " << r.max_bid_levels
                  << " median " << r.median_bid_levels << ", ask max "
                  << r.max_ask_levels << " median "
                  << r.median_ask_levels << '\n'
                  << "live orders:       high water "
                  << r.live_high_water << ", at end " << r.live_at_end
                  << '\n';
        if (r.executions > 0)
            std::cout
                << "executions:        " << r.executions
                << " inside window "
                << (inside_from.empty() ? "all" : inside_from)
                << " to "
                << (inside_to.empty() ? "all" : inside_to) << ", "
                << r.executions_at_inside << " at the best price ("
                << 100.0 *
                       static_cast<double>(r.executions_at_inside) /
                       static_cast<double>(r.executions)
                << "%)\n";
        else
            std::cout
                << "executions:        none flagged in this feed\n";
        if (!opt.depth_csv.empty())
            std::cout << "depth csv:         " << opt.depth_csv
                      << '\n';
        if (r.ok) {
            std::cout << "result:            OK\n";
            return 0;
        }
        std::cout << "result:            FAIL at update "
                  << r.failed_at << ": " << r.failure << '\n';
        return 1;
    } catch (const std::exception& e) {
        std::cerr << "error: " << e.what() << '\n';
        return 1;
    }
}
