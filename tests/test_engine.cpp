// Tests for the matching engine

#include "../engine.hpp"

#include <cstdio>
#include <map>
#include <vector>

using namespace ob;
using Fills = std::map<uint64_t, uint32_t>; // resting order id -> shares filled

static int passed = 0, failed = 0;
static void check(const char* name, bool ok) {
    if (ok) ++passed;
    else {
        ++failed;
        std::printf("FAIL %s\n", name);
    }
}

static Price P(double dollars) {
    return Price(dollars * 10000 + 0.5);
}

// Submit a limit order and return its trades
static std::vector<Trade> sub(OrderBook& b, uint64_t id, Side side, double px, uint32_t qty, uint64_t ts = 0) {
    std::vector<Trade> trades;
    b.submit({ts ? ts : id, id, P(px), qty, 0, EventType::ADD, side}, trades);
    return trades;
}

static Fills fills(const std::vector<Trade>& trades) {
    Fills f;
    for (const Trade& t : trades) {
        f[t.aggressor == Side::BUY ? t.sell_id : t.buy_id] += t.qty;
    }

    return f;
}

static OrderBook sell_level(MatchRule rule, std::vector<uint32_t> sizes, std::vector<uint64_t> ts) {
    OrderBook b(rule);
    for (size_t i = 0; i < sizes.size(); ++i) {
        sub(b, i+1, Side::SELL, 100, sizes[i], ts[i]);
    }

    return b;
}

int main() {
    const MatchRule PR = MatchRule::PRO_RATA, PT = MatchRule::PRICE_TIME;

    { OrderBook b(PR); sub(b, 1, Side::BUY, 100, 10); sub(b, 2, Side::BUY, 102, 20); sub(b, 3, Side::BUY, 101, 30); sub(b, 4, Side::BUY, 101, 5), sub(b, 5, Side::SELL, 105, 10); sub(b, 6, Side::SELL, 103, 10);
      check("prices best first", b.prices(Side::BUY) == std::vector<Price>{P(102), P(101), P(100)} && b.prices(Side::SELL) == std::vector<Price>{P(103), P(105)});
      check("level totals", b.total_at(Side::BUY, P(101)) == 35 && b.total_at(Side::SELL, P(104)) == 0 && b.total_at(Side::SELL, P(101)) == 0); 
    }

    { OrderBook b(PR); sub(b, 1, Side::SELL, 105, 50);
      check("non-crossing order rests", sub(b, 2, Side::BUY, 100, 50).empty() && b.total_at(Side::BUY, P(100)) == 50); 
    }

    { OrderBook b(PR); sub(b, 1, Side::SELL, 100, 50); auto t = sub(b, 2, Side::BUY, 100, 50);
      check("simple full match", t.size() == 1 && t[0].buy_id == 2 && t[0].sell_id == 1 && t[0].qty == 50 && b.prices(Side::SELL).empty() && b.prices(Side::BUY).empty());
    }

    { OrderBook b(PR); sub(b, 1, Side::SELL, 99, 10);
      check("trades at the resting price", sub(b, 2, Side::BUY, 101, 10)[0].price == P(99)); 
    }

    { OrderBook b(PR); sub(b, 1, Side::SELL, 100, 30); sub(b, 2, Side::BUY, 100, 100);
      check("partial fill rests the remainder", b.total_at(Side::BUY, P(100)) == 70 && b.prices(Side::SELL).empty());
    }

    { auto b = sell_level(PR, {30, 20}, {1, 2});
      check("taking the whole level clears it", fills(sub(b, 3, Side::BUY, 100, 50)) == Fills{{1, 30}, {2, 20}} && b.prices(Side::SELL).empty()); 
    }

    { auto b = sell_level(PR, {60, 30, 10}, {1, 2, 3});
      check("pro rata, no remainder", fills(sub(b, 9, Side::BUY, 100, 50)) == Fills{{1, 30}, {2, 15}, {3, 5}} && b.total_at(Side::SELL, P(100)) == 50);
    }

    { auto b = sell_level(PR, {33, 33, 34}, {1, 2, 3});
      check("remainder to the largest", fills(sub(b, 9, Side::BUY, 100, 95)) == Fills{{1, 31}, {2, 31}, {3, 33}});
    }

    { auto b = sell_level(PR, {30, 30, 30}, {3, 1, 2});
      check("remainder tie goes to the earliest timestamp", fills(sub(b, 9, Side::BUY, 100, 85)) == Fills{{1, 28}, {2, 29}, {3, 28}});
    }

    { auto b = sell_level(PR, {58, 142}, {1, 2});
      check("exact integer floor (58/200 of 100 = 29)", fills(sub(b, 9, Side::BUY, 100, 100)) == Fills{{1, 29}, {2, 71}});
    }

    { auto b = sell_level(PR, {1, 1, 98}, {1, 2, 3}); auto t = sub(b, 9, Side::BUY, 100, 2);
      check("small orders can get zero, no 0-share trades", fills(t) == Fills{{3, 2}} && t.size() == 1);
    }

    { OrderBook b(PR); sub(b, 1, Side::SELL, 98, 30); sub(b, 2, Side::SELL, 99, 40); sub(b, 3, Side::SELL, 100, 30);
      sub(b, 4, Side::SELL, 100, 20);
      check("sweep levels, then pro rata the last", fills(sub(b, 9, Side::BUY, 100, 100)) == Fills{{1, 30}, {2, 40}, {3, 18}, {4, 12}} && b.total_at(Side::SELL, P(100)) == 20 && b.prices(Side::BUY).empty());
    }

    { OrderBook b(PR); sub(b, 1, Side::BUY, 100, 40); sub(b, 2, Side::BUY, 100, 60);
      check("sell aggressor hits bids", fills(sub(b, 9, Side::SELL, 99, 25)) == Fills{{1, 10}, {2, 15}} && b.total_at(Side::BUY, P(100)) == 75);
    }

    { const uint32_t big = 2147483647u; auto b = sell_level(PR, {big, big}, {1, 2});
      uint64_t traded = 0;
      for (const Trade& t : sub(b, 9, Side::BUY, 100, big)) traded += t.qty;
      check("large quantities do not overflow", traded == big && b.total_at(Side::SELL, P(100)) == big);
    }

    { auto b = sell_level(PT, {30, 20}, {1, 2});
      check("price-time fills oldest first", fills(sub(b, 9, Side::BUY, 100, 40)) == Fills{{1, 30}, {2, 10}});
    }

    { auto b = sell_level(PT, {30, 20}, {1, 2});
      b.reduce(1, 10); b.reduce(2, UINT32_MAX); b.reduce(777, 5);
      check("cancel, delete, unknown id ignored", b.total_at(Side::SELL, P(100)) == 20);
      b.reduce(1, UINT32_MAX);
      check("removing the last order clears the level", b.prices(Side::SELL).empty());
    }
 
    std::printf("%d passed, %d failed\n", passed, failed);
    return failed ? 1 : 0;
}