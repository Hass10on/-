#pragma once

#include <cmath>
#include <cstdint>
#include <vector>

namespace refcore {

// SplitMix64: one 64-bit word of state, so careers save and replay deterministically.
class Rng {
public:
    explicit Rng(uint64_t seedValue = 0x5EEDu) : state_(seedValue) {}

    uint64_t state() const { return state_; }
    void setState(uint64_t s) { state_ = s; }

    uint64_t next() {
        uint64_t z = (state_ += 0x9E3779B97F4A7C15ull);
        z = (z ^ (z >> 30)) * 0xBF58476D1CE4E5B9ull;
        z = (z ^ (z >> 27)) * 0x94D049BB133111EBull;
        return z ^ (z >> 31);
    }
    double uniform() { return static_cast<double>(next() >> 11) * (1.0 / 9007199254740992.0); }
    double range(double lo, double hi) { return lo + (hi - lo) * uniform(); }
    int irange(int lo, int hiInclusive) {
        if (hiInclusive <= lo) return lo;
        const uint64_t span = static_cast<uint64_t>(hiInclusive - lo) + 1u;
        return lo + static_cast<int>(next() % span);
    }
    bool chance(double p) { return uniform() < p; }
    double normal(double mean, double sd) {
        double u1 = uniform();
        if (u1 < 1e-12) u1 = 1e-12;
        const double u2 = uniform();
        return mean + sd * std::sqrt(-2.0 * std::log(u1)) * std::cos(6.283185307179586 * u2);
    }
    // Index drawn proportionally to non-negative weights; -1 if all weights are zero.
    int weighted(const std::vector<double>& w) {
        double total = 0;
        for (double x : w) total += x > 0 ? x : 0;
        if (total <= 0) return -1;
        double r = uniform() * total;
        for (size_t i = 0; i < w.size(); ++i) {
            const double x = w[i] > 0 ? w[i] : 0;
            if (r < x) return static_cast<int>(i);
            r -= x;
        }
        return static_cast<int>(w.size()) - 1;
    }

    static uint64_t mix(uint64_t a, uint64_t b) {
        Rng r(a ^ (b * 0x9E3779B97F4A7C15ull));
        return r.next();
    }

private:
    uint64_t state_;
};

}  // namespace refcore
