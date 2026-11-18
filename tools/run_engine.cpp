// run_engine EVENTS.bin price-time|pro-rata [TRADES.bin]

#include "../engine.hpp"
#include "../event_file.hpp"

#include <chrono>
#include <cstdio>
#include <string>
#include <vector>

int main(int argc, char** argv) {
    if (argc < 3) {
        std::fprintf(stderr, "usage: run_engine EVENTS.bin price-time|pro-rata [TRADES.bin]\n");
        return 2;
    }

    const std::string rule_name = argv[2];
    if (rule_name != "price-time" && rule_name != "pro-rata") {
        std::fprintf(stderr, "rule must be price-time or pro-rata\n");
        return 2;
    }

    const ob::MatchRule rule = rule_name == "pro-rata" ? ob::MatchRule::PRO_RATA : ob::MatchRule::PRICE_TIME;

    const std::vector<ob::Event> events = ob::load_events(argv[1]);
    ob::Engine engine(rule);
    std::vector<ob::Trade> trades;
    trades.reserve(events.size() / 4);

    const auto start = std::chrono::steady_clock::now();
    for (const ob::Event& e : events) engine.apply(e, trades);
    const double secs = std::chrono::duration<double>(std::chrono::steady_clock::now() - start).count();

    std::printf("%zu events, %zu trades, %.3f s, %.2f M events/s\n", events.size(), trades.size(), secs, events.size() / secs / 1e6);

    if (argc > 3) {
        FILE* out = std::fopen(argv[3], "wb");
        if (!out) { std::fprintf(stderr, "cannot create %s\n", argv[3]); return 1; }
        std::fwrite(trades.data(), sizeof(ob::Trade), trades.size(), out);
        std::fclose(out);
    }

    return 0;
}