#include "ob/Bench.hpp"
#include "ob/Feed.hpp"
#include "ob/Itch.hpp"

#include <cstdio>
#include <cstdlib>
#include <iomanip>
#include <iostream>
#include <sstream>
#include <string>
#include <vector>
#include <zlib.h>

namespace ob::itch {

bool ReadItchFile(const std::string& path, Converter& converter,
                  std::string& error) {
    gzFile gz = gzopen(path.c_str(), "rb");
    if (!gz) {
        error = "cannot open " + path;
        return false;
    }
    gzbuffer(gz, 1 << 20);
    std::vector<u8> buf(1 << 22);
    std::size_t have = 0;
    bool eof = false;
    while (true) {
        if (!eof && have < buf.size()) {
            int n = gzread(gz, buf.data() + have,
                           static_cast<unsigned>(buf.size() - have));
            if (n < 0) {
                int err = 0;
                error = std::string("gzread: ") + gzerror(gz, &err);
                gzclose(gz);
                return false;
            }
            if (n == 0)
                eof = true;
            have += static_cast<std::size_t>(n);
        }
        std::size_t pos = 0;
        while (have - pos >= 2) {
            std::size_t len = BE16(buf.data() + pos);
            if (have - pos < 2 + len)
                break;
            converter.OnMessage(buf.data() + pos + 2, len);
            pos += 2 + len;
        }
        if (pos > 0) {
            std::memmove(buf.data(), buf.data() + pos, have - pos);
            have -= pos;
        }
        if (eof && have < 2)
            break;
        if (eof && have >= 2 && have - 0 < 2 + BE16(buf.data())) {
            error = "truncated message at end of file";
            gzclose(gz);
            return false;
        }
    }
    gzclose(gz);
    return true;
}

}

#ifndef OB_ITCH_NO_MAIN

namespace {

void Usage() {
    std::cerr
        << "usage: ob_itch --in FILE.gz --symbols A,B,C [--out-dir "
           "DIR]\n"
           "               [--from HH:MM] [--to HH:MM] [--date "
           "LABEL]\n";
}

std::vector<std::string> Split(const std::string& s) {
    std::vector<std::string> out;
    std::string cur;
    for (char c : s) {
        if (c == ',') {
            if (!cur.empty())
                out.push_back(cur);
            cur.clear();
        } else {
            cur += c;
        }
    }
    if (!cur.empty())
        out.push_back(cur);
    return out;
}

std::string ClockOf(ob::u64 ns) {
    ob::u64 s = ns / 1'000'000'000ull;
    std::ostringstream os;
    os << std::setfill('0') << std::setw(2) << s / 3600 << ':'
       << std::setw(2) << (s / 60) % 60 << ':' << std::setw(2)
       << s % 60;
    return os.str();
}

std::string DateLabel(const std::string& path) {
    auto slash = path.find_last_of('/');
    std::string base =
        slash == std::string::npos ? path : path.substr(slash + 1);
    auto dot = base.find('.');
    return dot == std::string::npos ? base : base.substr(0, dot);
}

}

int main(int argc, char** argv) {
    std::string in;
    std::string symbols;
    std::string out_dir = "results";
    std::string from;
    std::string to;
    std::string date;
    for (int i = 1; i < argc; ++i) {
        std::string a = argv[i];
        auto next = [&](const char* name) -> std::string {
            if (i + 1 >= argc) {
                std::cerr << name << " needs a value\n";
                std::exit(1);
            }
            return argv[++i];
        };
        if (a == "--in")
            in = next("--in");
        else if (a == "--symbols")
            symbols = next("--symbols");
        else if (a == "--out-dir")
            out_dir = next("--out-dir");
        else if (a == "--from")
            from = next("--from");
        else if (a == "--to")
            to = next("--to");
        else if (a == "--date")
            date = next("--date");
        else if (a == "--help" || a == "-h") {
            Usage();
            return 0;
        } else {
            std::cerr << "unknown argument " << a << '\n';
            Usage();
            return 1;
        }
    }
    if (in.empty() || symbols.empty()) {
        Usage();
        return 1;
    }
    if (date.empty())
        date = DateLabel(in);
    ob::u64 from_ns = from.empty() ? 0 : ob::itch::ParseClockNs(from);
    ob::u64 to_ns = to.empty() ? std::numeric_limits<ob::u64>::max()
                              : ob::itch::ParseClockNs(to);

    ob::itch::Converter conv(Split(symbols), from_ns, to_ns);
    ob::u64 t0 = ob::MonotonicNs();
    std::string err;
    if (!ob::itch::ReadItchFile(in, conv, err)) {
        std::cerr << "error: " << err << '\n';
        return 1;
    }
    ob::u64 t1 = ob::MonotonicNs();

    const ob::itch::Stats& st = conv.GetStats();
    std::cout << "file:              " << in << '\n'
              << "messages:          " << st.messages << " ("
              << st.bytes / (1024 * 1024)
              << " MiB of message bytes) in "
              << static_cast<double>(t1 - t0) / 1e9 << " s\n"
              << "time range:        " << ClockOf(st.first_ts)
              << " to " << ClockOf(st.last_ts) << '\n'
              << "symbols in directory: " << st.symbols_in_directory
              << '\n'
              << "window:            "
              << (from.empty() ? std::string("whole day") : from)
              << " to " << (to.empty() ? std::string("end") : to)
              << '\n';
    const char* known = "SRHYLVWKJhAFECXDUPQBIN";
    std::cout << "by type:          ";
    for (const char* c = known; *c; ++c)
        std::cout << ' ' << *c << '='
                  << st.by_type[static_cast<ob::u8>(*c)];
    std::cout << '\n';

    for (ob::itch::SymbolState& s : conv.States()) {
        ob::FeedConfig cfg{};
        cfg.seed = 0;
        cfg.update_count = s.updates.size();
        cfg.levels_per_side = 0;
        cfg.max_depth = 0;
        cfg.mid_price = 0;
        cfg.orders_per_level_seed = 0;
        cfg.depth_exponent = 0;
        cfg.new_level_rate = 0;
        cfg.add_pct = 0;
        cfg.modify_pct = 0;
        cfg.delete_pct = 0;
        cfg.max_volume = 0;
        std::string source = "itch:" + date + ":" + s.symbol;
        std::string path =
            out_dir + "/feed_itch_" + s.symbol + ".bin";
        ob::u64 adds = 0, mods = 0, dels = 0;
        for (const ob::Update& u : s.updates) {
            if (u.type == ob::MsgType::Add)
                ++adds;
            else if (u.type == ob::MsgType::Modify)
                ++mods;
            else
                ++dels;
        }
        try {
            ob::WriteFeed(path, cfg, s.updates, source);
        } catch (const std::exception& e) {
            std::cerr << "error: " << e.what() << '\n';
            return 1;
        }
        std::cout << s.symbol << ": " << s.updates.size()
                  << " updates (add=" << adds << " modify=" << mods
                  << " delete=" << dels
                  << ") unknown_ids=" << s.unknown_ids
                  << " dropped=" << s.dropped
                  << " live_high_water=" << s.live_high_water
                  << " live_at_end=" << s.orders.size() << " -> "
                  << path << '\n';
    }
    return 0;
}

#endif
