#pragma once

// Read a whole events.bin into memory

#include "event.hpp"

#include <cstdio>
#include <stdexcept>
#include <string>
#include <vector>

namespace ob {
    inline std::vector<Event> load_events(const std::string& path) {
        FILE* f = std::fopen(path.c_str(), "rb");
        if (!f) throw std::runtime_error("cannot open " + path);
        std::fseek(f, 0, SEEK_END);
        const long bytes = std::ftell(f);
        std::fseek(f, 0, SEEK_SET);
        std::vector<Event> events(size_t(bytes) / sizeof(Event));
        const size_t got = std::fread(events.data(), sizeof(Event), events.size(), f);
        std::fclose(f);
        if (got != events.size()) throw std::runtime_error("short read on " + path);
        return events;
    }
}