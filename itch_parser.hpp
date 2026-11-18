#pragma once

#include "event.hpp"

#include <zlib.h>

#include <cstdint>
#include <cstring>
#include <functional>
#include <stdexcept>
#include <string>
#include <unordered_map>
#include <unordered_set>
#include <vector>

namespace ob::itch {
    inline uint16_t be16(const uint8_t* p) {
        return uint16_t(p[0] << 8 | p[1]);
    }

    inline uint32_t be32(const uint8_t* p) {
        return uint32_t(p[0]) << 24 | uint32_t(p[1]) << 16 | uint32_t(p[2]) << 8 | p[3];
    }

    inline uint64_t be_n(const uint8_t* p, int n) {
        uint64_t v = 0;
        for (int i = 0; i < n; ++i) {
            v = v << 8 | p[i];
        }
        return v;
    }

    class ItchParser {
        public:
            using Sink = std::function<void(const Event&)>;

            ItchParser(const std::vector<std::string>& symbols, Sink sink) : wanted_(symbols.begin(), symbols.end()), keep_(65536, symbols.empty()), sink_(std::move(sink)) {
                orders_.reserve(1 << 22);
            }

            // Process one message
            void process(const uint8_t* m) {
                const uint16_t stock = be16(m + 1);
                const uint64_t ts = be_n(m + 5, 6);
                const char type = char(m[0]);

                if (type == 'R') {
                    std::string ticker(reinterpret_cast<const char*>(m + 11), 8);
                    ticker.erase(ticker.find_last_not_of(' ') + 1);
                    if (wanted_.count(ticker)) keep_[stock] = true;
                    return;
                }
                if (!keep_[stock]) return;

                switch (type) {
                    case 'A': // add
                    case 'F': // add with market maker id
                        on_add(stock, ts, be_n(m + 11, 8), m[19] == 'B' ? Side::BUY : Side::SELL, be32(m + 20), be32(m + 32));
                        break;
                    case 'E': // executed
                        on_exec(stock, ts, be_n(m + 11, 8), be32(m + 19), 0, false);
                        break;
                    case 'C': // executed with price
                        if (m[31] == 'N') on_reduce(stock, ts, be_n(m + 11, 8), be32(m + 19)); // auction fill - remove
                        else on_exec(stock, ts, be_n(m + 11, 8), be32(m + 19), be32(m + 32), true);
                        break;
                    case 'X': // partial cancel
                        on_reduce(stock, ts, be_n(m + 11, 8), be32(m + 19));
                        break;
                    case 'D': // delete
                        on_reduce(stock, ts, be_n(m + 11, 8), UINT32_MAX);
                        break;
                    case 'U': //replace
                        on_replace(stock, ts, be_n(m + 11, 8), be_n(m + 19, 8), be32(m + 27), be32(m + 31));
                        break;
                    default: break; // skip any other messages
                }
            }

            void finish() { flush(); }

        private:
            struct Order {Side side; Price price; uint32_t remaining; };

            // Executions being collected into one incoming order
            struct Pending {
                bool active = false;
                uint16_t stock = 0;
                uint64_t ts = 0;
                Side side = Side::BUY;
                uint32_t qty = 0;
                Price worst = 0;
            };

            void on_add(uint16_t stock, uint64_t ts, uint64_t ref, Side side, uint32_t shares, Price price) {
                const bool through = side == Side::BUY ? price >= pending_.worst : price <= pending_.worst;
                if (pending_.active && pending_.stock == stock && pending_.ts == ts && pending_.side == side && through) {
                    // unfilled remainder of the aggressor
                    sink_({ts, ref, price, pending_.qty + shares, stock, EventType::INCOMING, side});
                    pending_.active = false;
                } else {
                    flush();
                    sink_({ts, ref, price, shares, stock, EventType::ADD, side});
                }
                orders_[ref] = {side, price, shares};
            }

            void on_exec(uint16_t stock, uint64_t ts, uint64_t ref, uint32_t shares, Price price, bool has_price) {
                auto it = orders_.find(ref);
                if (it == orders_.end()) return;
                Order& o = it->second;
                if (!has_price) price = o.price;
                const Side agg = opposite(o.side);
                shares = std::min(shares, o.remaining);
                if ((o.remaining -= shares) == 0) orders_.erase(it); // full executions get no delete msg

                // same aggressor if same stock, same ts, same side and price not backwards
                const bool backwards = agg == Side::BUY ? price < pending_.worst : price > pending_.worst;
                if (!(pending_.active && pending_.stock == stock && pending_.ts == ts && pending_.side == agg && !backwards)) {
                    flush();
                    pending_ = {true, stock, ts, agg, 0, price};
                }
                pending_.qty += shares;
                pending_.worst = price;
            }

            void on_reduce(uint16_t stock, uint64_t ts, uint64_t ref, uint32_t shares) {
                flush();
                auto it = orders_.find(ref);
                if (it == orders_.end()) return;
                Order& o = it->second;
                const bool whole = shares >= o.remaining;
                sink_({ts, ref, o.price, whole ? 0 : shares, stock, whole ? EventType::DELETE : EventType::CANCEL, o.side});
                if (whole) orders_.erase(it);
                else o.remaining -= shares;
            }

            void on_replace(uint16_t stock, uint64_t ts, uint64_t old_ref, uint64_t new_ref, uint32_t shares, Price price) {
                flush();
                auto it = orders_.find(old_ref);
                if (it == orders_.end()) return;
                const Order old = it->second;
                orders_.erase(it);
                orders_[new_ref] = {old.side, price, shares};
                sink_({ts, old_ref, old.price, 0, stock, EventType::DELETE, old.side});
                sink_({ts, new_ref, price, shares, stock, EventType::ADD, old.side});
            }

            // Helf executions with no resting remainder
            void flush() {
                if (!pending_.active) return;
                sink_({pending_.ts, syntheticIdBit | ++next_id_, pending_.worst, pending_.qty, pending_.stock, EventType::INCOMING, pending_.side});
                pending_.active = false;
            }

            std::unordered_set<std::string> wanted_;
            std::vector<bool> keep_;
            Sink sink_;
            std::unordered_map<uint64_t, Order> orders_;
            Pending pending_;
            uint64_t next_id_ = 0;
    };

    // Stream a .gz ITCH file through the parser
    inline void parse_file(const std::string& path, ItchParser& parser) {
        gzFile f = gzopen(path.c_str(), "rb");
        if (!f) throw std::runtime_error("cannot open " + path);
        std::vector<uint8_t> buf(16 << 20);
        size_t have = 0;
        int n;

        while((n = gzread(f, buf.data() + have, unsigned(buf.size() - have))) > 0) {
            have += size_t(n);
            size_t pos = 0;
            while (have - pos >= 2 && have - pos - 2 >= be16(&buf[pos])) {
                parser.process(&buf[pos + 2]);
                pos += 2 + be16(&buf[pos]);
            }
            std::memmove(buf.data(), buf.data() + pos, have - pos);
            have -= pos;
        }

        gzclose(f);
        if (n < 0) throw std::runtime_error("error reading " + path);
        parser.finish();
    }
}
