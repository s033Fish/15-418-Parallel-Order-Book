// check_engine: verifies every registered engine against the sequential reference and
// benchmarks it across thread counts.
//
//   check_engine (--events FILE.bin | --random N [--stocks S] [--seed X])
//                [--rule price-time|pro-rata|both] [--engines a,b,...] [--threads 1,2,4,...]
//                [--reps R] [--warmup W] [--check-reps K] [--csv FILE] [--no-bench]

#include "bench.hpp"
#include "compare.hpp"
#include "engines.hpp"
#include "event_gen.hpp"
#include "../../event_file.hpp"

#include <unistd.h>

#include <algorithm>
#include <cstdio>
#include <cstdlib>
#include <exception>
#include <sstream>
#include <string>
#include <thread>
#include <vector>

using namespace ob;
using namespace ob::check;

namespace {
    struct Options {
        std::string events_path;
        GenConfig gen;
        bool random = false;
        std::vector<MatchRule> rules{MatchRule::PRICE_TIME};
        std::vector<std::string> engines;
        std::vector<int> threads;
        int reps = 5, warmup = 1, check_reps = 3;
        std::string csv;
        bool bench = true;
    };

    struct Row {
        MatchRule rule;
        std::string engine;
        int threads;
        bool has_stats = false;
        Stats stats;
        double speedup = 0, efficiency = 0;
        std::string check;
        uint64_t trades = 0;
    };

    const char* rule_name(MatchRule r) { return r == MatchRule::PRO_RATA ? "pro-rata" : "price-time"; }

    [[noreturn]] void usage(const char* msg = nullptr) {
        if (msg) std::fprintf(stderr, "check_engine: %s\n", msg);
        std::fprintf(stderr,
            "usage: check_engine (--events FILE.bin | --random N [--stocks S] [--seed X])\n"
            "                    [--rule price-time|pro-rata|both] [--engines a,b,...]\n"
            "                    [--threads 1,2,4,...] [--reps R] [--warmup W] [--check-reps K]\n"
            "                    [--csv FILE] [--no-bench]\n"
            "registered engines:");
        for (const EngineEntry& e : registry()) std::fprintf(stderr, " %s", e.name.c_str());
        std::fprintf(stderr, "\n");
        std::exit(2);
    }

    std::vector<std::string> split(const std::string& s) {
        std::vector<std::string> out;
        std::stringstream ss(s);
        for (std::string item; std::getline(ss, item, ',');) if (!item.empty()) out.push_back(item);
        return out;
    }

    Options parse(int argc, char** argv) {
        Options o;
        for (int i = 1; i < argc; ++i) {
            const std::string a = argv[i];
            auto val = [&]() -> std::string {
                if (i + 1 >= argc) usage(("missing value for " + a).c_str());
                return argv[++i];
            };
            if (a == "--events") o.events_path = val();
            else if (a == "--random") { o.random = true; o.gen.events = std::stoull(val()); }
            else if (a == "--stocks") o.gen.stocks = std::stoi(val());
            else if (a == "--seed") o.gen.seed = std::stoull(val());
            else if (a == "--rule") {
                const std::string r = val();
                if (r == "price-time") o.rules = {MatchRule::PRICE_TIME};
                else if (r == "pro-rata") o.rules = {MatchRule::PRO_RATA};
                else if (r == "both") o.rules = {MatchRule::PRICE_TIME, MatchRule::PRO_RATA};
                else usage("rule must be price-time, pro-rata or both");
            }
            else if (a == "--engines") o.engines = split(val());
            else if (a == "--threads") { o.threads.clear(); for (const auto& t : split(val())) o.threads.push_back(std::stoi(t)); }
            else if (a == "--reps") o.reps = std::stoi(val());
            else if (a == "--warmup") o.warmup = std::stoi(val());
            else if (a == "--check-reps") o.check_reps = std::stoi(val());
            else if (a == "--csv") o.csv = val();
            else if (a == "--no-bench") o.bench = false;
            else if (a == "-h" || a == "--help") usage();
            else usage(("unknown option " + a).c_str());
        }

        if (o.random == !o.events_path.empty()) usage("give exactly one of --events or --random");
        if (o.gen.stocks < 1 || o.gen.stocks > 65535) usage("--stocks must be 1..65535");
        if (o.reps < 1 || o.warmup < 0 || o.check_reps < 1) usage("bad --reps/--warmup/--check-reps");

        if (o.engines.empty()) for (const EngineEntry& e : registry()) o.engines.push_back(e.name);
        for (const auto& name : o.engines) make_engine(name, MatchRule::PRICE_TIME); // validate names

        if (o.threads.empty()) {
            const int hw = int(std::max(1u, std::thread::hardware_concurrency()));
            for (int t = 1; t < hw; t *= 2) o.threads.push_back(t);
            o.threads.push_back(hw);
        }
        for (int t : o.threads) if (t < 1) usage("thread counts must be >= 1");
        return o;
    }

