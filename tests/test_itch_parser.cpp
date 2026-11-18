// Tests for the ITCH parser

#include "../event.hpp"
#include "../itch_parser.hpp"

#include <cstdio>
#include <string>
#include <vector>

using namespace ob;
using namespace ob::itch;

static int passed = 0, failed = 0;

static void check(const char*name, bool ok) {
    if (ok) ++passed;
    else {
        ++failed;
        std::printf("FAIL %s\n", name);
    }
}

static Price P(double dollars) {
    return Price(dollars * 10000 + 0.5);
}

struct Itch {
    std::vector<uint8_t> bytes;
    void msg(char type, uint16_t stock, uint64_t ts, std::vector<std::pair<uint64_t, int>> fields) {
        std::vector<uint8_t> m{uint8_t(type)};

        auto put = [&](uint64_t v, int n) {
            for (int i = n - 1; i >= 0; --i) {
                m.push_back(uint8_t(v >> (8*i)));
            }
        };

        put(stock, 2); put(0, 2); put(ts, 6); // header: locate, tracking, timestamp
        for (auto [v, n] : fields) put(v, n);
        bytes.push_back(uint8_t(m.size() >> 8)); bytes.push_back(uint8_t(m.size()));
        bytes.insert(bytes.end(), m.begin(), m.end());
    }

    static uint64_t text8(const std::string& s) {
        uint64_t v = 0;
        for (int i = 0; i < 8; ++i) {
            v = v << 8 | uint8_t(i < int(s.size()) ? s[i] : ' ');
        }
        return v;
    }

    void R(uint16_t s, const std::string& t) {
        msg('R', s, 0, {{text8(t), 8}, {0, 8}, {0, 8}, {0, 4}});
    }

    void A(uint16_t s, uint64_t ts, uint64_t ref, char side, uint32_t q, Price p) {
        msg('A', s, ts, {{ref, 8}, {uint8_t(side), 1}, {q, 4}, {text8("TEST"), 8}, {p, 4}});
    }

    void E(uint16_t s, uint64_t ts, uint64_t ref, uint32_t q) {
        msg('E', s, ts, {{ref, 8}, {q, 4}, {0, 8}});
    }

    void C(uint16_t s, uint64_t ts, uint64_t ref, uint32_t q, char printable, Price p) {
        msg('C', s, ts, {{ref, 8}, {q, 4}, {0, 8}, {uint8_t(printable), 1}, {p, 4}});
    }

    void X(uint16_t s, uint64_t ts, uint64_t ref, uint32_t q) {
        msg('X', s, ts, {{ref, 8}, {q, 4}});
    }

    void D(uint16_t s, uint64_t ts, uint64_t ref) {
        msg('D', s, ts, {{ref, 8}});
    }

    void U(uint16_t s, uint64_t ts, uint64_t old_ref, uint64_t new_ref, uint32_t q, Price p) {
        msg('U', s, ts, {{old_ref, 8}, {new_ref, 8}, {q, 4}, {p, 4}});
    }
};

static std::vector<Event> parse(const Itch& b, const std::vector<std::string>& symbols = {}) {
    std::vector<Event> out;
    ItchParser p(symbols, [&](const Event& e) {
        out.push_back(e);
    });
    for (size_t pos = 0; pos + 2 <= b.bytes.size(); pos += 2 + be16(&b.bytes[pos])) {
        p.process(&b.bytes[pos + 2]);
    }
    p.finish();
    return out;
}

static bool is(const Event& e, EventType t, uint64_t id, Side side, Price price, uint32_t qty) {
    return e.type == t && e.order_id == id && e.side == side && e.price == price && e.qty == qty;
}

int main() {
    {Itch b; b.A(5, 1, 11, 'B', 300, P(99.5)); b.A(5, 2, 12, 'S', 200, P(100)); b.X(5, 3, 11, 100); b.D(5, 4, 12);
        auto ev = parse(b);
        check("add, cancel, delete", ev.size() == 4 && is(ev[0], EventType::ADD, 11, Side::BUY, P(99.5), 300) &&
            ev[0].stock == 5 && is(ev[2], EventType::CANCEL, 11, Side::BUY, P(99.5), 100) &&
            is(ev[3], EventType::DELETE, 12, Side::SELL, P(100), 0)); }

    {Itch b; b.A(1, 1, 7, 'S', 100, P(101)); b.A(1, 2, 9, 'S', 50, P(102)); b.E(1, 3, 7, 100); b.E(1, 3, 9, 50);
        auto ev = parse(b);
        check("sweep -> one incoming buy 150 at 102", ev.size() == 3 && ev[2].price == P(102) && (ev[2].order_id & syntheticIdBit)); }

    {Itch b; b.A(1, 1, 7, 'S', 100, P(101)); b.E(1, 2, 7, 40); b.E(1, 3, 7, 60);
        auto ev = parse(b);
        check("different timestamps -> two incoming", ev.size() == 3 && ev[1].qty == 40 && ev[2].qty == 60); }

    {Itch b; b.A(1, 1, 7, 'S', 100, P(101)); b.A(1, 1, 8, 'S', 100, P(102)); b.A(1, 1, 9, 'S', 100, P(101)); b.E(1, 2, 7, 100); b.E(1, 2, 8, 100); b.E(1, 2, 9, 100);
        auto ev = parse(b);
        check("price reversal splits the run", ev.size() == 5 && ev[3].qty == 200 && ev[4].qty == 100); }

    {Itch b; b.A(1, 1, 7, 'B', 100, P(100)); b.C(1, 2, 7, 40, 'Y', P(99.99)); b.C(1, 3, 7, 60, 'N', P(100));
        auto ev = parse(b);
        check("C fill and auction fill", ev.size() == 3 && ev[1].type == EventType::INCOMING && ev[1].side == Side::SELL && ev[1].price == P(99.99) && is(ev[2], EventType::DELETE, 7, Side::BUY, P(100), 0)); }

    std::printf("%d passed, %d failed\n", passed, failed);
    return failed ? 1 : 0;
}