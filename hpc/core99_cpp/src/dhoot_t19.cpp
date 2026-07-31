/*
WFLOP IMPLEMENTATION FACT DECLARATION
Implementation unit: T19 pure-C++ CPU-HPC Jensen/MRF wrapper around official
SRMP v1.01 TRW-S
Paper DOI: 10.1016/j.energy.2021.120035.
Public source: pinned SRMP v1.01 archive described in the controlling header.
Missing fields, Reconstruction decisions, licensing, semantic IDs and
Claim boundary: include/core99/dhoot_t19.hpp.
Contract: shared/contracts/core99_t19_dhoot_2021.json.
This optional source is GPL-3.0-or-later when linked with official SRMP.
Last evidence-audit date: 2026-08-01
END WFLOP IMPLEMENTATION FACT DECLARATION
*/
#include "core99/dhoot_t19.hpp"

#include "fode/executor.hpp"

#include <SRMP.h>
#include <FactorTypes/SharedPairwiseType.h>

#include <algorithm>
#include <array>
#include <bit>
#include <chrono>
#include <cmath>
#include <cstddef>
#include <cstdint>
#include <limits>
#include <numbers>
#include <numeric>
#include <set>
#include <stdexcept>
#include <tuple>
#include <utility>
#include <vector>

namespace core99::t19 {
namespace {

using Clock = std::chrono::steady_clock;
constexpr double kWakeDecay = 0.1;
constexpr double kAnnualHours = 8760.0;
constexpr double kAirDensity = 1.225;
constexpr const char* kHistoricalProblem =
    "t19_historical_grid100_jensen_v1";
constexpr const char* kRealisticProblem =
    "t19_realistic_grid100_400_2500_jensen_nrel5mw_v1";

// SRMP v1.01's SharedPairwiseFactorType predates the flags argument added to
// Energy::FactorType::InitFactor. This adapter preserves the official
// implementation and merely forwards the modern virtual entry point to the
// source-provided two-argument implementation.
class CompatibleSharedPairwiseFactorType final
    : public SharedPairwiseFactorType {
public:
    using SharedPairwiseFactorType::SharedPairwiseFactorType;

    void InitFactor(
        Energy::NonSingletonFactor* factor,
        double* user_data,
        unsigned flags
    ) override {
        (void)flags;
        SharedPairwiseFactorType::InitFactor(factor, user_data);
    }
};

double elapsed(const Clock::time_point start) {
    return std::chrono::duration<double>(Clock::now() - start).count();
}

std::uint64_t mix_hash(std::uint64_t hash, const std::uint64_t value) {
    hash ^= value + 0x9e3779b97f4a7c15ULL + (hash << 6U) + (hash >> 2U);
    return hash;
}

std::vector<WindState> wr36() {
    // The target paper shows WR-36 graphically but publishes no numeric table.
    // This is the same versioned Figure profile used by the platform's cited
    // Turner-lineage T05 benchmark; it is normalized jointly here.
    constexpr double profile17[36]{
        1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,
        1,1,1,1,1,1,1,1,1.2,1.55,1.7,2.7,3.2,2.7,1.7,1.55,1.2,1
    };
    constexpr double profile12[36]{
        1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,
        1,1,1,1,1,1,1,1,1.2,1.45,1.75,1.7,2.35,1.7,1.75,1.45,1.2,1
    };
    std::vector<WindState> states;
    states.reserve(108);
    double total = 0.0;
    for (int direction = 0; direction < 36; ++direction) {
        const double p8 = 0.005;
        const double p12 = 0.008 * profile12[direction];
        const double p17 = 0.011 * profile17[direction];
        states.push_back({10.0 * direction, 8.0, p8});
        states.push_back({10.0 * direction, 12.0, p12});
        states.push_back({10.0 * direction, 17.0, p17});
        total += p8 + p12 + p17;
    }
    for (auto& state : states) state.probability /= total;
    return states;
}

constexpr std::array<double, 48> kNrelSpeeds{
    2.0,2.5,3.0,3.5,4.0,4.5,5.0,5.5,6.0,6.5,7.0,7.5,
    8.0,8.5,9.0,9.5,10.0,10.5,11.0,11.5,12.0,12.5,13.0,13.5,
    14.0,14.5,15.0,15.5,16.0,16.5,17.0,17.5,18.0,18.5,19.0,19.5,
    20.0,20.5,21.0,21.5,22.0,22.5,23.0,23.5,24.0,24.5,25.0,25.5
};
constexpr std::array<double, 48> kNrelCp{
    0,0,.1780851,.28907459,.34902166,.3847278,.40605878,.4202279,
    .42882274,.43387274,.43622267,.43684468,.43657497,.43651053,
    .4365612,.43651728,.43590309,.43467276,.43322955,.43003137,
    .37655587,.33328466,.29700574,.26420779,.23839379,.21459275,
    .19382354,.1756635,.15970926,.14561785,.13287856,.12130194,
    .11219941,.10311631,.09545392,.08813781,.08186763,.07585005,
    .07071926,.06557558,.06148104,.05755207,.05413366,.05097969,
    .04806545,.04536883,.04287006,.04055141
};
constexpr std::array<double, 48> kNrelCt{
    1.19187945,1.17284634,1.09860817,1.02889592,.97373036,.92826162,
    .89210543,.86100905,.835423,.81237673,.79225789,.77584769,
    .7629228,.76156073,.76261984,.76169723,.75232027,.74026851,
    .72987175,.70701647,.54054532,.45509459,.39343381,.34250785,
    .30487242,.27164979,.24361964,.21973831,.19918151,.18131868,
    .16537679,.15103727,.13998636,.1289037,.11970413,.11087113,
    .10339901,.09617888,.09009926,.08395078,.0791188,.07448356,
    .07050731,.06684119,.06345518,.06032267,.05741999,.05472609
};

template <std::size_t N>
double interpolate(
    const double value,
    const std::array<double, N>& x,
    const std::array<double, N>& y
) {
    if (value <= x.front()) return y.front();
    if (value >= x.back()) return y.back();
    const auto upper = std::upper_bound(x.begin(), x.end(), value);
    const std::size_t right = static_cast<std::size_t>(upper - x.begin());
    const std::size_t left = right - 1U;
    const double fraction = (value - x[left]) / (x[right] - x[left]);
    return y[left] + fraction * (y[right] - y[left]);
}

double rotor_radius(const ProblemFamily family) {
    return family == ProblemFamily::historical ? 20.0 : 63.0;
}

double thrust_coefficient(
    const ProblemFamily family,
    const double speed
) {
    if (family == ProblemFamily::historical) return 0.88;
    if (speed < 3.0 || speed > 25.0) return 0.0;
    return std::clamp(interpolate(speed, kNrelSpeeds, kNrelCt), 0.0, .999);
}

double turbine_power_kw(
    const ProblemFamily family,
    const double speed
) {
    if (family == ProblemFamily::historical) return .3 * speed * speed * speed;
    if (speed < 3.0 || speed > 25.0) return 0.0;
    const double cp = interpolate(speed, kNrelSpeeds, kNrelCp);
    const double radius = rotor_radius(family);
    return std::min(
        5000.0,
        .5 * kAirDensity * std::numbers::pi * radius * radius
            * cp * speed * speed * speed / 1000.0
    );
}

double single_deficit(
    const Problem& problem,
    const int source,
    const int target,
    const WindState& wind
) {
    if (source == target) return 0.0;
    const auto& cells = problem.cells();
    const double radians = wind.from_degrees * std::numbers::pi / 180.0;
    const double flow_x = -std::sin(radians);
    const double flow_y = -std::cos(radians);
    const double dx = cells[target].x_m - cells[source].x_m;
    const double dy = cells[target].y_m - cells[source].y_m;
    const double downstream = dx * flow_x + dy * flow_y;
    if (downstream <= 1.0e-12) return 0.0;
    const double crosswind = std::abs(dx * flow_y - dy * flow_x);
    const double radius = rotor_radius(problem.config().family);
    if (crosswind > radius + kWakeDecay * downstream) return 0.0;
    const double ct = thrust_coefficient(problem.config().family, wind.speed_mps);
    if (ct <= 0.0) return 0.0;
    const double induction = .5 * (1.0 - std::sqrt(1.0 - ct));
    return 2.0 * induction
        / std::pow(1.0 + kWakeDecay * downstream / radius, 2.0);
}

std::vector<double> build_interaction(
    const Problem& problem,
    fode::PersistentExecutor& executor
) {
    const int n = problem.config().cell_count;
    std::vector<double> directed(static_cast<std::size_t>(n) * n, 0.0);
    executor.parallel_for(0, n, [&](const int source) {
        for (int target = 0; target < n; ++target) {
            double value = 0.0;
            for (const auto& wind : problem.wind_states()) {
                const double deficit = single_deficit(
                    problem, source, target, wind
                );
                value += wind.probability * wind.speed_mps
                    * deficit * deficit;
            }
            directed[static_cast<std::size_t>(source) * n + target] = value;
        }
    });
    std::vector<double> pair(static_cast<std::size_t>(n) * n, 0.0);
    executor.parallel_for(0, n, [&](const int i) {
        for (int j = i + 1; j < n; ++j) {
            const double value = directed[static_cast<std::size_t>(i) * n + j]
                + directed[static_cast<std::size_t>(j) * n + i];
            pair[static_cast<std::size_t>(i) * n + j] = value;
            pair[static_cast<std::size_t>(j) * n + i] = value;
        }
    });
    return pair;
}

double minimum_distance(const Problem& problem, const std::vector<int>& layout) {
    if (layout.size() < 2U) return std::numeric_limits<double>::infinity();
    double result = std::numeric_limits<double>::infinity();
    for (std::size_t i = 0; i < layout.size(); ++i) {
        for (std::size_t j = i + 1U; j < layout.size(); ++j) {
            const double dx = problem.cells()[layout[i]].x_m
                - problem.cells()[layout[j]].x_m;
            const double dy = problem.cells()[layout[i]].y_m
                - problem.cells()[layout[j]].y_m;
            result = std::min(result, std::hypot(dx, dy));
        }
    }
    return result;
}

bool conflict(const Problem& problem, const int i, const int j) {
    if (problem.config().family == ProblemFamily::historical) return false;
    const double dx = problem.cells()[i].x_m - problem.cells()[j].x_m;
    const double dy = problem.cells()[i].y_m - problem.cells()[j].y_m;
    return std::hypot(dx, dy) + 1.0e-10
        < problem.minimum_spacing_requirement_m();
}

double wake_objective(
    const std::vector<double>& pair,
    const int n,
    const std::vector<int>& layout
) {
    double result = 0.0;
    for (std::size_t a = 0; a < layout.size(); ++a) {
        for (std::size_t b = a + 1U; b < layout.size(); ++b) {
            result += pair[static_cast<std::size_t>(layout[a]) * n + layout[b]];
        }
    }
    return result;
}

double augmented_energy(
    const Problem& problem,
    const std::vector<double>& pair,
    const std::vector<int>& layout,
    const double beta,
    const double conflict_penalty
) {
    double value = wake_objective(pair, problem.config().cell_count, layout);
    const double delta = static_cast<double>(layout.size() - problem.config().turbine_count);
    value += beta * delta * delta;
    for (std::size_t a = 0; a < layout.size(); ++a) {
        for (std::size_t b = a + 1U; b < layout.size(); ++b) {
            if (conflict(problem, layout[a], layout[b])) value += conflict_penalty;
        }
    }
    return value;
}

Evaluation evaluate_layout(
    const Problem& problem,
    const std::vector<int>& layout,
    fode::PersistentExecutor* executor
) {
    Evaluation result;
    result.exact_cardinality = static_cast<int>(layout.size())
        == problem.config().turbine_count;
    result.minimum_spacing_m = minimum_distance(problem, layout);
    result.spacing_feasible = result.minimum_spacing_m + 1.0e-10
        >= problem.minimum_spacing_requirement_m();
    std::vector<unsigned char> seen(
        static_cast<std::size_t>(problem.config().cell_count), 0
    );
    for (const int cell : layout) {
        if (cell < 0 || cell >= problem.config().cell_count || seen[cell]) {
            throw std::invalid_argument("T19 layout has invalid/duplicate cell");
        }
        seen[cell] = 1;
    }
    const int tasks = static_cast<int>(
        problem.wind_states().size() * layout.size()
    );
    std::vector<double> contribution(static_cast<std::size_t>(tasks), 0.0);
    const auto evaluate_task = [&](const int task) {
        const std::size_t wind_index = static_cast<std::size_t>(task)
            / layout.size();
        const std::size_t target_index = static_cast<std::size_t>(task)
            % layout.size();
        const auto& wind = problem.wind_states()[wind_index];
        const int target = layout[target_index];
        double deficit_square = 0.0;
        for (const int source : layout) {
            const double deficit = single_deficit(
                problem, source, target, wind
            );
            deficit_square += deficit * deficit;
        }
        const double effective = wind.speed_mps
            * std::max(0.0, 1.0 - std::sqrt(deficit_square));
        contribution[static_cast<std::size_t>(task)] = wind.probability
            * turbine_power_kw(problem.config().family, effective);
    };
    if (executor != nullptr && tasks > 1) {
        executor->parallel_for(0, tasks, evaluate_task);
    } else {
        for (int task = 0; task < tasks; ++task) evaluate_task(task);
    }
    // Fixed task-index reduction makes one/all-worker receipts bit-identical.
    result.expected_power_kw = std::accumulate(
        contribution.begin(), contribution.end(), 0.0
    );
    result.aep_gwh = result.expected_power_kw * kAnnualHours / 1.0e6;
    result.physical_fes = 1;
    return result;
}

struct Triangle {
    double score = 0.0;
    int i = 0;
    int j = 0;
    int k = 0;
};

std::vector<std::array<int, 3>> select_triplets(
    const Problem& problem,
    const std::vector<double>& pair,
    const int requested,
    fode::PersistentExecutor& executor
) {
    if (requested <= 0) return {};
    const int n = problem.config().cell_count;
    constexpr int neighbors = 24;
    std::vector<std::vector<int>> strongest(static_cast<std::size_t>(n));
    executor.parallel_for(0, n, [&](const int i) {
        std::vector<std::pair<double, int>> row;
        row.reserve(static_cast<std::size_t>(n - 1));
        for (int j = 0; j < n; ++j) {
            if (i == j) continue;
            double score = pair[static_cast<std::size_t>(i) * n + j];
            if (conflict(problem, i, j)) {
                score += std::numeric_limits<double>::max() / 1.0e100;
            }
            row.emplace_back(-score, j);
        }
        const int keep = std::min(neighbors, static_cast<int>(row.size()));
        std::partial_sort(row.begin(), row.begin() + keep, row.end());
        strongest[i].reserve(static_cast<std::size_t>(keep));
        for (int index = 0; index < keep; ++index) {
            strongest[i].push_back(row[index].second);
        }
    });

    std::set<std::array<int, 3>> unique;
    for (int i = 0; i < n; ++i) {
        const auto& row = strongest[i];
        for (std::size_t a = 0; a < row.size(); ++a) {
            for (std::size_t b = a + 1U; b < row.size(); ++b) {
                std::array<int, 3> key{i, row[a], row[b]};
                std::sort(key.begin(), key.end());
                if (key[0] != key[1] && key[1] != key[2]) unique.insert(key);
            }
        }
    }
    std::vector<Triangle> candidates;
    candidates.reserve(unique.size());
    for (const auto& key : unique) {
        const auto at = [&](const int a, const int b) {
            return pair[static_cast<std::size_t>(a) * n + b]
                + (conflict(problem, a, b) ? 1.0e6 : 0.0);
        };
        candidates.push_back({
            at(key[0], key[1]) + at(key[0], key[2]) + at(key[1], key[2]),
            key[0], key[1], key[2]
        });
    }
    std::sort(candidates.begin(), candidates.end(), [](const Triangle& a, const Triangle& b) {
        if (a.score != b.score) return a.score > b.score;
        return std::tie(a.i, a.j, a.k) < std::tie(b.i, b.j, b.k);
    });
    std::vector<std::array<int, 3>> result;
    result.reserve(static_cast<std::size_t>(requested));
    for (const auto& item : candidates) {
        if (static_cast<int>(result.size()) >= requested) break;
        result.push_back({item.i, item.j, item.k});
    }
    for (int i = 0; static_cast<int>(result.size()) < requested && i < n; ++i) {
        for (int j = i + 1; static_cast<int>(result.size()) < requested && j < n; ++j) {
            for (int k = j + 1; static_cast<int>(result.size()) < requested && k < n; ++k) {
                const std::array<int, 3> key{i, j, k};
                if (unique.insert(key).second) result.push_back(key);
            }
        }
    }
    return result;
}

double marginal_pair_cost(
    const std::vector<double>& pair,
    const int n,
    const int cell,
    const std::vector<unsigned char>& selected,
    const Problem& problem,
    const double conflict_penalty
) {
    double result = 0.0;
    for (int other = 0; other < n; ++other) {
        if (!selected[other] || other == cell) continue;
        result += pair[static_cast<std::size_t>(cell) * n + other];
        if (conflict(problem, cell, other)) result += conflict_penalty;
    }
    return result;
}

std::vector<int> repair_layout(
    const Problem& problem,
    const std::vector<double>& pair,
    const std::vector<int>& raw,
    const double conflict_penalty,
    int& operations
) {
    const int n = problem.config().cell_count;
    const int k = problem.config().turbine_count;
    std::vector<unsigned char> selected(static_cast<std::size_t>(n), 0);
    for (const int cell : raw) selected[cell] = 1;
    operations = 0;
    auto count = [&]() {
        return std::accumulate(selected.begin(), selected.end(), 0);
    };
    while (true) {
        int remove = -1;
        double worst = -1.0;
        for (int i = 0; i < n; ++i) {
            if (!selected[i]) continue;
            bool violates = false;
            for (int j = 0; j < n; ++j) {
                if (i != j && selected[j] && conflict(problem, i, j)) {
                    violates = true;
                    break;
                }
            }
            if (!violates) continue;
            const double value = marginal_pair_cost(
                pair, n, i, selected, problem, conflict_penalty
            );
            if (value > worst || (value == worst && i > remove)) {
                worst = value;
                remove = i;
            }
        }
        if (remove < 0) break;
        selected[remove] = 0;
        ++operations;
    }
    while (count() > k) {
        int remove = -1;
        double worst = -1.0;
        for (int i = 0; i < n; ++i) {
            if (!selected[i]) continue;
            const double value = marginal_pair_cost(
                pair, n, i, selected, problem, conflict_penalty
            );
            if (value > worst || (value == worst && i > remove)) {
                worst = value;
                remove = i;
            }
        }
        if (remove < 0) throw std::runtime_error("T19 cardinality removal failed");
        selected[remove] = 0;
        ++operations;
    }
    while (count() < k) {
        int add = -1;
        double best = std::numeric_limits<double>::infinity();
        for (int i = 0; i < n; ++i) {
            if (selected[i]) continue;
            bool feasible = true;
            for (int j = 0; j < n; ++j) {
                if (selected[j] && conflict(problem, i, j)) {
                    feasible = false;
                    break;
                }
            }
            if (!feasible) continue;
            const double value = marginal_pair_cost(
                pair, n, i, selected, problem, conflict_penalty
            );
            if (value < best || (value == best && i < add)) {
                best = value;
                add = i;
            }
        }
        if (add < 0) throw std::runtime_error("T19 declared spacing repair cannot reach K");
        selected[add] = 1;
        ++operations;
    }
    std::vector<int> result;
    result.reserve(static_cast<std::size_t>(k));
    for (int i = 0; i < n; ++i) if (selected[i]) result.push_back(i);
    return result;
}

int one_swap_improve(
    const Problem& problem,
    const std::vector<double>& pair,
    std::vector<int>& layout,
    fode::PersistentExecutor& executor
) {
    const int n = problem.config().cell_count;
    std::vector<unsigned char> selected(static_cast<std::size_t>(n), 0);
    for (const int cell : layout) selected[cell] = 1;
    std::vector<double> selected_sum(static_cast<std::size_t>(n), 0.0);
    std::vector<int> conflict_count(static_cast<std::size_t>(n), 0);
    executor.parallel_for(0, n, [&](const int candidate) {
        double sum = 0.0;
        int conflicts = 0;
        for (const int occupied : layout) {
            sum += pair[static_cast<std::size_t>(candidate) * n + occupied];
            if (candidate != occupied
                && conflict(problem, candidate, occupied)) {
                ++conflicts;
            }
        }
        selected_sum[candidate] = sum;
        conflict_count[candidate] = conflicts;
    });
    struct Swap {
        double delta = 0.0;
        int remove = -1;
        int add = -1;
    };
    int operations = 0;
    for (int pass = 0; pass < n; ++pass) {
        std::vector<Swap> best_by_remove(layout.size());
        executor.parallel_for(0, static_cast<int>(layout.size()), [&](const int index) {
            const int remove = layout[static_cast<std::size_t>(index)];
            Swap best{-1.0e-14, -1, -1};
            for (int add = 0; add < n; ++add) {
                if (selected[add]) continue;
                const int remaining_conflicts = conflict_count[add]
                    - (conflict(problem, add, remove) ? 1 : 0);
                if (remaining_conflicts != 0) continue;
                const double delta = selected_sum[add]
                    - pair[static_cast<std::size_t>(add) * n + remove]
                    - selected_sum[remove];
                if (delta < best.delta || (
                    delta == best.delta
                    && std::pair(add, remove) < std::pair(best.add, best.remove)
                )) {
                    best = {delta, remove, add};
                }
            }
            best_by_remove[static_cast<std::size_t>(index)] = best;
        });
        Swap best{-1.0e-14, -1, -1};
        for (const auto& candidate : best_by_remove) {
            if (candidate.remove < 0) continue;
            if (candidate.delta < best.delta || (
                candidate.delta == best.delta
                && std::pair(candidate.add, candidate.remove)
                    < std::pair(best.add, best.remove)
            )) {
                best = candidate;
            }
        }
        if (best.remove < 0) break;
        selected[best.remove] = 0;
        selected[best.add] = 1;
        executor.parallel_for(0, n, [&](const int candidate) {
            selected_sum[candidate] +=
                pair[static_cast<std::size_t>(candidate) * n + best.add]
                - pair[static_cast<std::size_t>(candidate) * n + best.remove];
            if (candidate != best.add
                && conflict(problem, candidate, best.add)) {
                ++conflict_count[candidate];
            }
            if (candidate != best.remove
                && conflict(problem, candidate, best.remove)) {
                --conflict_count[candidate];
            }
        });
        *std::find(layout.begin(), layout.end(), best.remove) = best.add;
        std::sort(layout.begin(), layout.end());
        ++operations;
    }
    return operations;
}

}  // namespace

const char* family_name(const ProblemFamily family) noexcept {
    return family == ProblemFamily::historical ? "historical" : "realistic";
}

const char* wind_name(const WindRegime wind) noexcept {
    return wind == WindRegime::wr1 ? "wr1" : "wr36";
}

Problem::Problem(ProblemConfig config) : config_(config) {
    if (config_.cell_count != 100 && config_.cell_count != 400
        && config_.cell_count != 2500) {
        throw std::invalid_argument("T19 cell count must be 100, 400 or 2500");
    }
    if (config_.family == ProblemFamily::historical
        && config_.cell_count != 100) {
        throw std::invalid_argument("T19 historical family is 100-cell only");
    }
    if (config_.turbine_count <= 0
        || config_.turbine_count > config_.cell_count) {
        throw std::invalid_argument("T19 invalid turbine count");
    }
    const int side = static_cast<int>(std::lround(std::sqrt(config_.cell_count)));
    if (side * side != config_.cell_count) {
        throw std::invalid_argument("T19 square grid required");
    }
    const double width = config_.family == ProblemFamily::historical
        ? 200.0 : 7000.0 / static_cast<double>(side);
    cells_.reserve(static_cast<std::size_t>(config_.cell_count));
    for (int row = 0; row < side; ++row) {
        for (int column = 0; column < side; ++column) {
            cells_.push_back({
                (static_cast<double>(column) + .5) * width,
                (static_cast<double>(row) + .5) * width
            });
        }
    }
    wind_states_ = config_.wind == WindRegime::wr1
        ? std::vector<WindState>{{0.0, 12.0, 1.0}} : wr36();
}

const ProblemConfig& Problem::config() const noexcept { return config_; }

std::string Problem::semantic_id() const {
    return config_.family == ProblemFamily::historical
        ? kHistoricalProblem : kRealisticProblem;
}

const std::vector<Point>& Problem::cells() const noexcept { return cells_; }
const std::vector<WindState>& Problem::wind_states() const noexcept {
    return wind_states_;
}

double Problem::minimum_spacing_requirement_m() const noexcept {
    return 5.0 * rotor_radius(config_.family);
}

Evaluation Problem::evaluate(const std::vector<int>& layout) const {
    return evaluate_layout(*this, layout, nullptr);
}

SolveReceipt Problem::solve(const SolveConfig& config) const {
    if (config.workers <= 0 || config.maximum_iterations <= 0
        || config.time_limit_seconds <= 0.0) {
        throw std::invalid_argument("T19 invalid solve configuration");
    }
    const auto total_start = Clock::now();
    SolveReceipt receipt;
    receipt.problem_semantic_id = semantic_id();
    receipt.problem = config_;
    receipt.requested_workers = config.workers;
    receipt.maximum_iterations = config.maximum_iterations;
    receipt.time_limit_seconds = config.time_limit_seconds;
    fode::PersistentExecutor executor(config.workers);
    executor.reset_work_receipt();

    const auto interaction_start = Clock::now();
    const std::vector<double> pair = build_interaction(*this, executor);
    receipt.interaction_assembly_seconds = elapsed(interaction_start);
    const int n = config_.cell_count;
    double maximum_row_sum = 0.0;
    for (int i = 0; i < n; ++i) {
        double row_sum = 0.0;
        for (int j = 0; j < n; ++j) {
            row_sum += pair[static_cast<std::size_t>(i) * n + j];
        }
        maximum_row_sum = std::max(maximum_row_sum, row_sum);
    }
    receipt.beta = 1.01 * std::max(maximum_row_sum, 1.0e-12);
    const double conflict_penalty = receipt.beta
        * (4.0 * static_cast<double>(config_.turbine_count) + 4.0);
    receipt.requested_triplets = config.requested_triplets >= 0
        ? config.requested_triplets : (n < 2500 ? 5000 : 0);

    const auto triplet_start = Clock::now();
    const auto triplets = select_triplets(
        *this, pair, receipt.requested_triplets, executor
    );
    receipt.triplet_generation_seconds = elapsed(triplet_start);
    receipt.generated_triplets = static_cast<int>(triplets.size());

    const auto graph_start = Clock::now();
    Energy energy(n);
    for (int i = 0; i < n; ++i) energy.AddNode(2);
    const double unary_costs[2]{
        0.0, receipt.beta * (1.0 - 2.0 * config_.turbine_count)
    };
    for (int i = 0; i < n; ++i) {
        energy.AddUnaryFactor(i, const_cast<double*>(unary_costs));
    }
    double shared_costs[4]{0.0, 0.0, 0.0, 1.0};
    CompatibleSharedPairwiseFactorType shared_pairwise(2, 2, shared_costs);
    if (triplets.empty()) {
        for (int i = 0; i < n; ++i) {
            for (int j = i + 1; j < n; ++j) {
                int nodes[2]{i, j};
                double coefficient = pair[static_cast<std::size_t>(i) * n + j]
                    + 2.0 * receipt.beta;
                if (conflict(*this, i, j)) coefficient += conflict_penalty;
                energy.AddFactor(2, nodes, &coefficient, &shared_pairwise);
            }
        }
    } else {
        // SRMP v1.01's SharedPairwiseFactorType deliberately leaves its MPLP
        // message path unimplemented. AddTriplet activates that path, so the
        // paper's 100/400-cell triplet roles use source-native copied factors.
        for (int i = 0; i < n; ++i) {
            for (int j = i + 1; j < n; ++j) {
                double coefficient = pair[static_cast<std::size_t>(i) * n + j]
                    + 2.0 * receipt.beta;
                if (conflict(*this, i, j)) coefficient += conflict_penalty;
                double costs[4]{0.0, 0.0, 0.0, coefficient};
                energy.AddPairwiseFactor(i, j, costs);
            }
        }
    }
    if (!triplets.empty()) {
        // This is the source example's required order: initialize the base
        // pairwise relaxation first, then attach triplet consistency factors.
        energy.SetFullEdges();
        for (const auto& triplet : triplets) {
            energy.AddTriplet(triplet[0], triplet[1], triplet[2]);
        }
    }
    receipt.graph_assembly_seconds = elapsed(graph_start);

    Energy::Options options;
    options.method = Energy::Options::SRMP;
    options.TRWS_weighting = 1.0;
    options.iter_max = config.maximum_iterations;
    options.time_max = config.time_limit_seconds;
    options.eps = config.convergence_epsilon;
    options.compute_solution_period = 10;
    options.print_times = false;
    // Fixed insertion order is an official SRMP option and satisfies the
    // paper appendix's fixed-arbitrary-order requirement. It also avoids the
    // v1.01 quicksort's non-terminating equal-key recursion for dense triplet
    // graphs on this modern AArch64 toolchain.
    options.sort_flag = -1;
    options.verbose = false;
    const auto solver_start = Clock::now();
    receipt.srmp_lower_bound = energy.Solve(options);
    receipt.sequential_trws_seconds = elapsed(solver_start);
    std::vector<int> raw;
    for (int i = 0; i < n; ++i) {
        if (energy.GetSolution(i) == 1) raw.push_back(i);
    }
    receipt.raw_cardinality = static_cast<int>(raw.size());
    receipt.raw_augmented_energy = augmented_energy(
        *this, pair, raw, receipt.beta, conflict_penalty
    );

    const auto repair_start = Clock::now();
    receipt.layout = repair_layout(
        *this, pair, raw, conflict_penalty, receipt.repair_operations
    );
    if (config.one_swap_improvement) {
        receipt.local_swap_operations = one_swap_improve(
            *this, pair, receipt.layout, executor
        );
    }
    receipt.repaired_cardinality = static_cast<int>(receipt.layout.size());
    receipt.repaired_augmented_energy = augmented_energy(
        *this, pair, receipt.layout, receipt.beta, conflict_penalty
    );
    receipt.repair_and_local_search_seconds = elapsed(repair_start);

    const auto aep_start = Clock::now();
    receipt.evaluation = evaluate_layout(*this, receipt.layout, &executor);
    receipt.evaluation.qip_wake_objective = wake_objective(
        pair, n, receipt.layout
    );
    receipt.nonlinear_aep_seconds = elapsed(aep_start);
    receipt.end_to_end_seconds = elapsed(total_start);
    receipt.observed_workers = executor.work_receipt().distinct_participants;
    std::uint64_t hash = 0xcbf29ce484222325ULL;
    for (const int cell : receipt.layout) hash = mix_hash(hash, cell);
    hash = mix_hash(hash, std::bit_cast<std::uint64_t>(receipt.evaluation.aep_gwh));
    hash = mix_hash(hash, std::bit_cast<std::uint64_t>(receipt.evaluation.qip_wake_objective));
    receipt.scientific_hash = hash;
    return receipt;
}

std::vector<ProblemConfig> paper_roles() {
    std::vector<ProblemConfig> roles{
        {ProblemFamily::historical, WindRegime::wr1, 100, 26},
        {ProblemFamily::historical, WindRegime::wr1, 100, 30},
        {ProblemFamily::historical, WindRegime::wr36, 100, 15},
        {ProblemFamily::historical, WindRegime::wr36, 100, 39},
    };
    for (const auto wind : {WindRegime::wr1, WindRegime::wr36}) {
        for (const int k : {10,20,30,40,50,60,70,80,90,100}) {
            roles.push_back({ProblemFamily::realistic, wind, 100, k});
            roles.push_back({ProblemFamily::realistic, wind, 400, k});
            roles.push_back({ProblemFamily::realistic, wind, 2500, k});
        }
        for (int k = 120; k <= 400; k += 20) {
            roles.push_back({ProblemFamily::realistic, wind, 400, k});
        }
        for (int k = 120; k <= 280; k += 20) {
            roles.push_back({ProblemFamily::realistic, wind, 2500, k});
        }
    }
    return roles;
}

}  // namespace core99::t19
