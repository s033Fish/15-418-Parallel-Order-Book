#pragma once

// Adapters that wrap each engine implementation in the MatchingEngine interface, plus the
// registry the checker looks names up in.
//
// To add a new engine:
//   1. #include its header below
//   2. write an adapter class (see SequentialAdapter)
//   3. add one line to registry()

#include "engine_api.hpp"
#include "parallel/across-stock-engine.hpp"

#include <functional>
#include <memory>
#include <stdexcept>
#include <string>
#include <vector>

namespace ob::check {
    // Reference: the single-threaded engine in sequential/sequential-engine.hpp
    class SequentialAdapter : public MatchingEngine {
        public:
            explicit SequentialAdapter(MatchRule rule) : engine_(rule) {}

            std::string name() const override { return "seq"; }
            bool parallel() const override { return false; }

            void run(const std::vector<Event>& events, int, std::vector<Trade>& out) override {
                for (const Event& e : events) engine_.apply(e, out);
            }

            BookState book_state(uint16_t stock) override { return snapshot(engine_.book(stock)); }

        private:
            Engine engine_;
    };

    // Week 2: across-stock parallel engine (parallel/across-stock-engine.hpp)
    class AcrossStockAdapter : public MatchingEngine {
        public:
            AcrossStockAdapter(MatchRule rule, Schedule schedule, std::string name)
                : engine_(rule, schedule), name_(std::move(name)) {}

            std::string name() const override { return name_; }

            void run(const std::vector<Event>& events, int threads, std::vector<Trade>& out) override {
                engine_.run(events, threads, out);
            }

            BookState book_state(uint16_t stock) override { return snapshot(engine_.book(stock)); }

        private:
            AcrossStockEngine engine_;
            std::string name_;
    };

    using EngineFactory =std::function<std::unique_ptr<MatchingEngine>(MatchRule)>;

    struct EngineEntry {
        std::string name;
        EngineFactory make;
    };

    inline const std::vector<EngineEntry>& registry() {
        static const std::vector<EngineEntry> engines = {
            {"seq", [](MatchRule r) { return std::make_unique<SequentialAdapter>(r); }},
            {"stock-static", [](MatchRule r) { return std::make_unique<AcrossStockAdapter>(r, Schedule::STATIC, "stock-static"); }},
            {"stock-dynamic", [](MatchRule r) { return std::make_unique<AcrossStockAdapter>(r, Schedule::DYNAMIC, "stock-dynamic"); }},
            {"stock-semi", [](MatchRule r) { return std::make_unique<AcrossStockAdapter>(r, Schedule::SEMI_STATIC, "stock-semi"); }},
        };
        return engines;
    }

    inline std::unique_ptr<MatchingEngine> make_engine(const std::string& name, MatchRule rule) {
        for (const EngineEntry& e : registry()) {
            if (e.name == name) return e.make(rule);
        }
        throw std::runtime_error("unknown engine '" + name + "'");
    }
}
