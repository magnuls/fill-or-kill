#include "ob/Diff.hpp"
#include "ob/Bench.hpp"
#include "ob/Books.hpp"
#include "ob/Feed.hpp"

#include <cstdlib>
#include <iostream>
#include <string>
#include <vector>

namespace {

void Usage() {
    std::cerr
        << "usage: ob_diff --feed PATH [--book NAME|all] [--every "
           "N] [--depth N]\n";
}

}

int main(int argc, char** argv) {
    std::string feed_path;
    std::string book = "all";
    ob::u64 every = 1000;
    ob::LevelIdx depth = 8;
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
        else if (a == "--book")
            book = next("--book");
        else if (a == "--every")
            every = std::stoull(next("--every"));
        else if (a == "--depth")
            depth = static_cast<ob::LevelIdx>(
                std::stoul(next("--depth")));
        else if (a == "--help" || a == "-h") {
            Usage();
            return 0;
        } else {
            std::cerr << "unknown argument " << a << '\n';
            Usage();
            return 1;
        }
    }
    if (feed_path.empty()) {
        Usage();
        return 1;
    }
    try {
        ob::MappedFeed feed(feed_path);
        std::vector<std::string> names;
        if (book == "all")
            for (const char* n : ob::VARIANT_NAMES)
                names.push_back(n);
        else
            names.push_back(book);
        std::cout << "feed:    " << feed_path << " (" << feed.source()
                  << "), " << feed.count()
                  << " updates, full compare every " << every
                  << ", top depth " << depth << " every update\n";
        int rc = 0;
        for (const std::string& n : names) {
            ob::u64 t0 = ob::MonotonicNs();
            ob::DiffReport r;
            bool known = ob::DispatchBook(n, [&](auto tag) {
                using B = typename decltype(tag)::type;
                if constexpr (!B::IS_STUB)
                    r = ob::DiffFeed<B>(feed.data(), feed.count(),
                                        every, depth);
            });
            if (!known) {
                std::cerr << "unknown book " << n
                          << " (known: " << ob::KnownBooks() << ")\n";
                return 1;
            }
            ob::u64 t1 = ob::MonotonicNs();
            std::cout << n << ": ";
            if (r.ok)
                std::cout << "OK, " << r.updates << " updates, "
                          << r.full_compares << " full compares, "
                          << static_cast<double>(t1 - t0) / 1e9
                          << " s\n";
            else {
                std::cout << "FAIL at update " << r.failed_at << ": "
                          << r.failure << '\n';
                rc = 1;
            }
        }
        return rc;
    } catch (const std::exception& e) {
        std::cerr << "error: " << e.what() << '\n';
        return 1;
    }
}
