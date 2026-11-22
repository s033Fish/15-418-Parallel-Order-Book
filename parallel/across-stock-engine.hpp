#pragma once

#include "event.hpp"
#include "sequential/sequential-engine.hpp"

#include <algorithm>
#include <atomic>
#include <cstdint>
#include <functional>
#include <queue>
#include <thread>
#include <utility>
#include <vector>

namespace ob {
    // How stocks are handed out to threads
    enum class Schedule : uint8_t {
        STATIC, // Stocks dealt round-robin to threads, ignoring load
        DYNAMIC, // Threads grab the next unprocessed stock from a shared counter
        SEMI_STATIC, // Greedy load-balanced assignment, recomputed each epoch from the last epoch's counts
    };

    // Across-stock parallel engine. Books of different stocks share no state, so each stock
    // is matched by exactly one thread in arrival order, giving the same per-stock trades as the
    // sequential engine with no locks
    class AcrossStockEngine {
        public:
            AcrossStockEngine(MatchRule rule, Schedule schedule, size_t epoch_events = 1 << 18)
                : engine_(rule), schedule_(schedule), epoch_events_(std::max<size_t>(1, epoch_events)),
                  count_(kStocks, 0), offset_(kStocks + 1, 0), prev_count_(kStocks, 0) {}

            // Replay every event using up to threads threads and append the trades
            void run(const std::vector<Event>& events, int threads, std::vector<Trade>& trades) {
                const size_t t = size_t(std::max(1, threads));
                if (schedule_ != Schedule::SEMI_STATIC) {
                    run_epoch(events.data(), events.size(), t, trades);
                    return;
                }

                // Semi-static - cut the stream into epochs and rebalance between them
                for (size_t start = 0; start < events.size(); start += epoch_events_) {
                    run_epoch(events.data() + start, std::min(epoch_events_, events.size() - start), t, trades);
                }
            }

            OrderBook& book(uint16_t stock) {
                return engine_.book(stock);
            }

        private:
            static constexpr size_t kStocks = 65536;

            // One batch - partition by stock, assign stocks to threads, match, merge
            void run_epoch(const Event* events, size_t n, size_t threads, std::vector<Trade>& trades) {
                partition(events, n);

                std::vector<std::vector<uint16_t>> owned; // per-thread stock lists (static / semi-static)
                if (schedule_ == Schedule::STATIC) owned = assign_round_robin(threads);
                else if (schedule_ == Schedule::SEMI_STATIC) owned = assign_greedy(threads);

                std::vector<std::vector<Trade>> local(threads); // per-thread trades, merged after the join
                std::atomic<size_t> next{0}; // dynamic - index of the next unclaimed stock

                auto worker = [&](size_t id) {
                    if (schedule_ == Schedule::DYNAMIC) {
                        for (size_t k; (k = next.fetch_add(1, std::memory_order_relaxed)) < active_.size();) {
                            match_stock(active_[k], local[id]);
                        }
                    } else {
                        for (uint16_t s : owned[id]) match_stock(s, local[id]);
                    }
                };

                // Main thread works as thread 0
                std::vector<std::thread> pool;
                for (size_t id = 1; id < threads; ++id) pool.emplace_back(worker, id);
                worker(0);
                for (std::thread& th : pool) th.join();

                // Each stock lives in one thread's vector, so its trades stay in order
                for (const std::vector<Trade>& l : local) trades.insert(trades.end(), l.begin(), l.end());

                if (schedule_ == Schedule::SEMI_STATIC) remember_counts();
            }

            // Counting sort of the batch by stock. Keeps each stock's events in arrival order
            void partition(const Event* events, size_t n) {
                std::fill(count_.begin(), count_.end(), 0);
                for (size_t i = 0; i < n; ++i) ++count_[events[i].stock];

                active_.clear();
                offset_[0] = 0;
                for (size_t s = 0; s < kStocks; ++s) {
                    offset_[s + 1] = offset_[s] + count_[s];
                    if (count_[s]) active_.push_back(uint16_t(s));
                }

                sorted_.resize(n);
                std::vector<size_t> cursor(offset_.begin(), offset_.end() - 1);
                for (size_t i = 0; i < n; ++i) sorted_[cursor[events[i].stock]++] = events[i];
            }

            // Replay one stock's events for this batch
            void match_stock(uint16_t stock, std::vector<Trade>& out) {
                for (size_t i = offset_[stock]; i < offset_[stock + 1]; ++i) engine_.apply(sorted_[i], out);
            }

            // Static - k-th active stock goes to thread k % threads
            std::vector<std::vector<uint16_t>> assign_round_robin(size_t threads) const {
                std::vector<std::vector<uint16_t>> owned(threads);
                for (size_t k = 0; k < active_.size(); ++k) owned[k % threads].push_back(active_[k]);
                return owned;
            }

            // Semi-static - heaviest stock first onto the least-loaded thread. Cost estimate is the
            // stock's event count in the previous epoch [this epoch's count if it was absent]
            std::vector<std::vector<uint16_t>> assign_greedy(size_t threads) const {
                auto cost = [&](uint16_t s) -> uint64_t { return prev_count_[s] ? prev_count_[s] : count_[s]; };

                std::vector<uint16_t> order = active_;
                std::sort(order.begin(), order.end(), [&](uint16_t a, uint16_t b) { return cost(a) > cost(b); });

                using Load = std::pair<uint64_t, size_t>; // (estimated events, thread)
                std::priority_queue<Load, std::vector<Load>, std::greater<Load>> least;
                for (size_t id = 0; id < threads; ++id) least.push({0, id});

                std::vector<std::vector<uint16_t>> owned(threads);
                for (uint16_t s : order) {
                    auto [load, id] = least.top();
                    least.pop();
                    owned[id].push_back(s);
                    least.push({load + cost(s), id});
                }
                return owned;
            }

            // Semi-static - this epoch's counts become the next epoch's estimates
            void remember_counts() {
                for (uint16_t s : prev_active_) prev_count_[s] = 0;
                for (uint16_t s : active_) prev_count_[s] = count_[s];
                prev_active_ = active_;
            }

            Engine engine_; // one book per stock, each touched by a single thread per epoch
            Schedule schedule_;
            size_t epoch_events_; // semi-static batch size

            std::vector<size_t> count_; // events per stock in this batch
            std::vector<size_t> offset_; // stock s's events are sorted_[offset_[s], offset_[s+1])
            std::vector<Event> sorted_; // batch events grouped by stock
            std::vector<uint16_t> active_; // stocks with events in this batch, by id

            std::vector<size_t> prev_count_; // semi-static - last epoch's events per stock
            std::vector<uint16_t> prev_active_;
    };
}
