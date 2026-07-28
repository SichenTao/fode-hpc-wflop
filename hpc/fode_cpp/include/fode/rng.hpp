#pragma once

#include <cmath>
#include <cstdint>
#include <limits>
#include <numbers>

namespace fode {

// A counter-keyed generator makes every logical FODE draw independent of
// OpenMP scheduling. It preserves the required uniform/normal/Cauchy laws,
// although it intentionally does not reproduce MATLAB's MT19937 bit stream.
class CounterRng {
public:
    explicit CounterRng(std::uint64_t seed) : seed_(seed) {}

    [[nodiscard]] double uniform(
        std::uint64_t generation,
        std::uint64_t phase,
        std::uint64_t individual,
        std::uint64_t coordinate = 0,
        std::uint64_t draw = 0
    ) const {
        std::uint64_t value = seed_;
        mix_into(value, generation);
        mix_into(value, phase);
        mix_into(value, individual);
        mix_into(value, coordinate);
        mix_into(value, draw);
        const std::uint64_t bits = splitmix64(value);
        return static_cast<double>(bits >> 11)
            * (1.0 / 9007199254740992.0);
    }

    [[nodiscard]] int integer(
        int low,
        int high_exclusive,
        std::uint64_t generation,
        std::uint64_t phase,
        std::uint64_t individual,
        std::uint64_t coordinate = 0,
        std::uint64_t draw = 0
    ) const {
        const double u = uniform(
            generation, phase, individual, coordinate, draw
        );
        const int width = high_exclusive - low;
        int result = low + static_cast<int>(u * static_cast<double>(width));
        if (result >= high_exclusive) {
            result = high_exclusive - 1;
        }
        return result;
    }

    [[nodiscard]] double normal(
        std::uint64_t generation,
        std::uint64_t phase,
        std::uint64_t individual,
        std::uint64_t coordinate = 0,
        std::uint64_t draw = 0
    ) const {
        const double u1 = std::max(
            uniform(generation, phase, individual, coordinate, 2 * draw),
            std::numeric_limits<double>::min()
        );
        const double u2 = uniform(
            generation, phase, individual, coordinate, 2 * draw + 1
        );
        return std::sqrt(-2.0 * std::log(u1))
            * std::cos(2.0 * std::numbers::pi * u2);
    }

private:
    std::uint64_t seed_;

    static std::uint64_t splitmix64(std::uint64_t value) {
        value += 0x9e3779b97f4a7c15ULL;
        value = (value ^ (value >> 30)) * 0xbf58476d1ce4e5b9ULL;
        value = (value ^ (value >> 27)) * 0x94d049bb133111ebULL;
        return value ^ (value >> 31);
    }

    static void mix_into(std::uint64_t& state, std::uint64_t value) {
        state ^= splitmix64(value + 0x9e3779b97f4a7c15ULL + (state << 6)
                            + (state >> 2));
    }
};

}  // namespace fode
