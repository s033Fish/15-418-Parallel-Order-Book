#pragma once

// Timing harness. Each repetition gets a fresh engine (book state must not carry over).
// Only MatchingEngine::run() is timed; construction, prepare() and teardown are not.

#include "engine_api.hpp"

#include <algorithm>
#include <chrono>
#include <cmath>
#include <numeric>
#include <vector>

namespace ob::check {
    struct Stats {
        double min = 0, median = 0, mean = 0, stddev = 0;
        std::vector<double> samples;
    };

    inline Stats summarize(std::vector<double> s) {
        Stats st;
        if (s.empty()) return st;
        st.samples = s;
        std::sort(s.begin(), s.end());
        st.min = s.front();
        st.median = s.size() % 2 ? s[s.size() / 2] : (s[s.size() / 2 - 1] + s[s.size() / 2]) / 2;
        st.mean = std::accumulate(s.begin(), s.end(), 0.0) / s.size();
        double var = 0;
        for (double x : s) var += (x - st.mean) * (x - st.mean);
        st.stddev = s.size() > 1 ? std::sqrt(var / (s.size() - 1)) : 0;
        return st;
    }

    // make: callable returning std::unique_ptr<MatchingEngine>
    template <class Make>
    Stats benchmark(Make make, const std::vector<Event>& events, int threads, int warmup, int reps) {
        std::vector<double> samples;
        volatile size_t sink = 0; // keep the result observable

        for (int i = 0; i < warmup + reps; ++i) {
            auto eng = make();
            eng->prepare(events);
            std::vector<Trade> out;
            out.reserve(events.size() / 4);

            const auto t0 = std::chrono::steady_clock::now();
            eng->run(events, threads, out);
            const auto t1 = std::chrono::steady_clock::now();

            sink = sink + out.size();
            if (i >= warmup) samples.push_back(std::chrono::duration<double>(t1 - t0).count());
        }
        return summarize(samples);
    }
}
