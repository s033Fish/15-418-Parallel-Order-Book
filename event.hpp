#pragma once

#include <cstdint>

namespace ob {
    // Prices are int ticks of 1e-4 dollars
    using Price = uint32_t;

    // Enum for buy vs. sell side
    enum class Side : uint8_t {
        BUY,
        SELL,
    };

    inline constexpr Side opposite(Side s) {
        return s == Side::BUY ? Side::SELL : Side::BUY;
    }

    enum class EventType : uint8_t {
        ADD, // Passive limit order that rests in the book
        CANCEL, // Remove shares from a specific order
        DELETE, // Remove the entire order
        INCOMING, // Aggressive limit order that is matched, and excess rests
    };

    // Made up id for aggressors that never rested
    inline constexpr uint64_t syntheticIdBit = 1ull << 63;

    struct Event {
        uint64_t ts_ns;         // ITCH timestamp
        uint64_t order_id;      // ITCH orderID
        Price price;            // limit price
        uint32_t qty;           // Add/incoming: # shares | Cancel: shares removed | Delete: 0
        uint16_t stock;         // ITCH stock locate code
        EventType type;
        Side side;
    };
}