    std::string hostname() {
        char buf[256] = {};
        return gethostname(buf, sizeof buf - 1) == 0 ? buf : "unknown";
    }

    void print_table(const std::vector<Row>& rows, size_t n_events) {
        std::printf("\n%-10s  %-14s %4s  %10s %10s %10s  %8s  %7s %6s  %10s  %s\n",
                    "rule", "engine", "thr", "median_s", "min_s", "stddev_s", "Mev/s", "speedup", "eff", "trades", "check");
        for (const Row& r : rows) {
            std::printf("%-10s  %-14s %4d  ", rule_name(r.rule), r.engine.c_str(), r.threads);
            if (r.has_stats) {
                std::printf("%10.4f %10.4f %10.4f  %8.2f  %7.2f %6.2f  ", r.stats.median, r.stats.min, r.stats.stddev,
                            n_events / r.stats.median / 1e6, r.speedup, r.efficiency);
            } else {
                std::printf("%10s %10s %10s  %8s  %7s %6s  ", "-", "-", "-", "-", "-", "-");
            }
            std::printf("%10llu  %s\n", (unsigned long long)r.trades, r.check.c_str());
        }
    }
}

int main(int argc, char** argv) {
    Options opt;
    std::vector<Event> events;
    try {
        opt = parse(argc, argv);
        events = opt.random ? generate_events(opt.gen) : load_events(opt.events_path);
    } catch (const std::exception& e) {
        std::fprintf(stderr, "check_engine: %s\n", e.what());
        return 2;
    }
    if (events.empty()) { std::fprintf(stderr, "check_engine: no events\n"); return 2; }

    // Input characteristics (stock skew drives per-stock load imbalance)
    std::vector<uint64_t> per_stock(65536, 0);
    for (const Event& e : events) ++per_stock[e.stock];
    std::vector<uint16_t> stocks;
    uint64_t top = 0;
    for (size_t s = 0; s < per_stock.size(); ++s) {
        if (per_stock[s]) stocks.push_back(uint16_t(s));
        top = std::max(top, per_stock[s]);
    }

    const std::string input = opt.random
        ? "random(n=" + std::to_string(opt.gen.events) + ",stocks=" + std::to_string(opt.gen.stocks) + ",seed=" + std::to_string(opt.gen.seed) + ")"
        : opt.events_path;
#ifdef NDEBUG
    const char* build = "optimized (NDEBUG)";
#else
    const char* build = "debug (asserts on)";
#endif

    std::printf("input:     %s\n", input.c_str());
    std::printf("events:    %zu across %zu stocks (busiest stock = %.1f%% of events)\n",
                events.size(), stocks.size(), 100.0 * double(top) / double(events.size()));
    std::printf("host:      %s, %u hardware threads\n", hostname().c_str(), std::thread::hardware_concurrency());
    std::printf("build:     %s, compiler %s\n", build, __VERSION__);
    std::printf("bench:     %s\n", opt.bench ? ("warmup " + std::to_string(opt.warmup) + ", reps " + std::to_string(opt.reps) +
                                                ", fresh engine per rep, timing run() only").c_str() : "disabled");

    std::vector<Row> rows;
    bool any_fail = false;

    for (MatchRule rule : opt.rules) {
        std::printf("\n== %s ==\n", rule_name(rule));

        // Reference output
        std::vector<Trade> ref_trades;
        std::map<uint16_t, BookState> ref_books;
        {
            auto ref = make_engine("seq", rule);
            ref->prepare(events);
            ref_trades.reserve(events.size() / 4);
            ref->run(events, 1, ref_trades);
            ref_books = capture_books(*ref, stocks);
        }
        std::printf("reference: %zu trades\n", ref_trades.size());

        Row seq_row{rule, "seq", 1};
        seq_row.check = "ref";
        seq_row.trades = ref_trades.size();
        double seq_median = 0;
        if (opt.bench) {
            seq_row.stats = benchmark([&] { return make_engine("seq", rule); }, events, 1, opt.warmup, opt.reps);
            seq_row.has_stats = true;
            seq_row.speedup = seq_row.efficiency = 1.0;
            seq_median = seq_row.stats.median;
            std::printf("[bench] seq: median %.4f s\n", seq_median);
        }
        rows.push_back(seq_row);

        for (const std::string& name : opt.engines) {
            if (name == "seq") continue;
            const bool par = make_engine(name, rule)->parallel();
            const std::vector<int> thread_list = par ? opt.threads : std::vector<int>{1};

            for (int t : thread_list) {
                Row row{rule, name, t};
                row.check = "OK";

                for (int c = 0; c < opt.check_reps; ++c) {
                    auto eng = make_engine(name, rule);
                    eng->prepare(events);
                    std::vector<Trade> out;
                    out.reserve(events.size() / 4);
                    eng->run(events, t, out);
                    row.trades = out.size();

                    const CompareResult tr = compare_trades(ref_trades, out);
                    const CompareResult br = compare_books(ref_books, *eng);
                    if (!tr.ok || !br.ok) {
                        std::printf("[FAIL] %s threads=%d (check run %d/%d)\n", name.c_str(), t, c + 1, opt.check_reps);
                        print_result("trade", tr);
                        print_result("book", br);
                        row.check = "FAIL";
                        any_fail = true;
                        break;
                    }
                }
                if (row.check == "OK") std::printf("[ok]   %s threads=%d (%d check runs)\n", name.c_str(), t, opt.check_reps);

                if (opt.bench && row.check == "OK") {
                    row.stats = benchmark([&] { return make_engine(name, rule); }, events, t, opt.warmup, opt.reps);
                    row.has_stats = true;
                    row.speedup = seq_median / row.stats.median;
                    row.efficiency = row.speedup / t;
                    std::printf("[bench] %s threads=%d: median %.4f s, speedup %.2fx\n", name.c_str(), t, row.stats.median, row.speedup);
                }
                rows.push_back(row);
            }
        }
    }

    print_table(rows, events.size());

    if (!opt.csv.empty()) {
        FILE* f = std::fopen(opt.csv.c_str(), "w");
        if (!f) { std::fprintf(stderr, "cannot create %s\n", opt.csv.c_str()); return 1; }
        std::fprintf(f, "host,input,events,stocks,rule,engine,threads,warmup,reps,median_s,min_s,mean_s,stddev_s,mevents_per_s,speedup,efficiency,trades,check,samples_s\n");
        const std::string host = hostname();
        for (const Row& r : rows) {
            std::fprintf(f, "%s,\"%s\",%zu,%zu,%s,%s,%d,%d,%d,", host.c_str(), input.c_str(), events.size(), stocks.size(),
                         rule_name(r.rule), r.engine.c_str(), r.threads, opt.warmup, opt.reps);
            if (r.has_stats) {
                std::fprintf(f, "%.6f,%.6f,%.6f,%.6f,%.4f,%.4f,%.4f,", r.stats.median, r.stats.min, r.stats.mean, r.stats.stddev,
                             events.size() / r.stats.median / 1e6, r.speedup, r.efficiency);
            } else {
                std::fprintf(f, ",,,,,,,");
            }
            std::fprintf(f, "%llu,%s,\"", (unsigned long long)r.trades, r.check.c_str());
            for (size_t i = 0; i < r.stats.samples.size(); ++i) std::fprintf(f, "%s%.6f", i ? ";" : "", r.stats.samples[i]);
            std::fprintf(f, "\"\n");
        }
        std::fclose(f);
        std::printf("\nwrote %s\n", opt.csv.c_str());
    }

    if (any_fail) { std::printf("\nRESULT: FAIL\n"); return 1; }
    std::printf("\nRESULT: OK\n");
    return 0;
}
