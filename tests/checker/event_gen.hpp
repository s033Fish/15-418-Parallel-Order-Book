#pragma once

// Seeded random event streams for stress-testing parallel engines without ITCH data.
// Stocks are Zipf-skewed (a few hot stocks, like the real market) and prices cluster near
// a drifting mid, so activity concentrates at the top of the book.

#include "../../event.hpp"

#include <algorithm>
#include <cmath>
#include <cstdint>
#include <random>
#include <vector>

namespace ob::check {
    struct GenConfig {
        uint64_t events = 1'000'000;
        int stocks = 50;        // stock ids 1..stocks
        uint64_t seed = 1;
        double zipf = 1.1;      // larger -> more skewed toward stock 1
    };

    inline std::vector<Event> generate_events(const GenConfig& cfg) {
        constexpr Price tick = 100;        // $0.01
        constexpr size_t max_live = 5000;  // per stock, keeps cancel targets bounded

        std::mt19937_64 rng(cfg.seed);
        std::uniform_real_distribution<double> unit(0.0, 1.0);
        std::geometric_distribution<int> depth(0.35);

        std::vector<double> weights(cfg.stocks);
        for (int i = 0; i < cfg.stocks; ++i) weights[i] = 1.0 / std::pow(i + 1, cfg.zipf);
        std::discrete_distribution<int> pick_stock(weights.begin(), weights.end());

        struct Live { uint64_t id; Price price; Side side; };
        struct StockState { Price mid = 100 * 10000; std::vector<Live> live; };
        std::vector<StockState> st(cfg.stocks);

        auto qty = [&]() -> uint32_t {
            static constexpr uint32_t lots[] = {100, 200, 300, 500, 1000};
            return unit(rng) < 0.5 ? lots[rng() % 5] : uint32_t(1 + rng() % 999);
        };

        std::vector<Event> out;
        out.reserve(cfg.events);
        uint64_t ts = 1, next_id = 1;

        while (out.size() < cfg.events) {
            const int si = pick_stock(rng);
            StockState& s = st[si];
            const uint16_t stock = uint16_t(si + 1);

            if (unit(rng) < 0.9) ts += 1 + rng() % 1000; // ~10% share a timestamp
            if (unit(rng) < 0.05) s.mid = unit(rng) < 0.5 ? s.mid + tick : std::max<Price>(s.mid - tick, 10 * tick);

            const double r = unit(rng);
            const bool must_delete = s.live.size() >= max_live;

            if (!must_delete && r < 0.50) { // passive add, 1+ ticks away from mid
                const Side side = unit(rng) < 0.5 ? Side::BUY : Side::SELL;
                const Price off = tick * Price(1 + depth(rng));
                const Price px = side == Side::BUY ? s.mid - std::min(off, s.mid - tick) : s.mid + off;
                const uint64_t id = next_id++;
                out.push_back({ts, id, px, qty(), stock, EventType::ADD, side});
                s.live.push_back({id, px, side});
            } else if (!must_delete && r < 0.65) { // aggressive order through the mid
                const Side side = unit(rng) < 0.5 ? Side::BUY : Side::SELL;
                const Price off = tick * Price(depth(rng));
                const Price px = side == Side::BUY ? s.mid + off : s.mid - std::min(off, s.mid - tick);
                const uint64_t id = next_id++;
                out.push_back({ts, id, px, qty() * 3, stock, EventType::INCOMING, side});
                s.live.push_back({id, px, side}); // remainder may rest
            } else if (s.live.empty()) {
                continue;
            } else if (!must_delete && r < 0.85) { // partial cancel
                const Live& o = s.live[rng() % s.live.size()];
                out.push_back({ts, o.id, o.price, uint32_t(1 + rng() % 200), stock, EventType::CANCEL, o.side});
            } else { // delete (orders already filled by the engine are simply ignored)
                const size_t k = rng() % s.live.size();
                const Live o = s.live[k];
                s.live[k] = s.live.back();
                s.live.pop_back();
                out.push_back({ts, o.id, o.price, 0, stock, EventType::DELETE, o.side});
            }
        }
        return out;
    }
}
