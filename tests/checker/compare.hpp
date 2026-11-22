#pragma once

// Correctness comparison between the sequential reference and a candidate engine.
//
// Trades: grouped by stock (keeping order), then compared field by field. Order across
//         stocks is free; order within a stock must be identical. Never memcmp: Trade
//         has uninitialized padding bytes.
// Books:  the final BookState of every stock must be identical.

#include "engine_api.hpp"

#include <algorithm>
#include <cstdio>
#include <map>
#include <set>
#include <string>
#include <vector>

namespace ob::check {
    struct CompareResult {
        bool ok = true;
        uint64_t mismatched_stocks = 0;
        std::vector<std::string> messages; // capped at max_report
        uint64_t ref_trades = 0, cand_trades = 0;
        uint64_t ref_shares = 0, cand_shares = 0;
    };

    inline bool same_trade(const Trade& a, const Trade& b) {
        return a.buy_id == b.buy_id && a.sell_id == b.sell_id && a.price == b.price &&
               a.qty == b.qty && a.stock == b.stock && a.aggressor == b.aggressor;
    }

    inline std::string format_trade(const Trade& t) {
        char buf[160];
        std::snprintf(buf, sizeof buf, "{buy=%llu sell=%llu px=%u qty=%u stock=%u agg=%s}",
                      (unsigned long long)t.buy_id, (unsigned long long)t.sell_id, t.price, t.qty,
                      unsigned(t.stock), t.aggressor == Side::BUY ? "B" : "S");
        return buf;
    }

    inline std::string format_level(const LevelState& l) {
        char buf[96];
        std::snprintf(buf, sizeof buf, "{%s px=%u total=%llu}", l.side == Side::BUY ? "bid" : "ask",
                      l.price, (unsigned long long)l.total);
        return buf;
    }

    inline std::map<uint16_t, std::vector<const Trade*>> by_stock(const std::vector<Trade>& trades) {
        std::map<uint16_t, std::vector<const Trade*>> out;
        for (const Trade& t : trades) out[t.stock].push_back(&t);
        return out;
    }

    inline void report(CompareResult& r, size_t max_report, std::string msg) {
        r.ok = false;
        ++r.mismatched_stocks;
        if (r.messages.size() < max_report) r.messages.push_back(std::move(msg));
    }

    inline CompareResult compare_trades(const std::vector<Trade>& ref, const std::vector<Trade>& cand, size_t max_report = 10) {
        CompareResult r;
        r.ref_trades = ref.size();
        r.cand_trades = cand.size();
        for (const Trade& t : ref) r.ref_shares += t.qty;
        for (const Trade& t : cand) r.cand_shares += t.qty;

        const auto a = by_stock(ref), b = by_stock(cand);
        std::set<uint16_t> stocks;
        for (const auto& [s, v] : a) stocks.insert(s);
        for (const auto& [s, v] : b) stocks.insert(s);

        static const std::vector<const Trade*> none;
        for (uint16_t s : stocks) {
            const auto ia = a.find(s), ib = b.find(s);
            const auto& x = ia == a.end() ? none : ia->second;
            const auto& y = ib == b.end() ? none : ib->second;
            const size_t n = std::min(x.size(), y.size());

            size_t i = 0;
            while (i < n && same_trade(*x[i], *y[i])) ++i;

            if (i < n) {
                report(r, max_report, "stock " + std::to_string(s) + " trade #" + std::to_string(i) +
                       ": expected " + format_trade(*x[i]) + " got " + format_trade(*y[i]));
            } else if (x.size() != y.size()) {
                const std::string first = x.size() > y.size() ? "first missing " + format_trade(*x[n])
                                                              : "first extra " + format_trade(*y[n]);
                report(r, max_report, "stock " + std::to_string(s) + ": expected " + std::to_string(x.size()) +
                       " trades, got " + std::to_string(y.size()) + " (" + first + ")");
            }
        }
        return r;
    }

    inline std::map<uint16_t, BookState> capture_books(MatchingEngine& eng, const std::vector<uint16_t>& stocks) {
        std::map<uint16_t, BookState> out;
        for (uint16_t s : stocks) out[s] = eng.book_state(s);
        return out;
    }

    inline CompareResult compare_books(const std::map<uint16_t, BookState>& ref, MatchingEngine& cand, size_t max_report = 10) {
        CompareResult r;
        for (const auto& [s, want] : ref) {
            const BookState got = cand.book_state(s);
            if (got == want) continue;

            const size_t n = std::min(want.size(), got.size());
            size_t i = 0;
            while (i < n && want[i] == got[i]) ++i;

            std::string msg = "stock " + std::to_string(s) + " final book level #" + std::to_string(i) + ": ";
            if (i < n) msg += "expected " + format_level(want[i]) + " got " + format_level(got[i]);
            else msg += "expected " + std::to_string(want.size()) + " levels, got " + std::to_string(got.size());
            report(r, max_report, msg);
        }
        return r;
    }

    inline void print_result(const char* what, const CompareResult& r) {
        if (r.ok) return;
        std::printf("    %s mismatch in %llu stock(s)\n", what, (unsigned long long)r.mismatched_stocks);
        if (r.ref_trades || r.cand_trades) {
            std::printf("    trades: expected %llu (%llu shares), got %llu (%llu shares)\n",
                        (unsigned long long)r.ref_trades, (unsigned long long)r.ref_shares,
                        (unsigned long long)r.cand_trades, (unsigned long long)r.cand_shares);
        }
        for (const std::string& m : r.messages) std::printf("      %s\n", m.c_str());
        if (r.mismatched_stocks > r.messages.size()) {
            std::printf("      ... %llu more\n", (unsigned long long)(r.mismatched_stocks - r.messages.size()));
        }
    }
}
