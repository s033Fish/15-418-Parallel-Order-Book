#pragma once

// Common interface every engine variant (sequential, per-stock parallel, within-stock
// parallel, ...) is wrapped in so the checker and benchmark can drive them uniformly.

#include "../../sequential/sequential-engine.hpp"

#include <cstdint>
#include <string>
#include <vector>

namespace ob::check {
    // One price level of a book after the replay
    struct LevelState {
        Side side;
        Price price;
        uint64_t total;
        bool operator==(const LevelState&) const = default;
    };

    // All levels of one stock's book: bids best-first, then asks best-first
    using BookState = std::vector<LevelState>;

    class MatchingEngine {
        public:
            virtual ~MatchingEngine() = default;

            virtual std::string name() const = 0;

            // false -> the engine ignores the thread count and is only run with threads = 1
            virtual bool parallel() const { return true; }

            // Untimed setup (allocation, thread pool start-up, ...)
            virtual void prepare(const std::vector<Event>&) {}

            // Timed: replay every event and append all trades to out.
            // Trades of different stocks may be interleaved in any order, but the trades
            // of a single stock must come out in exactly the sequential order.
            virtual void run(const std::vector<Event>& events, int threads, std::vector<Trade>& out) = 0;

            // Final book of one stock, called after run()
            virtual BookState book_state(uint16_t stock) = 0;
    };

    // Snapshot helper for engines built on ob::OrderBook
    inline BookState snapshot(const OrderBook& book) {
        BookState out;
        for (Side side : {Side::BUY, Side::SELL}) {
            for (Price p : book.prices(side)) out.push_back({side, p, book.total_at(side, p)});
        }
        return out;
    }
}
