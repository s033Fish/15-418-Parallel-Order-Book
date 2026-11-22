// Self-test for the checker: it must accept identical output (including trades of different
// stocks reordered) and reject corrupted trades or books.

#include "compare.hpp"
#include "engines.hpp"
#include "event_gen.hpp"

#include <cstdio>
#include <utility>
#include <vector>

using namespace ob;
using namespace ob::check;

static int passed = 0, failed = 0;
static void expect(const char* name, bool ok) {
    if (ok) ++passed;
    else {
        ++failed;
        std::printf("FAIL %s\n", name);
    }
}

static std::vector<uint16_t> stocks_of(const std::vector<Event>& events) {
    std::vector<bool> seen(65536, false);
    std::vector<uint16_t> out;
    for (const Event& e : events) if (!seen[e.stock]) { seen[e.stock] = true; out.push_back(e.stock); }
    return out;
}

static void run_rule(MatchRule rule, const char* label) {
    GenConfig cfg;
    cfg.events = 50'000;
    cfg.stocks = 6;
    cfg.seed = 7;
    const std::vector<Event> events = generate_events(cfg);
    const std::vector<uint16_t> stocks = stocks_of(events);

    auto ref = make_engine("seq", rule);
    std::vector<Trade> ref_trades;
    ref->run(events, 1, ref_trades);
    const auto ref_books = capture_books(*ref, stocks);

    std::printf("%s: %zu events, %zu trades\n", label, events.size(), ref_trades.size());
    expect("generator produces trades", ref_trades.size() > 100);

    // Deterministic: a second run is identical
    auto again = make_engine("seq", rule);
    std::vector<Trade> again_trades;
    again->run(events, 1, again_trades);
    expect("identical run passes trades", compare_trades(ref_trades, again_trades).ok);
    expect("identical run passes books", compare_books(ref_books, *again).ok);

    // Find two adjacent trades of the same stock that differ, and two adjacent of different stocks
    size_t same_i = SIZE_MAX, cross_i = SIZE_MAX;
    for (size_t i = 0; i + 1 < ref_trades.size(); ++i) {
        const Trade &a = ref_trades[i], &b = ref_trades[i + 1];
        if (a.stock == b.stock && !same_trade(a, b) && same_i == SIZE_MAX) same_i = i;
        if (a.stock != b.stock && cross_i == SIZE_MAX) cross_i = i;
    }
    expect("found test positions", same_i != SIZE_MAX && cross_i != SIZE_MAX);
    if (same_i == SIZE_MAX || cross_i == SIZE_MAX) return;

    { auto t = ref_trades; t[t.size() / 2].qty += 1;
      expect("changed qty is caught", !compare_trades(ref_trades, t).ok); }

    { auto t = ref_trades; t.erase(t.begin() + t.size() / 2);
      expect("dropped trade is caught", !compare_trades(ref_trades, t).ok); }

    { auto t = ref_trades; t.push_back(t.front());
      expect("extra trade is caught", !compare_trades(ref_trades, t).ok); }

    { auto t = ref_trades; std::swap(t[same_i], t[same_i + 1]);
      expect("reorder within a stock is caught", !compare_trades(ref_trades, t).ok); }

    { auto t = ref_trades; std::swap(t[cross_i], t[cross_i + 1]);
      expect("reorder across stocks is allowed", compare_trades(ref_trades, t).ok); }

    // Book state: an engine that saw only half the events must differ
    auto half = make_engine("seq", rule);
    std::vector<Trade> half_trades;
    half->run(std::vector<Event>(events.begin(), events.begin() + events.size() / 2), 1, half_trades);
    expect("different final book is caught", !compare_books(ref_books, *half).ok);
}

int main() {
    run_rule(MatchRule::PRICE_TIME, "price-time");
    run_rule(MatchRule::PRO_RATA, "pro-rata");
    std::printf("%d passed, %d failed\n", passed, failed);
    return failed ? 1 : 0;
}
