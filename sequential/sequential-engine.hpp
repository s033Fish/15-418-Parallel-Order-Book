#pragma once

#include "event.hpp"

#include <algorithm>
#include <cstdint>
#include <iterator>
#include <list>
#include <map>
#include <unordered_map>
#include <vector>

namespace ob {
    enum class MatchRule : uint8_t {
        PRICE_TIME,
        PRO_RATA
    };

    struct Trade {
        uint64_t buy_id;
        uint64_t sell_id;
        Price price; // Resting order's price
        uint32_t qty;
        uint16_t stock;
        Side aggressor;
    };

    class OrderBook {
        public:
            explicit OrderBook(MatchRule rule = MatchRule::PRICE_TIME) : rule_(rule) {}

            // Limit order. Match against order side and rest the remainder
            void submit(const Event& e, std::vector<Trade>& trades) {
                uint32_t qty = e.qty;
                if (e.side == Side::BUY) {
                    match(asks_, e, qty, trades, [&](Price p) { return p <= e.price; });
                }
                else {
                    match(bids_, e, qty, trades, [&](Price p) { return p >= e.price; });
                }

                if (qty > 0) {
                    rest(e, qty);
                }
            }

            // Remove up to qty shares from an order
            void reduce(uint64_t id, uint32_t qty) {
                auto found = index_.find(id);
                if (found == index_.end()) return;

                const Loc loc = found->second;
                if (loc.side == Side::BUY) reduce_at(bids_, loc, qty);
                else reduce_at(asks_, loc, qty);
            }

            uint64_t total_at(Side side, Price price) const {
                if (side == Side::BUY) {
                    auto it = bids_.find(price);
                    return it == bids_.end() ? 0 : it->second.total;
                }

                auto it = asks_.find(price);
                return it == asks_.end() ? 0 : it->second.total;
            }

            // Price levels on one side
            std::vector<Price> prices(Side side) const {
                std::vector<Price> out;
                if (side == Side::BUY) for (const auto& [p, lvl] : bids_) out.push_back(p);
                else for (const auto& [p, lvl] : asks_) out.push_back(p);

                return out;
            }

        private:
            struct Resting { uint64_t id; uint32_t qty; uint64_t ts; uint64_t seq; };
            struct Level { std::list<Resting> orders; uint64_t total = 0; };
            struct Loc { Side side; Price price; std::list<Resting>::iterator it; };

            // pro-rata priority - largest first, then earliest
            static bool pro_rata_first(const Resting& a, const Resting& b) {
                if (a.qty != b.qty) return a.qty > b.qty;
                return a.ts != b.ts ? a.ts < b.ts : a.seq < b.seq;
            }

            template <class Book, class Crosses>
            void match(Book& book, const Event& in, uint32_t& qty, std::vector<Trade>& trades, Crosses crosses) {
                while (qty > 0 && !book.empty() && crosses(book.begin()->first)) {
                    auto lvl = book.begin();
                    if (rule_ == MatchRule::PRO_RATA && qty < lvl->second.total) fill_pro_rata(lvl->second, lvl->first, in, qty, trades);
                    else fill_in_order(lvl->second, lvl->first, in, qty, trades);

                    if(lvl->second.orders.empty()) book.erase(lvl);
                }
            }

            // price-time - oldest first [pro-rata when the incoming order takes the whole level]
            void fill_in_order(Level& level, Price price, const Event& in, uint32_t& qty, std::vector<Trade>& trades) {
                if (rule_ == MatchRule::PRO_RATA) level.orders.sort(pro_rata_first);
                while (qty > 0 && !level.orders.empty()) {
                    Resting& r = level.orders.front();
                    const uint32_t f = std::min(qty, r.qty);
                    trade(in, r.id, price, f, trades);
                    qty -= f; r.qty -= f; level.total -= f;
                    if (r.qty == 0) {
                        index_.erase(r.id); level.orders.pop_front();
                    }
                }
            }

            // Each order gets floor(qty * size / total). Leftover gets one each in priority order
            void fill_pro_rata(Level& level, Price price, const Event& in, uint32_t& qty, std::vector<Trade>& trades) {
                level.orders.sort(pro_rata_first);
                std::vector<uint32_t> fill;
                uint64_t allocated = 0;
                for (const Resting& r : level.orders) {
                    fill.push_back(uint32_t(uint64_t(qty) * r.qty / level.total));
                    allocated += fill.back();
                }

                for (uint64_t i = 0; i < qty - allocated; ++i) {
                    ++fill[i];
                }

                size_t i = 0;

                for (auto it = level.orders.begin(); it != level.orders.end(); ++i) {
                    if (fill[i] > 0) {
                        trade(in, it->id, price, fill[i], trades);
                        it->qty -= fill[i];
                    }

                    if (it->qty == 0) {
                        index_.erase(it->id); it = level.orders.erase(it);
                    } else {
                        ++it;
                    }

                    level.total -= qty;
                    qty = 0;
                }
            }  

            void rest(const Event& e, uint32_t qty) {
                Level& level = e.side == Side::BUY ? bids_[e.price] : asks_[e.price];
                level.orders.push_back({e.order_id, qty, e.ts_ns, seq_++});
                level.total += qty;
                index_[e.order_id] = {e.side, e.price, std::prev(level.orders.end())};
            }

            template <class Book>
            void reduce_at(Book& book, const Loc& loc, uint32_t qty) {
                auto lvl = book.find(loc.price);
                const uint32_t q = std::min(qty, loc.it->qty);
                loc.it->qty -= q;
                lvl->second.total -= q;
                if (loc.it->qty == 0) {
                    index_.erase(loc.it->id);
                    lvl->second.orders.erase(loc.it);
                    if (lvl->second.orders.empty()) book.erase(lvl);
                }
            }

            static void trade(const Event& in, uint64_t resting, Price price, uint32_t q, std::vector<Trade>& out) {
                if (in.side == Side::BUY) {
                    out.push_back({in.order_id, resting, price, q, in.stock, Side::BUY});
                } else {
                    out.push_back({resting, in.order_id, price, q, in.stock, Side::SELL});
                }
            }

            MatchRule rule_;
            std::map<Price, Level, std::greater<Price>> bids_; // highest first
            std::map<Price, Level> asks_; // lowest first
            std::unordered_map<uint64_t, Loc> index_; // order id -> where it rests
            uint64_t seq_ = 0; // arrival order to break timestamp ties
    };

    class Engine {
        public:
            explicit Engine(MatchRule rule) : books_(65536, OrderBook(rule)) {}

            void apply(const Event& e, std::vector<Trade>& trades) {
                OrderBook& book = books_[e.stock];
                switch (e.type) {
                    case EventType::ADD:
                    case EventType::INCOMING:
                        book.submit(e, trades);
                        break;
                    case EventType::CANCEL:
                        book.reduce(e.order_id, e.qty);
                        break;
                    case EventType::DELETE:
                        book.reduce(e.order_id, UINT32_MAX);
                        break;
                }
            }

            OrderBook& book(uint16_t stock) {
                return books_[stock];
            }

        private:
            std::vector<OrderBook> books_;
    };
}