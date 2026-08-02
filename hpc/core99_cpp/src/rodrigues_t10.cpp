/*
WFLOP IMPLEMENTATION FACT DECLARATION
Implementation unit: T10 Katic-Jensen evaluator, CHTs, MOGOMEA/o-MOGOMEA,
NSGA-II/c-NSGA-II, multi-resolution, and deterministic CPU-HPC kernels
Paper/DOI: 10.1016/j.rser.2016.07.021
Public source, missing fields, reconstruction decisions, semantic IDs, HPC
design and claim boundary: hpc/core99_cpp/include/core99/rodrigues_t10.hpp
Controlling contract: shared/contracts/core99_t10_rodrigues_2016.json
Last evidence-audit date: 2026-08-01
END WFLOP IMPLEMENTATION FACT DECLARATION
*/
#include "core99/rodrigues_t10.hpp"

#include "fode/rng.hpp"

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
#include <stdexcept>
#include <string>
#include <tuple>
#include <utility>
#include <vector>

namespace core99::t10 {
namespace {

using Clock = std::chrono::steady_clock;
constexpr double kDiameterM = 164.0;
constexpr double kRadiusM = 82.0;
constexpr double kHubHeightM = 107.0;
constexpr double kRoughnessM = 0.0005;
constexpr double kMinimumSpacingM = 8.0 * kDiameterM;
constexpr int kClusters = 5;
constexpr int kInitialClusterSize = 4;
constexpr int kInitialPopulation = kClusters * kInitialClusterSize;
constexpr double kHypervolumeImprovement = 1.0e-5;
constexpr double kCrossoverProbability = 0.9;

double seconds_since(const Clock::time_point start) {
    return std::chrono::duration<double>(Clock::now() - start).count();
}

bool occupied(const std::vector<std::uint64_t>& words, const int index) {
    return ((words[static_cast<std::size_t>(index / 64)]
             >> static_cast<unsigned>(index % 64)) & 1ULL) != 0ULL;
}

void set_occupied(
    std::vector<std::uint64_t>& words,
    const int index,
    const bool value
) {
    const std::uint64_t mask = 1ULL << static_cast<unsigned>(index % 64);
    auto& word = words[static_cast<std::size_t>(index / 64)];
    if (value) word |= mask;
    else word &= ~mask;
}

double interpolate(
    const std::array<double, 22>& values,
    const double speed
) {
    if (speed < 4.0 || speed > 25.0) return 0.0;
    if (speed >= 25.0) return values.back();
    const int lower = static_cast<int>(std::floor(speed));
    const std::size_t index = static_cast<std::size_t>(lower - 4);
    const double fraction = speed - static_cast<double>(lower);
    return values[index] * (1.0 - fraction) + values[index + 1U] * fraction;
}

constexpr std::array<double, 22> kPowerKw{
    100.0, 570.0, 1103.0, 1835.0, 2858.0, 4089.0, 5571.0,
    7105.0, 7873.0, 7986.0, 8008.0, 8008.0, 8008.0, 8008.0,
    8008.0, 8008.0, 8008.0, 8008.0, 8008.0, 8008.0, 8008.0,
    8008.0,
};

constexpr std::array<double, 22> kThrust{
    0.700000000, 0.722386304, 0.773588333, 0.773285946,
    0.767899317, 0.732727569, 0.688896343, 0.623028669,
    0.500046699, 0.373661747, 0.293230676, 0.238407400,
    0.196441644, 0.163774674, 0.137967245, 0.117309371,
    0.100578122, 0.086883163, 0.075565832, 0.066131748,
    0.058204932, 0.051495998,
};

struct WindState {
    double direction_from_deg = 0.0;
    double speed_mps = 0.0;
    double probability = 0.0;
    double flow_cosine = 0.0;
    double flow_sine = 0.0;
    double ideal_power_kw = 0.0;
    double thrust = 0.0;
};

std::vector<WindState> wind_states() {
    constexpr std::array<double, 12> speed{
        9.77, 8.34, 7.93, 10.18, 8.14, 8.24,
        9.05, 11.59, 12.11, 11.90, 10.38, 8.14,
    };
    constexpr std::array<double, 12> frequency{
        6.3, 5.9, 5.5, 7.8, 8.3, 6.5,
        11.4, 14.6, 12.1, 8.5, 6.4, 6.7,
    };
    std::vector<WindState> result;
    for (int index = 0; index < 12; ++index) {
        const double from = 30.0 * static_cast<double>(index);
        const double flow_to = (from + 180.0) * std::numbers::pi / 180.0;
        result.push_back({
            from,
            speed[static_cast<std::size_t>(index)],
            frequency[static_cast<std::size_t>(index)] / 100.0,
            std::cos(flow_to),
            std::sin(flow_to),
            interpolate(kPowerKw, speed[static_cast<std::size_t>(index)]),
            interpolate(kThrust, speed[static_cast<std::size_t>(index)]),
        });
    }
    return result;
}

double circle_overlap(
    const double first_radius,
    const double second_radius,
    const double centre_distance
) {
    if (centre_distance >= first_radius + second_radius) return 0.0;
    if (centre_distance <= std::abs(first_radius - second_radius)) {
        const double radius = std::min(first_radius, second_radius);
        return std::numbers::pi * radius * radius;
    }
    const double first_cosine = std::clamp(
        (first_radius * first_radius + centre_distance * centre_distance
         - second_radius * second_radius)
        / (2.0 * first_radius * centre_distance),
        -1.0,
        1.0
    );
    const double second_cosine = std::clamp(
        (second_radius * second_radius + centre_distance * centre_distance
         - first_radius * first_radius)
        / (2.0 * second_radius * centre_distance),
        -1.0,
        1.0
    );
    const double first_angle = 2.0 * std::acos(first_cosine);
    const double second_angle = 2.0 * std::acos(second_cosine);
    return 0.5 * first_radius * first_radius
            * (first_angle - std::sin(first_angle))
        + 0.5 * second_radius * second_radius
            * (second_angle - std::sin(second_angle));
}

std::uint64_t hash_mix(std::uint64_t state, const std::uint64_t value) {
    state ^= value + 0x9e3779b97f4a7c15ULL + (state << 6U) + (state >> 2U);
    return state;
}

struct Individual {
    std::vector<std::uint64_t> layout;
    Evaluation evaluation;
    int rank = 0;
    double crowding = 0.0;
};

bool same_objectives(const Evaluation& left, const Evaluation& right) {
    return std::bit_cast<std::uint64_t>(left.normalized_energy)
            == std::bit_cast<std::uint64_t>(right.normalized_energy)
        && std::bit_cast<std::uint64_t>(left.efficiency)
            == std::bit_cast<std::uint64_t>(right.efficiency);
}

bool dominates(const Evaluation& left, const Evaluation& right) {
    const bool no_worse = left.normalized_energy >= right.normalized_energy
        && left.efficiency >= right.efficiency;
    const bool strict = left.normalized_energy > right.normalized_energy
        || left.efficiency > right.efficiency;
    return no_worse && strict;
}

std::vector<std::vector<int>> assign_rank(
    std::vector<Individual>& population,
    fode::PersistentExecutor& executor
) {
    const int count = static_cast<int>(population.size());
    std::vector<std::vector<int>> outgoing(static_cast<std::size_t>(count));
    std::vector<int> incoming(static_cast<std::size_t>(count), 0);
    if (count > 0) {
        executor.parallel_for(0, count, [&](const int left) {
            auto& row = outgoing[static_cast<std::size_t>(left)];
            int degree = 0;
            for (int right = 0; right < count; ++right) {
                if (left == right) continue;
                if (dominates(
                        population[static_cast<std::size_t>(left)].evaluation,
                        population[static_cast<std::size_t>(right)].evaluation
                    )) {
                    row.push_back(right);
                } else if (dominates(
                        population[static_cast<std::size_t>(right)].evaluation,
                        population[static_cast<std::size_t>(left)].evaluation
                    )) {
                    ++degree;
                }
            }
            incoming[static_cast<std::size_t>(left)] = degree;
        });
    }
    std::vector<std::vector<int>> fronts;
    std::vector<int> current;
    for (int index = 0; index < count; ++index) {
        if (incoming[static_cast<std::size_t>(index)] == 0) {
            population[static_cast<std::size_t>(index)].rank = 1;
            current.push_back(index);
        }
    }
    int rank = 1;
    while (!current.empty()) {
        fronts.push_back(current);
        std::vector<int> next;
        for (const int source : current) {
            for (const int target : outgoing[static_cast<std::size_t>(source)]) {
                int& degree = incoming[static_cast<std::size_t>(target)];
                --degree;
                if (degree == 0) {
                    population[static_cast<std::size_t>(target)].rank = rank + 1;
                    next.push_back(target);
                }
            }
        }
        current = std::move(next);
        ++rank;
    }
    return fronts;
}

void assign_crowding(
    std::vector<Individual>& population,
    const std::vector<int>& front
) {
    for (const int index : front) {
        population[static_cast<std::size_t>(index)].crowding = 0.0;
    }
    if (front.size() <= 2U) {
        for (const int index : front) {
            population[static_cast<std::size_t>(index)].crowding =
                std::numeric_limits<double>::infinity();
        }
        return;
    }
    for (int objective = 0; objective < 2; ++objective) {
        std::vector<int> order = front;
        std::stable_sort(order.begin(), order.end(), [&](const int a, const int b) {
            const auto value = [&](const int index) {
                const Evaluation& evaluation =
                    population[static_cast<std::size_t>(index)].evaluation;
                return objective == 0 ? evaluation.normalized_energy
                                      : evaluation.efficiency;
            };
            if (value(a) != value(b)) return value(a) < value(b);
            return a < b;
        });
        population[static_cast<std::size_t>(order.front())].crowding =
            std::numeric_limits<double>::infinity();
        population[static_cast<std::size_t>(order.back())].crowding =
            std::numeric_limits<double>::infinity();
        const auto value = [&](const int index) {
            const Evaluation& evaluation =
                population[static_cast<std::size_t>(index)].evaluation;
            return objective == 0 ? evaluation.normalized_energy
                                  : evaluation.efficiency;
        };
        const double range = value(order.back()) - value(order.front());
        if (!(range > 0.0)) continue;
        for (std::size_t index = 1; index + 1U < order.size(); ++index) {
            auto& item = population[static_cast<std::size_t>(order[index])];
            if (std::isfinite(item.crowding)) {
                item.crowding += (value(order[index + 1U])
                                  - value(order[index - 1U])) / range;
            }
        }
    }
}

void rank_and_crowding(
    std::vector<Individual>& population,
    fode::PersistentExecutor& executor
) {
    const auto fronts = assign_rank(population, executor);
    for (const auto& front : fronts) assign_crowding(population, front);
}

double hypervolume(std::vector<Individual> archive) {
    std::stable_sort(
        archive.begin(), archive.end(),
        [](const Individual& left, const Individual& right) {
            if (left.evaluation.normalized_energy
                != right.evaluation.normalized_energy) {
                return left.evaluation.normalized_energy
                    < right.evaluation.normalized_energy;
            }
            return left.evaluation.efficiency > right.evaluation.efficiency;
        }
    );
    std::vector<double> suffix(archive.size(), 0.0);
    for (std::size_t reverse = archive.size(); reverse > 0U; --reverse) {
        const std::size_t index = reverse - 1U;
        suffix[index] = std::clamp(
            archive[index].evaluation.efficiency, 0.0, 1.0
        );
        if (index + 1U < suffix.size()) {
            suffix[index] = std::max(suffix[index], suffix[index + 1U]);
        }
    }
    double result = 0.0;
    double previous_energy = 0.0;
    for (std::size_t index = 0; index < archive.size(); ++index) {
        const double energy = std::clamp(
            archive[index].evaluation.normalized_energy, 0.0, 1.0
        );
        if (energy > previous_energy) {
            result += (energy - previous_energy) * suffix[index];
            previous_energy = energy;
        }
    }
    return result;
}

void update_archive(std::vector<Individual>& archive, Individual candidate) {
    for (const auto& member : archive) {
        if (member.layout == candidate.layout
            || same_objectives(member.evaluation, candidate.evaluation)
            || dominates(member.evaluation, candidate.evaluation)) {
            return;
        }
    }
    archive.erase(
        std::remove_if(
            archive.begin(), archive.end(),
            [&](const Individual& member) {
                return dominates(candidate.evaluation, member.evaluation);
            }
        ),
        archive.end()
    );
    archive.push_back(std::move(candidate));
}

std::vector<int> nondominated_indices(const std::vector<Individual>& population) {
    std::vector<int> result;
    for (int candidate = 0; candidate < static_cast<int>(population.size()); ++candidate) {
        bool dominated_by_other = false;
        for (int other = 0; other < static_cast<int>(population.size()); ++other) {
            if (other != candidate && dominates(
                    population[static_cast<std::size_t>(other)].evaluation,
                    population[static_cast<std::size_t>(candidate)].evaluation
                )) {
                dominated_by_other = true;
                break;
            }
        }
        if (!dominated_by_other) result.push_back(candidate);
    }
    return result;
}

struct Clustering {
    std::vector<std::vector<int>> members;
    std::vector<int> assignment;
};

Clustering cluster_population(
    const std::vector<Individual>& population,
    const fode::CounterRng& rng,
    const std::uint64_t generation
) {
    const int count = static_cast<int>(population.size());
    Clustering result;
    result.members.resize(kClusters);
    result.assignment.assign(static_cast<std::size_t>(count), 0);
    if (count == 0) return result;
    const auto front = nondominated_indices(population);
    const std::vector<int>& pool = front.empty()
        ? [&]() -> const std::vector<int>& {
            static thread_local std::vector<int> all;
            all.resize(static_cast<std::size_t>(count));
            std::iota(all.begin(), all.end(), 0);
            return all;
        }()
        : front;
    double energy_min = std::numeric_limits<double>::infinity();
    double energy_max = -std::numeric_limits<double>::infinity();
    double efficiency_min = std::numeric_limits<double>::infinity();
    double efficiency_max = -std::numeric_limits<double>::infinity();
    for (const auto& item : population) {
        energy_min = std::min(energy_min, item.evaluation.normalized_energy);
        energy_max = std::max(energy_max, item.evaluation.normalized_energy);
        efficiency_min = std::min(efficiency_min, item.evaluation.efficiency);
        efficiency_max = std::max(efficiency_max, item.evaluation.efficiency);
    }
    const auto normalized = [&](const int index) {
        const Evaluation& e = population[static_cast<std::size_t>(index)].evaluation;
        return std::pair{
            (e.normalized_energy - energy_min)
                / std::max(1.0e-15, energy_max - energy_min),
            (e.efficiency - efficiency_min)
                / std::max(1.0e-15, efficiency_max - efficiency_min),
        };
    };
    std::vector<int> leaders;
    const int first_objective = rng.integer(0, 2, generation, 101, 0);
    leaders.push_back(*std::max_element(
        pool.begin(), pool.end(), [&](const int left, const int right) {
            const auto l = normalized(left);
            const auto r = normalized(right);
            const double lv = first_objective == 0 ? l.first : l.second;
            const double rv = first_objective == 0 ? r.first : r.second;
            if (lv != rv) return lv < rv;
            return left > right;
        }
    ));
    while (static_cast<int>(leaders.size()) < kClusters) {
        int best = pool.front();
        double best_distance = -1.0;
        for (const int candidate : pool) {
            if (std::find(leaders.begin(), leaders.end(), candidate) != leaders.end()) {
                continue;
            }
            const auto point = normalized(candidate);
            double nearest = std::numeric_limits<double>::infinity();
            for (const int leader : leaders) {
                const auto reference = normalized(leader);
                nearest = std::min(
                    nearest,
                    std::hypot(point.first - reference.first,
                               point.second - reference.second)
                );
            }
            if (nearest > best_distance
                || (nearest == best_distance && candidate < best)) {
                best = candidate;
                best_distance = nearest;
            }
        }
        if (std::find(leaders.begin(), leaders.end(), best) != leaders.end()) {
            best = static_cast<int>(leaders.size()) % count;
        }
        leaders.push_back(best);
    }
    const int cluster_size = std::max(
        kInitialClusterSize,
        2 * static_cast<int>(std::floor(0.5 * static_cast<double>(count)))
            / kClusters
    );
    std::vector<std::pair<double, double>> means(kClusters);
    for (int cluster = 0; cluster < kClusters; ++cluster) {
        const auto leader = normalized(leaders[static_cast<std::size_t>(cluster)]);
        std::vector<std::pair<double, int>> distance;
        distance.reserve(population.size());
        for (int index = 0; index < count; ++index) {
            const auto point = normalized(index);
            distance.emplace_back(
                std::hypot(point.first - leader.first,
                           point.second - leader.second),
                index
            );
        }
        std::stable_sort(distance.begin(), distance.end());
        const int take = std::min(cluster_size, count);
        for (int position = 0; position < take; ++position) {
            const int index = distance[static_cast<std::size_t>(position)].second;
            result.members[static_cast<std::size_t>(cluster)].push_back(index);
            const auto point = normalized(index);
            means[static_cast<std::size_t>(cluster)].first += point.first;
            means[static_cast<std::size_t>(cluster)].second += point.second;
        }
        means[static_cast<std::size_t>(cluster)].first /= static_cast<double>(take);
        means[static_cast<std::size_t>(cluster)].second /= static_cast<double>(take);
    }
    for (int index = 0; index < count; ++index) {
        std::vector<int> membership;
        for (int cluster = 0; cluster < kClusters; ++cluster) {
            const auto& members = result.members[static_cast<std::size_t>(cluster)];
            if (std::find(members.begin(), members.end(), index) != members.end()) {
                membership.push_back(cluster);
            }
        }
        int selected = 0;
        if (!membership.empty()) {
            selected = membership[static_cast<std::size_t>(rng.integer(
                0, static_cast<int>(membership.size()), generation, 102,
                static_cast<std::uint64_t>(index)
            ))];
        } else {
            const auto point = normalized(index);
            double best = std::numeric_limits<double>::infinity();
            for (int cluster = 0; cluster < kClusters; ++cluster) {
                const auto mean = means[static_cast<std::size_t>(cluster)];
                const double distance = std::hypot(
                    point.first - mean.first, point.second - mean.second
                );
                if (distance < best) {
                    best = distance;
                    selected = cluster;
                }
            }
        }
        result.assignment[static_cast<std::size_t>(index)] = selected;
    }
    return result;
}

struct LinkageTree {
    std::vector<std::vector<int>> subsets;
};

std::size_t triangular_index(const int high, const int low) {
    return static_cast<std::size_t>(high) * static_cast<std::size_t>(high - 1) / 2U
        + static_cast<std::size_t>(low);
}

LinkageTree build_linkage_tree(
    const PaperCase& paper_case,
    const std::vector<Individual>& population,
    const std::vector<int>& cluster,
    const bool geographic
) {
    const int variables = paper_case.variables;
    LinkageTree result;
    if (variables <= 1) return result;
    result.subsets.reserve(static_cast<std::size_t>(2 * variables - 2));
    for (int variable = 0; variable < variables; ++variable) {
        result.subsets.push_back({variable});
    }
    const int nodes = 2 * variables - 1;
    std::vector<double> distances(
        static_cast<std::size_t>(nodes) * static_cast<std::size_t>(nodes - 1) / 2U,
        0.0
    );
    const auto distance_at = [&](const int left, const int right) -> double& {
        const int high = std::max(left, right);
        const int low = std::min(left, right);
        return distances[triangular_index(high, low)];
    };
    for (int left = 1; left < variables; ++left) {
        const int left_row = left / paper_case.side_points;
        const int left_column = left % paper_case.side_points;
        for (int right = 0; right < left; ++right) {
            if (geographic) {
                const int right_row = right / paper_case.side_points;
                const int right_column = right % paper_case.side_points;
                distance_at(left, right) = std::hypot(
                    static_cast<double>(left_row - right_row),
                    static_cast<double>(left_column - right_column)
                );
                continue;
            }
            int n00 = 0;
            int n01 = 0;
            int n10 = 0;
            int n11 = 0;
            for (const int member : cluster) {
                const auto& words = population[static_cast<std::size_t>(member)].layout;
                const bool a = occupied(words, left);
                const bool b = occupied(words, right);
                if (a && b) ++n11;
                else if (a) ++n10;
                else if (b) ++n01;
                else ++n00;
            }
            const double sample = static_cast<double>(
                std::max(1, n00 + n01 + n10 + n11)
            );
            const double pa1 = static_cast<double>(n10 + n11) / sample;
            const double pb1 = static_cast<double>(n01 + n11) / sample;
            double mutual_information = 0.0;
            const auto add = [&](const int count, const double pa, const double pb) {
                if (count == 0 || !(pa > 0.0) || !(pb > 0.0)) return 0.0;
                const double joint = static_cast<double>(count) / sample;
                return joint * std::log(joint / (pa * pb));
            };
            mutual_information += add(n00, 1.0 - pa1, 1.0 - pb1);
            mutual_information += add(n01, 1.0 - pa1, pb1);
            mutual_information += add(n10, pa1, 1.0 - pb1);
            mutual_information += add(n11, pa1, pb1);
            distance_at(left, right) = std::max(
                0.0, 1.0 - mutual_information / std::numbers::ln2
            );
        }
    }
    std::vector<bool> active(static_cast<std::size_t>(nodes), false);
    std::vector<int> weight(static_cast<std::size_t>(nodes), 0);
    std::vector<std::vector<int>> members(static_cast<std::size_t>(nodes));
    for (int index = 0; index < variables; ++index) {
        active[static_cast<std::size_t>(index)] = true;
        weight[static_cast<std::size_t>(index)] = 1;
        members[static_cast<std::size_t>(index)] = {index};
    }
    int next_node = variables;
    std::vector<int> chain;
    while (next_node < nodes) {
        if (chain.empty()) {
            for (int index = 0; index < next_node; ++index) {
                if (active[static_cast<std::size_t>(index)]) {
                    chain.push_back(index);
                    break;
                }
            }
        }
        const int source = chain.back();
        int nearest = -1;
        double best = std::numeric_limits<double>::infinity();
        for (int candidate = 0; candidate < next_node; ++candidate) {
            if (candidate == source || !active[static_cast<std::size_t>(candidate)]) {
                continue;
            }
            const double value = distance_at(source, candidate);
            if (value < best || (value == best && candidate < nearest)) {
                best = value;
                nearest = candidate;
            }
        }
        if (nearest < 0) throw std::runtime_error("T10 linkage tree stalled");
        if (chain.size() >= 2U && nearest == chain[chain.size() - 2U]) {
            const int left = source;
            const int right = nearest;
            members[static_cast<std::size_t>(next_node)] =
                members[static_cast<std::size_t>(right)];
            auto& merged = members[static_cast<std::size_t>(next_node)];
            merged.insert(
                merged.end(),
                members[static_cast<std::size_t>(left)].begin(),
                members[static_cast<std::size_t>(left)].end()
            );
            std::sort(merged.begin(), merged.end());
            weight[static_cast<std::size_t>(next_node)] =
                weight[static_cast<std::size_t>(left)]
                + weight[static_cast<std::size_t>(right)];
            for (int candidate = 0; candidate < next_node; ++candidate) {
                if (!active[static_cast<std::size_t>(candidate)]
                    || candidate == left || candidate == right) continue;
                distance_at(next_node, candidate) = (
                    static_cast<double>(weight[static_cast<std::size_t>(left)])
                        * distance_at(left, candidate)
                    + static_cast<double>(weight[static_cast<std::size_t>(right)])
                        * distance_at(right, candidate)
                ) / static_cast<double>(
                    weight[static_cast<std::size_t>(left)]
                    + weight[static_cast<std::size_t>(right)]
                );
            }
            active[static_cast<std::size_t>(left)] = false;
            active[static_cast<std::size_t>(right)] = false;
            active[static_cast<std::size_t>(next_node)] = true;
            if (next_node != nodes - 1) result.subsets.push_back(merged);
            chain.clear();
            ++next_node;
        } else {
            chain.push_back(nearest);
        }
    }
    return result;
}

}  // namespace

struct Problem::Data {
    PaperCase paper_case;
    std::vector<std::pair<double, double>> coordinates;
    std::vector<std::pair<int, int>> conflicts;
    std::vector<WindState> winds;
    double wake_expansion = 0.0;
    double ideal_expected_power_kw = 0.0;
};

namespace {

PaperCase make_case(const std::string& case_id) {
    if (case_id.size() < 7U || case_id.substr(0, 4) != "t10_"
        || case_id[5] != '_') {
        throw std::invalid_argument("unknown T10 case " + case_id);
    }
    const char farm = case_id[4];
    const int step = std::stoi(case_id.substr(6));
    int q8 = 0;
    int maximum = 0;
    double area = 0.0;
    if (farm == 'A') { q8 = 4; maximum = 16; area = 15.49; }
    else if (farm == 'B') { q8 = 7; maximum = 49; area = 61.97; }
    else if (farm == 'C') { q8 = 10; maximum = 100; area = 139.43; }
    else if (farm == 'D') { q8 = 13; maximum = 169; area = 247.89; }
    else throw std::invalid_argument("unknown T10 wind farm " + case_id);
    if (step != 2 && step != 4 && step != 8) {
        throw std::invalid_argument("unknown T10 grid step " + case_id);
    }
    const int factor = 8 / step;
    const int side_points = factor * (q8 - 1) + 1;
    const double step_m = static_cast<double>(step) * kDiameterM;
    const double side_m = static_cast<double>(side_points - 1) * step_m;
    return {
        case_id, farm, step, side_points, side_points * side_points,
        maximum, side_m, step_m, area,
    };
}

Evaluation evaluate_serial(
    const Problem::Data& data,
    const std::vector<std::uint64_t>& layout,
    const ConstraintHandling constraint
) {
    const PaperCase& paper_case = data.paper_case;
    Evaluation result;
    for (const auto& [left, right] : data.conflicts) {
        if (occupied(layout, left) && occupied(layout, right)) {
            ++result.violating_pairs;
        }
    }
    double expected_power = 0.0;
    std::vector<int> turbines;
    turbines.reserve(static_cast<std::size_t>(paper_case.maximum_packing));
    for (int variable = 0; variable < paper_case.variables; ++variable) {
        if (occupied(layout, variable)) turbines.push_back(variable);
    }
    result.occupied_turbines = static_cast<int>(turbines.size());
    if (result.occupied_turbines == 0) {
        result.physically_evaluated = true;
        return result;
    }
    std::vector<double> along(turbines.size());
    std::vector<double> across(turbines.size());
    for (const WindState& wind : data.winds) {
        for (std::size_t index = 0; index < turbines.size(); ++index) {
            const auto [x, y] = data.coordinates[
                static_cast<std::size_t>(turbines[index])
            ];
            along[index] = wind.flow_cosine * x + wind.flow_sine * y;
            across[index] = -wind.flow_sine * x + wind.flow_cosine * y;
        }
        double state_power = 0.0;
        for (std::size_t target = 0; target < turbines.size(); ++target) {
            double sum_squared_deficit = 0.0;
            for (std::size_t source = 0; source < turbines.size(); ++source) {
                if (source == target) continue;
                const double downstream = along[target] - along[source];
                if (!(downstream > 0.0)) continue;
                const double crosswind = std::abs(across[target] - across[source]);
                const double wake_radius = kRadiusM
                    + data.wake_expansion * downstream;
                const double overlap = circle_overlap(
                    wake_radius, kRadiusM, crosswind
                );
                if (!(overlap > 0.0)) continue;
                const double denominator = 1.0
                    + data.wake_expansion * downstream / kRadiusM;
                const double deficit = (
                    1.0 - std::sqrt(std::max(0.0, 1.0 - wind.thrust))
                ) / (denominator * denominator)
                    * overlap / (std::numbers::pi * kRadiusM * kRadiusM);
                sum_squared_deficit += deficit * deficit;
            }
            const double effective_speed = wind.speed_mps * std::max(
                0.0, 1.0 - std::sqrt(sum_squared_deficit)
            );
            state_power += interpolate(kPowerKw, effective_speed);
        }
        expected_power += wind.probability * state_power;
    }
    double energy_numerator = expected_power;
    if (constraint == ConstraintHandling::penalty) {
        energy_numerator -= static_cast<double>(result.violating_pairs)
            * data.ideal_expected_power_kw;
    }
    result.normalized_energy = energy_numerator
        / (static_cast<double>(paper_case.maximum_packing)
           * data.ideal_expected_power_kw);
    result.efficiency = energy_numerator
        / (static_cast<double>(result.occupied_turbines)
           * data.ideal_expected_power_kw);
    result.physically_evaluated = true;
    return result;
}

}  // namespace

Problem::Problem(std::string case_id) : data_(std::make_shared<Data>()) {
    auto mutable_data = std::const_pointer_cast<Data>(data_);
    mutable_data->paper_case = make_case(case_id);
    const PaperCase& paper_case = mutable_data->paper_case;
    mutable_data->coordinates.reserve(static_cast<std::size_t>(paper_case.variables));
    for (int row = 0; row < paper_case.side_points; ++row) {
        for (int column = 0; column < paper_case.side_points; ++column) {
            mutable_data->coordinates.emplace_back(
                static_cast<double>(column) * paper_case.step_m,
                static_cast<double>(row) * paper_case.step_m
            );
        }
    }
    const double minimum_squared = kMinimumSpacingM * kMinimumSpacingM;
    for (int left = 0; left < paper_case.variables; ++left) {
        const auto [lx, ly] = mutable_data->coordinates[static_cast<std::size_t>(left)];
        for (int right = left + 1; right < paper_case.variables; ++right) {
            const auto [rx, ry] = mutable_data->coordinates[static_cast<std::size_t>(right)];
            const double dx = lx - rx;
            const double dy = ly - ry;
            if (dx * dx + dy * dy < minimum_squared - 1.0e-9) {
                mutable_data->conflicts.emplace_back(left, right);
            }
        }
    }
    mutable_data->winds = wind_states();
    mutable_data->wake_expansion = 0.5 / std::log(kHubHeightM / kRoughnessM);
    for (const WindState& wind : mutable_data->winds) {
        mutable_data->ideal_expected_power_kw +=
            wind.probability * wind.ideal_power_kw;
    }
}

const PaperCase& Problem::paper_case() const noexcept {
    return data_->paper_case;
}

int Problem::word_count() const noexcept {
    return (data_->paper_case.variables + 63) / 64;
}

const std::vector<std::pair<int, int>>& Problem::conflicts() const noexcept {
    return data_->conflicts;
}

Evaluation Problem::evaluate(
    const std::vector<std::uint64_t>& layout,
    const ConstraintHandling constraint,
    fode::PersistentExecutor& executor
) const {
    if (static_cast<int>(layout.size()) != word_count()) {
        throw std::invalid_argument("T10 layout word count mismatch");
    }
    const int wind_count = static_cast<int>(data_->winds.size());
    std::vector<double> state_power(static_cast<std::size_t>(wind_count), 0.0);
    int turbines = 0;
    int violations = 0;
    for (const auto& [left, right] : data_->conflicts) {
        if (occupied(layout, left) && occupied(layout, right)) ++violations;
    }
    std::vector<int> indices;
    indices.reserve(static_cast<std::size_t>(data_->paper_case.maximum_packing));
    for (int variable = 0; variable < data_->paper_case.variables; ++variable) {
        if (occupied(layout, variable)) indices.push_back(variable);
    }
    turbines = static_cast<int>(indices.size());
    executor.parallel_for(0, wind_count, [&](const int state_index) {
        const WindState& wind = data_->winds[static_cast<std::size_t>(state_index)];
        std::vector<double> along(indices.size());
        std::vector<double> across(indices.size());
        for (std::size_t index = 0; index < indices.size(); ++index) {
            const auto [x, y] = data_->coordinates[
                static_cast<std::size_t>(indices[index])
            ];
            along[index] = wind.flow_cosine * x + wind.flow_sine * y;
            across[index] = -wind.flow_sine * x + wind.flow_cosine * y;
        }
        double power = 0.0;
        for (std::size_t target = 0; target < indices.size(); ++target) {
            double squared = 0.0;
            for (std::size_t source = 0; source < indices.size(); ++source) {
                if (source == target) continue;
                const double downstream = along[target] - along[source];
                if (!(downstream > 0.0)) continue;
                const double crosswind = std::abs(across[target] - across[source]);
                const double wake_radius = kRadiusM
                    + data_->wake_expansion * downstream;
                const double overlap = circle_overlap(wake_radius, kRadiusM, crosswind);
                if (!(overlap > 0.0)) continue;
                const double denominator = 1.0
                    + data_->wake_expansion * downstream / kRadiusM;
                const double deficit = (
                    1.0 - std::sqrt(std::max(0.0, 1.0 - wind.thrust))
                ) / (denominator * denominator)
                    * overlap / (std::numbers::pi * kRadiusM * kRadiusM);
                squared += deficit * deficit;
            }
            power += interpolate(
                kPowerKw,
                wind.speed_mps * std::max(0.0, 1.0 - std::sqrt(squared))
            );
        }
        state_power[static_cast<std::size_t>(state_index)] = wind.probability * power;
    });
    Evaluation result;
    result.occupied_turbines = turbines;
    result.violating_pairs = violations;
    result.physically_evaluated = true;
    if (turbines == 0) return result;
    double expected = std::accumulate(state_power.begin(), state_power.end(), 0.0);
    if (constraint == ConstraintHandling::penalty) {
        expected -= static_cast<double>(violations) * data_->ideal_expected_power_kw;
    }
    result.normalized_energy = expected
        / (static_cast<double>(data_->paper_case.maximum_packing)
           * data_->ideal_expected_power_kw);
    result.efficiency = expected
        / (static_cast<double>(turbines) * data_->ideal_expected_power_kw);
    return result;
}

std::vector<Evaluation> Problem::evaluate_population(
    const std::vector<std::vector<std::uint64_t>>& layouts,
    const ConstraintHandling constraint,
    fode::PersistentExecutor& executor
) const {
    std::vector<Evaluation> result(layouts.size());
    executor.parallel_for(0, static_cast<int>(layouts.size()), [&](const int index) {
        const auto& layout = layouts[static_cast<std::size_t>(index)];
        if (static_cast<int>(layout.size()) != word_count()) {
            throw std::invalid_argument("T10 population layout word mismatch");
        }
        result[static_cast<std::size_t>(index)] =
            evaluate_serial(*data_, layout, constraint);
    });
    return result;
}

Evaluation Problem::evaluate_direct(
    const std::vector<std::uint64_t>& layout,
    const ConstraintHandling constraint
) const {
    if (static_cast<int>(layout.size()) != word_count()) {
        throw std::invalid_argument("T10 direct layout word mismatch");
    }
    return evaluate_serial(*data_, layout, constraint);
}

std::vector<std::uint64_t> Problem::initial_layout(
    const std::uint64_t seed,
    const std::uint64_t individual
) const {
    const fode::CounterRng rng(seed);
    std::vector<std::uint64_t> result(static_cast<std::size_t>(word_count()), 0ULL);
    const int desired = rng.integer(
        1, data_->paper_case.maximum_packing + 1, 0, 201, individual
    );
    int first = rng.integer(
        0, data_->paper_case.variables, 0, 202, individual
    );
    set_occupied(result, first, true);
    std::vector<double> nearest(
        static_cast<std::size_t>(data_->paper_case.variables), 0.0
    );
    const auto [first_x, first_y] =
        data_->coordinates[static_cast<std::size_t>(first)];
    for (int candidate = 0; candidate < data_->paper_case.variables; ++candidate) {
        const auto [x, y] = data_->coordinates[static_cast<std::size_t>(candidate)];
        nearest[static_cast<std::size_t>(candidate)] =
            std::hypot(x - first_x, y - first_y);
    }
    for (int selected = 1; selected < desired; ++selected) {
        int best = -1;
        double best_distance = -1.0;
        for (int candidate = 0; candidate < data_->paper_case.variables; ++candidate) {
            if (occupied(result, candidate)) continue;
            const double local_nearest = nearest[static_cast<std::size_t>(candidate)];
            if (local_nearest >= kMinimumSpacingM - 1.0e-9
                && (local_nearest > best_distance
                    || (local_nearest == best_distance && candidate < best))) {
                best = candidate;
                best_distance = local_nearest;
            }
        }
        if (best < 0) break;
        set_occupied(result, best, true);
        const auto [best_x, best_y] =
            data_->coordinates[static_cast<std::size_t>(best)];
        for (int candidate = 0; candidate < data_->paper_case.variables; ++candidate) {
            if (occupied(result, candidate)) continue;
            const auto [x, y] =
                data_->coordinates[static_cast<std::size_t>(candidate)];
            nearest[static_cast<std::size_t>(candidate)] = std::min(
                nearest[static_cast<std::size_t>(candidate)],
                std::hypot(x - best_x, y - best_y)
            );
        }
    }
    return result;
}

std::vector<std::uint64_t> Problem::repair(
    std::vector<std::uint64_t> layout,
    const std::uint64_t seed,
    const std::uint64_t event
) const {
    const fode::CounterRng rng(seed);
    std::uint64_t draw = 0;
    while (true) {
        int chosen_left = -1;
        int chosen_right = -1;
        for (const auto& [left, right] : data_->conflicts) {
            if (occupied(layout, left) && occupied(layout, right)) {
                chosen_left = left;
                chosen_right = right;
                break;
            }
        }
        if (chosen_left < 0) break;
        const bool remove_left = rng.uniform(0, 203, event, draw++) < 0.5;
        set_occupied(layout, remove_left ? chosen_left : chosen_right, false);
    }
    bool any = false;
    for (int variable = 0; variable < data_->paper_case.variables; ++variable) {
        any = any || occupied(layout, variable);
    }
    if (!any) {
        set_occupied(
            layout,
            rng.integer(0, data_->paper_case.variables, 0, 204, event),
            true
        );
    }
    return layout;
}

int Problem::violation_count(const std::vector<std::uint64_t>& layout) const noexcept {
    int result = 0;
    for (const auto& [left, right] : data_->conflicts) {
        if (occupied(layout, left) && occupied(layout, right)) ++result;
    }
    return result;
}

bool Problem::feasible(const std::vector<std::uint64_t>& layout) const noexcept {
    return violation_count(layout) == 0;
}

namespace {

bool archive_dominates(
    const std::vector<Individual>& archive,
    const Evaluation& candidate
) {
    return std::any_of(
        archive.begin(), archive.end(),
        [&](const Individual& member) {
            return dominates(member.evaluation, candidate);
        }
    );
}

std::vector<Individual> survivor_selection(
    std::vector<Individual> candidates,
    const int target,
    fode::PersistentExecutor& executor
) {
    if (static_cast<int>(candidates.size()) <= target) return candidates;
    const auto fronts = assign_rank(candidates, executor);
    std::vector<Individual> result;
    result.reserve(static_cast<std::size_t>(target));
    for (const auto& front : fronts) {
        assign_crowding(candidates, front);
        if (result.size() + front.size() <= static_cast<std::size_t>(target)) {
            for (const int index : front) {
                result.push_back(std::move(candidates[static_cast<std::size_t>(index)]));
            }
            continue;
        }
        std::vector<int> order = front;
        std::stable_sort(order.begin(), order.end(), [&](const int left, const int right) {
            const auto& a = candidates[static_cast<std::size_t>(left)];
            const auto& b = candidates[static_cast<std::size_t>(right)];
            if (a.crowding != b.crowding) return a.crowding > b.crowding;
            return left < right;
        });
        for (const int index : order) {
            if (static_cast<int>(result.size()) == target) break;
            result.push_back(std::move(candidates[static_cast<std::size_t>(index)]));
        }
        break;
    }
    return result;
}

std::uint64_t population_hash(const std::vector<Individual>& archive) {
    std::vector<const Individual*> order;
    order.reserve(archive.size());
    for (const auto& item : archive) order.push_back(&item);
    std::stable_sort(order.begin(), order.end(), [](const auto* left, const auto* right) {
        if (left->evaluation.normalized_energy
            != right->evaluation.normalized_energy) {
            return left->evaluation.normalized_energy
                < right->evaluation.normalized_energy;
        }
        if (left->evaluation.efficiency != right->evaluation.efficiency) {
            return left->evaluation.efficiency < right->evaluation.efficiency;
        }
        return left->layout < right->layout;
    });
    std::uint64_t result = 0xcbf29ce484222325ULL;
    for (const Individual* item : order) {
        result = hash_mix(result, std::bit_cast<std::uint64_t>(
            item->evaluation.normalized_energy
        ));
        result = hash_mix(result, std::bit_cast<std::uint64_t>(
            item->evaluation.efficiency
        ));
        result = hash_mix(result, static_cast<std::uint64_t>(
            item->evaluation.occupied_turbines
        ));
        for (const std::uint64_t word : item->layout) {
            result = hash_mix(result, word);
        }
    }
    return result;
}

int tournament(
    const std::vector<Individual>& population,
    const fode::CounterRng& rng,
    const std::uint64_t generation,
    const std::uint64_t individual,
    const std::uint64_t draw,
    const std::vector<int>* candidate_pool = nullptr
) {
    const int pool_size = candidate_pool == nullptr
        ? static_cast<int>(population.size())
        : static_cast<int>(candidate_pool->size());
    const int first_position = rng.integer(
        0, pool_size, generation, 301, individual, 2U * draw
    );
    const int second_position = rng.integer(
        0, pool_size, generation, 301, individual, 2U * draw + 1U
    );
    const int first = candidate_pool == nullptr
        ? first_position
        : (*candidate_pool)[static_cast<std::size_t>(first_position)];
    const int second = candidate_pool == nullptr
        ? second_position
        : (*candidate_pool)[static_cast<std::size_t>(second_position)];
    const auto& a = population[static_cast<std::size_t>(first)];
    const auto& b = population[static_cast<std::size_t>(second)];
    if (a.rank != b.rank) return a.rank < b.rank ? first : second;
    if (a.crowding != b.crowding) return a.crowding > b.crowding ? first : second;
    return std::min(first, second);
}

std::vector<std::uint64_t> nsga_child(
    const Problem& problem,
    const std::vector<Individual>& population,
    const std::vector<int>* pool,
    const fode::CounterRng& rng,
    const std::uint64_t generation,
    const std::uint64_t child_index,
    const double mutation_probability,
    const std::uint64_t attempt
) {
    const int first = tournament(
        population, rng, generation, child_index, 2U * attempt, pool
    );
    const int second = tournament(
        population, rng, generation, child_index, 2U * attempt + 1U, pool
    );
    std::vector<std::uint64_t> child =
        population[static_cast<std::size_t>(first)].layout;
    const auto& mate = population[static_cast<std::size_t>(second)].layout;
    const int variables = problem.paper_case().variables;
    if (rng.uniform(generation, 302, child_index, attempt) < kCrossoverProbability) {
        int cut_one = rng.integer(
            0, variables, generation, 303, child_index, 2U * attempt
        );
        int cut_two = rng.integer(
            0, variables, generation, 303, child_index, 2U * attempt + 1U
        );
        if (cut_one > cut_two) std::swap(cut_one, cut_two);
        for (int variable = cut_one; variable <= cut_two; ++variable) {
            set_occupied(child, variable, occupied(mate, variable));
        }
    }
    for (int variable = 0; variable < variables; ++variable) {
        if (rng.uniform(
                generation, 304, child_index,
                static_cast<std::uint64_t>(variable), attempt
            ) < mutation_probability) {
            set_occupied(child, variable, !occupied(child, variable));
        }
    }
    return child;
}

struct GomeaWork {
    Individual offspring;
    std::vector<Individual> archive_candidates;
    std::uint64_t physical_fes = 0;
    std::uint64_t attempts = 0;
    std::uint64_t rejected = 0;
    double evaluator_cpu_seconds = 0.0;
};

GomeaWork mix_individual(
    const Problem& problem,
    const std::vector<Individual>& population,
    const std::vector<Individual>& archive_snapshot,
    const Clustering& clustering,
    const std::vector<LinkageTree>& linkage,
    const int individual_index,
    const RunConfig& config,
    const fode::CounterRng& rng,
    const std::uint64_t generation,
    const std::uint64_t evaluation_quota,
    const int no_improvement_generations
) {
    GomeaWork result;
    result.offspring = population[static_cast<std::size_t>(individual_index)];
    const int cluster = clustering.assignment[static_cast<std::size_t>(individual_index)];
    const auto& donors = clustering.members[static_cast<std::size_t>(cluster)];
    std::vector<Individual> donor_pool;
    donor_pool.reserve(donors.size());
    for (const int donor : donors) {
        donor_pool.push_back(population[static_cast<std::size_t>(donor)]);
    }
    const auto& fos = linkage[static_cast<std::size_t>(cluster)].subsets;
    std::vector<int> order(fos.size());
    std::iota(order.begin(), order.end(), 0);
    for (std::size_t position = order.size(); position > 1U; --position) {
        const int swap_position = rng.integer(
            0, static_cast<int>(position), generation, 401,
            static_cast<std::uint64_t>(individual_index), position
        );
        std::swap(order[position - 1U], order[static_cast<std::size_t>(swap_position)]);
    }
    int energy_extreme = 0;
    int efficiency_extreme = 0;
    std::array<double, kClusters> mean_energy{};
    std::array<double, kClusters> mean_efficiency{};
    for (int c = 0; c < kClusters; ++c) {
        const auto& members = clustering.members[static_cast<std::size_t>(c)];
        for (const int member : members) {
            mean_energy[static_cast<std::size_t>(c)] +=
                population[static_cast<std::size_t>(member)].evaluation.normalized_energy;
            mean_efficiency[static_cast<std::size_t>(c)] +=
                population[static_cast<std::size_t>(member)].evaluation.efficiency;
        }
        const double denominator = static_cast<double>(std::max<std::size_t>(1U, members.size()));
        mean_energy[static_cast<std::size_t>(c)] /= denominator;
        mean_efficiency[static_cast<std::size_t>(c)] /= denominator;
        if (mean_energy[static_cast<std::size_t>(c)]
            > mean_energy[static_cast<std::size_t>(energy_extreme)]) energy_extreme = c;
        if (mean_efficiency[static_cast<std::size_t>(c)]
            > mean_efficiency[static_cast<std::size_t>(efficiency_extreme)]) {
            efficiency_extreme = c;
        }
    }
    const int single_objective = cluster == energy_extreme ? 0
        : (cluster == efficiency_extreme ? 1 : -1);
    const double mutation_probability = config.mutation_probability_override > 0.0
        ? config.mutation_probability_override
        : 1.0 / static_cast<double>(problem.paper_case().variables);
    std::vector<Individual> local_archive = archive_snapshot;
    bool changed = false;
    const auto accepted = [&](const Evaluation& candidate, const Evaluation& parent,
                              const bool forced) {
        if (single_objective == 0) {
            return candidate.normalized_energy > parent.normalized_energy
                || (!forced && candidate.normalized_energy == parent.normalized_energy);
        }
        if (single_objective == 1) {
            return candidate.efficiency > parent.efficiency
                || (!forced && candidate.efficiency == parent.efficiency);
        }
        if (dominates(candidate, parent)) return true;
        if (!forced && same_objectives(candidate, parent)) return true;
        return !archive_dominates(local_archive, candidate)
            && (!forced || !same_objectives(candidate, parent));
    };
    auto attempt_subset = [&](
        const std::vector<int>& subset,
        const std::vector<Individual>& donor_source,
        const std::uint64_t phase,
        const bool forced
    ) mutable -> bool {
        if (result.physical_fes >= evaluation_quota || donor_source.empty()) return false;
        const int max_tries = config.constraint == ConstraintHandling::resample ? 100 : 1;
        for (int trial = 0; trial < max_tries; ++trial) {
            if (result.physical_fes >= evaluation_quota) return false;
            ++result.attempts;
            const int donor_index = rng.integer(
                0, static_cast<int>(donor_source.size()), generation, phase,
                static_cast<std::uint64_t>(individual_index),
                static_cast<std::uint64_t>(result.attempts)
            );
            std::vector<std::uint64_t> candidate = result.offspring.layout;
            const auto& donor = donor_source[static_cast<std::size_t>(donor_index)].layout;
            for (const int variable : subset) {
                bool value = occupied(donor, variable);
                if (rng.uniform(
                        generation, phase + 1U,
                        static_cast<std::uint64_t>(individual_index),
                        static_cast<std::uint64_t>(variable),
                        static_cast<std::uint64_t>(result.attempts)
                    ) < mutation_probability) value = !value;
                set_occupied(candidate, variable, value);
            }
            if (candidate == result.offspring.layout) continue;
            if (config.constraint == ConstraintHandling::repair) {
                candidate = problem.repair(
                    std::move(candidate), config.seed,
                    generation * 100000000ULL
                        + static_cast<std::uint64_t>(individual_index) * 100000ULL
                        + result.attempts
                );
            }
            const int violations = problem.violation_count(candidate);
            if ((config.constraint == ConstraintHandling::constraint_domination
                 || config.constraint == ConstraintHandling::resample)
                && violations != 0) {
                ++result.rejected;
                continue;
            }
            const auto start = Clock::now();
            Evaluation evaluation = problem.evaluate_direct(
                candidate, config.constraint
            );
            result.evaluator_cpu_seconds += seconds_since(start);
            ++result.physical_fes;
            Individual proposal{std::move(candidate), evaluation};
            if (accepted(proposal.evaluation, result.offspring.evaluation, forced)) {
                result.offspring = proposal;
                result.archive_candidates.push_back(proposal);
                update_archive(local_archive, std::move(proposal));
                changed = true;
                return true;
            }
            if (config.constraint != ConstraintHandling::resample) return false;
        }
        return false;
    };
    for (const int subset_index : order) {
        if (result.physical_fes >= evaluation_quota) break;
        attempt_subset(
            fos[static_cast<std::size_t>(subset_index)], donor_pool, 402, false
        );
    }
    const int fi_threshold = 1 + static_cast<int>(std::floor(
        std::log10(static_cast<double>(std::max<std::size_t>(1U, population.size())))
    ));
    if (!changed && no_improvement_generations > fi_threshold
        && result.physical_fes < evaluation_quota && !archive_snapshot.empty()) {
        for (const int subset_index : order) {
            if (attempt_subset(
                    fos[static_cast<std::size_t>(subset_index)],
                    archive_snapshot, 410, true
                )) break;
            if (result.physical_fes >= evaluation_quota) break;
        }
        if (!changed) {
            const int replacement = rng.integer(
                0, static_cast<int>(archive_snapshot.size()), generation, 420,
                static_cast<std::uint64_t>(individual_index)
            );
            result.offspring = archive_snapshot[static_cast<std::size_t>(replacement)];
        }
    }
    return result;
}

struct EngineResult {
    RunReceipt receipt;
    std::vector<Individual> archive;
    bool stagnated = false;
};

EngineResult run_fixed_engine(
    const Problem& problem,
    const RunConfig& config,
    const std::vector<std::vector<std::uint64_t>>& transferred
) {
    if (config.workers <= 0 || config.maximum_physical_fes == 0U) {
        throw std::invalid_argument("invalid T10 run configuration");
    }
    const auto total_start = Clock::now();
    fode::PersistentExecutor executor(config.workers);
    executor.reset_work_receipt();
    const fode::CounterRng rng(config.seed);
    std::vector<Individual> population(static_cast<std::size_t>(kInitialPopulation));
    const int transferred_count = std::min(
        kInitialPopulation, static_cast<int>(transferred.size())
    );
    for (int index = 0; index < transferred_count; ++index) {
        population[static_cast<std::size_t>(index)].layout =
            transferred[static_cast<std::size_t>(index)];
    }
    executor.parallel_for(transferred_count, kInitialPopulation, [&](const int index) {
        population[static_cast<std::size_t>(index)].layout = problem.initial_layout(
            config.seed, static_cast<std::uint64_t>(index)
        );
    });
    std::vector<std::vector<std::uint64_t>> initial_layouts;
    for (const auto& item : population) initial_layouts.push_back(item.layout);
    const auto evaluation_start = Clock::now();
    const auto initial_evaluations = problem.evaluate_population(
        initial_layouts, config.constraint, executor
    );
    double evaluator_seconds = seconds_since(evaluation_start);
    std::uint64_t physical_fes = initial_evaluations.size();
    for (std::size_t index = 0; index < population.size(); ++index) {
        population[index].evaluation = initial_evaluations[index];
    }
    std::vector<Individual> archive;
    for (const auto& item : population) update_archive(archive, item);
    std::uint64_t attempted = population.size();
    std::uint64_t rejected = 0;
    double linkage_seconds = 0.0;
    int generation = 0;
    int no_improvement = 0;
    double previous_hypervolume = hypervolume(archive);
    bool stagnated = false;
    LinkageTree offline_tree;
    if (config.algorithm == Algorithm::offline_mogomea) {
        offline_tree = build_linkage_tree(
            problem.paper_case(), population, {}, true
        );
    }
    while (physical_fes < config.maximum_physical_fes
           && generation < config.maximum_generations) {
        ++generation;
        const std::uint64_t generation_key = static_cast<std::uint64_t>(generation);
        if (config.algorithm == Algorithm::mogomea
            || config.algorithm == Algorithm::offline_mogomea) {
            const Clustering clustering = cluster_population(
                population, rng, generation_key
            );
            const auto linkage_start = Clock::now();
            std::vector<LinkageTree> linkage(kClusters);
            if (config.algorithm == Algorithm::offline_mogomea) {
                std::fill(linkage.begin(), linkage.end(), offline_tree);
            } else {
                executor.parallel_for(0, kClusters, [&](const int cluster) {
                    linkage[static_cast<std::size_t>(cluster)] = build_linkage_tree(
                        problem.paper_case(), population,
                        clustering.members[static_cast<std::size_t>(cluster)], false
                    );
                });
            }
            linkage_seconds += seconds_since(linkage_start);
            const std::uint64_t remaining =
                config.maximum_physical_fes - physical_fes;
            const std::uint64_t base_quota = remaining / population.size();
            const std::uint64_t extra = remaining % population.size();
            std::vector<GomeaWork> work(population.size());
            const auto mixing_start = Clock::now();
            executor.parallel_for(0, static_cast<int>(population.size()), [&](const int index) {
                const std::uint64_t quota = base_quota
                    + (static_cast<std::uint64_t>(index) < extra ? 1ULL : 0ULL);
                work[static_cast<std::size_t>(index)] = mix_individual(
                    problem, population, archive, clustering, linkage, index,
                    config, rng, generation_key, quota, no_improvement
                );
            });
            const double mixing_wall = seconds_since(mixing_start);
            double evaluation_cpu = 0.0;
            std::vector<Individual> next;
            next.reserve(population.size() + kInitialPopulation);
            for (auto& item : work) {
                physical_fes += item.physical_fes;
                attempted += item.attempts;
                rejected += item.rejected;
                evaluation_cpu += item.evaluator_cpu_seconds;
                for (auto& candidate : item.archive_candidates) {
                    update_archive(archive, std::move(candidate));
                }
                next.push_back(std::move(item.offspring));
            }
            evaluator_seconds += std::min(
                mixing_wall,
                evaluation_cpu / static_cast<double>(std::max(1, config.workers))
            );
            population = std::move(next);
            const int growth = static_cast<int>(std::min<std::uint64_t>(
                kInitialPopulation,
                config.maximum_physical_fes - physical_fes
            ));
            if (growth > 0) {
                std::vector<std::vector<std::uint64_t>> layouts(
                    static_cast<std::size_t>(growth)
                );
                executor.parallel_for(0, growth, [&](const int index) {
                    layouts[static_cast<std::size_t>(index)] = problem.initial_layout(
                        config.seed,
                        generation_key * 100000ULL
                            + static_cast<std::uint64_t>(index)
                    );
                });
                const auto start = Clock::now();
                const auto evaluations = problem.evaluate_population(
                    layouts, config.constraint, executor
                );
                evaluator_seconds += seconds_since(start);
                for (int index = 0; index < growth; ++index) {
                    Individual item{
                        std::move(layouts[static_cast<std::size_t>(index)]),
                        evaluations[static_cast<std::size_t>(index)]
                    };
                    update_archive(archive, item);
                    population.push_back(std::move(item));
                }
                physical_fes += static_cast<std::uint64_t>(growth);
                attempted += static_cast<std::uint64_t>(growth);
            }
            std::vector<Individual> candidates = archive;
            candidates.insert(candidates.end(), population.begin(), population.end());
            population = survivor_selection(
                std::move(candidates), static_cast<int>(population.size()), executor
            );
        } else {
            rank_and_crowding(population, executor);
            Clustering clustering;
            if (config.algorithm == Algorithm::clustered_nsgaii) {
                clustering = cluster_population(population, rng, generation_key);
            }
            const int offspring_count = static_cast<int>(std::min<std::uint64_t>(
                population.size(), config.maximum_physical_fes - physical_fes
            ));
            std::vector<std::vector<std::uint64_t>> offspring_layouts(
                static_cast<std::size_t>(offspring_count)
            );
            std::vector<std::uint8_t> evaluate_child(
                static_cast<std::size_t>(offspring_count), 1U
            );
            executor.parallel_for(0, offspring_count, [&](const int index) {
                const std::vector<int>* pool = nullptr;
                if (config.algorithm == Algorithm::clustered_nsgaii) {
                    const int cluster = index % kClusters;
                    pool = &clustering.members[static_cast<std::size_t>(cluster)];
                }
                const double mutation_probability =
                    config.mutation_probability_override > 0.0
                    ? config.mutation_probability_override
                    : 1.0 / static_cast<double>(problem.paper_case().variables);
                std::vector<std::uint64_t> child;
                bool ready = false;
                const int max_tries = config.constraint == ConstraintHandling::resample
                    ? 100 : 1;
                for (int trial = 0; trial < max_tries; ++trial) {
                    child = nsga_child(
                        problem, population, pool, rng, generation_key,
                        static_cast<std::uint64_t>(index), mutation_probability,
                        static_cast<std::uint64_t>(trial)
                    );
                    if (config.constraint == ConstraintHandling::repair) {
                        child = problem.repair(
                            std::move(child), config.seed,
                            generation_key * 100000ULL
                                + static_cast<std::uint64_t>(index) * 100ULL
                                + static_cast<std::uint64_t>(trial)
                        );
                    }
                    if ((config.constraint == ConstraintHandling::constraint_domination
                         || config.constraint == ConstraintHandling::resample)
                        && !problem.feasible(child)) continue;
                    ready = true;
                    break;
                }
                if (!ready) {
                    const int parent = tournament(
                        population, rng, generation_key,
                        static_cast<std::uint64_t>(index), 99, pool
                    );
                    child = population[static_cast<std::size_t>(parent)].layout;
                    evaluate_child[static_cast<std::size_t>(index)] = 0U;
                }
                offspring_layouts[static_cast<std::size_t>(index)] = std::move(child);
            });
            std::vector<std::vector<std::uint64_t>> to_evaluate;
            for (int index = 0; index < offspring_count; ++index) {
                ++attempted;
                if (evaluate_child[static_cast<std::size_t>(index)]) {
                    to_evaluate.push_back(offspring_layouts[static_cast<std::size_t>(index)]);
                } else {
                    ++rejected;
                }
            }
            const auto start = Clock::now();
            const auto evaluations = problem.evaluate_population(
                to_evaluate, config.constraint, executor
            );
            evaluator_seconds += seconds_since(start);
            physical_fes += evaluations.size();
            std::vector<Individual> offspring;
            offspring.reserve(static_cast<std::size_t>(offspring_count));
            std::size_t evaluation_index = 0;
            for (int index = 0; index < offspring_count; ++index) {
                if (evaluate_child[static_cast<std::size_t>(index)]) {
                    Individual item{
                        std::move(offspring_layouts[static_cast<std::size_t>(index)]),
                        evaluations[evaluation_index++]
                    };
                    update_archive(archive, item);
                    offspring.push_back(std::move(item));
                } else {
                    int matching = 0;
                    for (int parent = 0; parent < static_cast<int>(population.size()); ++parent) {
                        if (population[static_cast<std::size_t>(parent)].layout
                            == offspring_layouts[static_cast<std::size_t>(index)]) {
                            matching = parent;
                            break;
                        }
                    }
                    offspring.push_back(population[static_cast<std::size_t>(matching)]);
                }
            }
            std::vector<Individual> merged = population;
            merged.insert(merged.end(), offspring.begin(), offspring.end());
            population = survivor_selection(
                std::move(merged), static_cast<int>(population.size()), executor
            );
            const int equivalent_mogomea_generations =
                std::max(1, 2 * problem.paper_case().variables - 2);
            if (generation % equivalent_mogomea_generations == 0
                && physical_fes < config.maximum_physical_fes) {
                const int growth = static_cast<int>(std::min<std::uint64_t>(
                    kInitialPopulation,
                    config.maximum_physical_fes - physical_fes
                ));
                std::vector<std::vector<std::uint64_t>> layouts(
                    static_cast<std::size_t>(growth)
                );
                executor.parallel_for(0, growth, [&](const int index) {
                    layouts[static_cast<std::size_t>(index)] = problem.initial_layout(
                        config.seed,
                        generation_key * 100000ULL
                            + static_cast<std::uint64_t>(index)
                    );
                });
                const auto start_growth = Clock::now();
                const auto evaluations_growth = problem.evaluate_population(
                    layouts, config.constraint, executor
                );
                evaluator_seconds += seconds_since(start_growth);
                for (int index = 0; index < growth; ++index) {
                    Individual item{
                        std::move(layouts[static_cast<std::size_t>(index)]),
                        evaluations_growth[static_cast<std::size_t>(index)]
                    };
                    update_archive(archive, item);
                    population.push_back(std::move(item));
                }
                physical_fes += static_cast<std::uint64_t>(growth);
                attempted += static_cast<std::uint64_t>(growth);
            }
        }
        const double current_hypervolume = hypervolume(archive);
        if (current_hypervolume - previous_hypervolume >= kHypervolumeImprovement) {
            previous_hypervolume = current_hypervolume;
            no_improvement = 0;
        } else {
            ++no_improvement;
        }
        const int nis = 1 + static_cast<int>(std::floor(
            std::log10(static_cast<double>(std::max<std::size_t>(1U, population.size())))
        ));
        if (no_improvement >= 2 * nis) {
            stagnated = true;
            break;
        }
    }
    const fode::ExecutorWorkReceipt work = executor.work_receipt();
    RunReceipt receipt;
    receipt.case_id = problem.paper_case().case_id;
    receipt.algorithm = algorithm_name(config.algorithm);
    receipt.constraint_handling = constraint_name(config.constraint);
    receipt.problem_semantic_id = "t10_mowflop_katic_binary_grid_v1";
    receipt.method_semantic_id = config.algorithm == Algorithm::mogomea
        ? "t10_mogomea_online_mi_v1"
        : (config.algorithm == Algorithm::offline_mogomea
            ? "t10_omogomea_offline_geographic_v1"
            : (config.algorithm == Algorithm::nsgaii
                ? "t10_nsgaii_archive_growth_v1"
                : "t10_clustered_nsgaii_archive_growth_v1"));
    receipt.seed = config.seed;
    receipt.requested_workers = config.workers;
    receipt.observed_workers = std::max(1, work.distinct_participants);
    receipt.physical_fes = physical_fes;
    receipt.attempted_candidates = attempted;
    receipt.rejected_infeasible_without_evaluation = rejected;
    receipt.generations = generation;
    receipt.final_grid_step_diameters = problem.paper_case().grid_step_diameters;
    receipt.final_population = static_cast<int>(population.size());
    receipt.archive_size = static_cast<int>(archive.size());
    receipt.hypervolume = hypervolume(archive);
    receipt.evaluator_seconds = evaluator_seconds;
    receipt.linkage_seconds = linkage_seconds;
    receipt.end_to_end_seconds = seconds_since(total_start);
    receipt.algorithm_seconds = std::max(
        0.0, receipt.end_to_end_seconds - evaluator_seconds
    );
    receipt.scientific_hash = population_hash(archive);
    for (const auto& item : archive) {
        receipt.archive.push_back({
            item.evaluation.normalized_energy,
            item.evaluation.efficiency,
            item.evaluation.occupied_turbines,
            item.evaluation.violating_pairs,
            item.layout,
        });
    }
    return {std::move(receipt), std::move(archive), stagnated};
}

std::vector<std::vector<std::uint64_t>> map_archive(
    const std::vector<Individual>& archive,
    const PaperCase& coarse,
    const Problem& fine
) {
    std::vector<std::vector<std::uint64_t>> result;
    const int factor = coarse.grid_step_diameters
        / fine.paper_case().grid_step_diameters;
    for (const auto& item : archive) {
        std::vector<std::uint64_t> mapped(
            static_cast<std::size_t>(fine.word_count()), 0ULL
        );
        for (int variable = 0; variable < coarse.variables; ++variable) {
            if (!occupied(item.layout, variable)) continue;
            const int row = variable / coarse.side_points;
            const int column = variable % coarse.side_points;
            const int fine_index = row * factor * fine.paper_case().side_points
                + column * factor;
            set_occupied(mapped, fine_index, true);
        }
        result.push_back(std::move(mapped));
    }
    return result;
}

}  // namespace

RunReceipt optimize(const Problem& problem, const RunConfig& config) {
    if (!config.multi_resolution) {
        return run_fixed_engine(problem, config, {}).receipt;
    }
    const char farm = problem.paper_case().farm;
    const auto total_start = Clock::now();
    std::uint64_t remaining = config.maximum_physical_fes;
    std::vector<std::vector<std::uint64_t>> transferred;
    std::vector<Individual> archive;
    RunReceipt aggregate;
    aggregate.case_id = std::string("t10_") + farm + "_mr_8_4_2";
    aggregate.algorithm = algorithm_name(config.algorithm);
    aggregate.constraint_handling = constraint_name(config.constraint);
    aggregate.problem_semantic_id = "t10_mowflop_katic_binary_grid_v1";
    aggregate.method_semantic_id = config.algorithm == Algorithm::mogomea
        ? "t10_mogomea_online_mi_v1"
        : (config.algorithm == Algorithm::offline_mogomea
            ? "t10_omogomea_offline_geographic_v1"
            : (config.algorithm == Algorithm::nsgaii
                ? "t10_nsgaii_archive_growth_v1"
                : "t10_clustered_nsgaii_archive_growth_v1"));
    aggregate.seed = config.seed;
    aggregate.requested_workers = config.workers;
    for (const int step : {8, 4, 2}) {
        Problem phase_problem(std::string("t10_") + farm + "_" + std::to_string(step));
        RunConfig phase_config = config;
        phase_config.multi_resolution = false;
        phase_config.maximum_physical_fes = remaining;
        if (step != 8) {
            // Paper keeps the 8D mutation probability at finer resolutions.
            const int q8 = farm == 'A' ? 4 : (farm == 'B' ? 7 : (farm == 'C' ? 10 : 13));
            phase_config.mutation_probability_override = 1.0
                / static_cast<double>(q8 * q8);
        }
        EngineResult phase = run_fixed_engine(
            phase_problem, phase_config, transferred
        );
        remaining -= std::min(remaining, phase.receipt.physical_fes);
        aggregate.physical_fes += phase.receipt.physical_fes;
        aggregate.attempted_candidates += phase.receipt.attempted_candidates;
        aggregate.rejected_infeasible_without_evaluation +=
            phase.receipt.rejected_infeasible_without_evaluation;
        aggregate.generations += phase.receipt.generations;
        aggregate.evaluator_seconds += phase.receipt.evaluator_seconds;
        aggregate.linkage_seconds += phase.receipt.linkage_seconds;
        aggregate.algorithm_seconds += phase.receipt.algorithm_seconds;
        aggregate.observed_workers = std::max(
            aggregate.observed_workers, phase.receipt.observed_workers
        );
        aggregate.final_grid_step_diameters = step;
        aggregate.final_population = phase.receipt.final_population;
        archive = std::move(phase.archive);
        if (step == 2 || remaining == 0U) break;
        Problem next_problem(std::string("t10_") + farm + "_"
            + std::to_string(step / 2));
        transferred = map_archive(
            archive, phase_problem.paper_case(), next_problem
        );
    }
    aggregate.archive_size = static_cast<int>(archive.size());
    aggregate.hypervolume = hypervolume(archive);
    aggregate.scientific_hash = population_hash(archive);
    aggregate.archive.clear();
    for (const auto& item : archive) {
        aggregate.archive.push_back({
            item.evaluation.normalized_energy,
            item.evaluation.efficiency,
            item.evaluation.occupied_turbines,
            item.evaluation.violating_pairs,
            item.layout,
        });
    }
    aggregate.end_to_end_seconds = seconds_since(total_start);
    return aggregate;
}

std::vector<std::string> paper_case_ids() {
    std::vector<std::string> result;
    for (const char farm : {'A', 'B', 'C', 'D'}) {
        for (const int step : {8, 4, 2}) {
            result.push_back(
                std::string("t10_") + farm + "_" + std::to_string(step)
            );
        }
    }
    return result;
}

std::vector<std::string> paper_role_ids(const bool include_multi_resolution) {
    std::vector<std::string> result;
    for (const char farm : {'A', 'B', 'C', 'D'}) {
        for (const int step : {8, 4, 2}) {
            for (const char* constraint : {"constraint", "penalty", "repair", "resample"}) {
                for (const char* algorithm : {"mogomea", "omogomea", "nsgaii", "c-nsgaii"}) {
                    result.push_back(
                        std::string("t10_") + farm + "_" + std::to_string(step)
                        + "_" + constraint + "_" + algorithm
                    );
                }
            }
        }
    }
    if (include_multi_resolution) {
        for (const char* algorithm : {"mogomea", "omogomea", "nsgaii", "c-nsgaii"}) {
            result.push_back(std::string("t10_B_mr_") + algorithm);
        }
    }
    return result;
}

const char* algorithm_name(const Algorithm value) noexcept {
    switch (value) {
        case Algorithm::mogomea: return "mogomea";
        case Algorithm::offline_mogomea: return "o-mogomea";
        case Algorithm::nsgaii: return "nsga-ii";
        case Algorithm::clustered_nsgaii: return "c-nsga-ii";
    }
    return "unknown";
}

const char* constraint_name(const ConstraintHandling value) noexcept {
    switch (value) {
        case ConstraintHandling::no_constraints: return "no-constraints";
        case ConstraintHandling::constraint_domination: return "constraint-domination";
        case ConstraintHandling::penalty: return "penalty";
        case ConstraintHandling::repair: return "repair";
        case ConstraintHandling::resample: return "resample";
    }
    return "unknown";
}

Algorithm parse_algorithm(const std::string& value) {
    if (value == "mogomea") return Algorithm::mogomea;
    if (value == "omogomea" || value == "o-mogomea") {
        return Algorithm::offline_mogomea;
    }
    if (value == "nsgaii" || value == "nsga-ii") return Algorithm::nsgaii;
    if (value == "cnsgaii" || value == "c-nsgaii" || value == "c-nsga-ii") {
        return Algorithm::clustered_nsgaii;
    }
    throw std::invalid_argument("unknown T10 algorithm " + value);
}

ConstraintHandling parse_constraint(const std::string& value) {
    if (value == "none" || value == "no-constraints") {
        return ConstraintHandling::no_constraints;
    }
    if (value == "constraint" || value == "constraint-domination") {
        return ConstraintHandling::constraint_domination;
    }
    if (value == "penalty") return ConstraintHandling::penalty;
    if (value == "repair") return ConstraintHandling::repair;
    if (value == "resample") return ConstraintHandling::resample;
    throw std::invalid_argument("unknown T10 constraint " + value);
}

}  // namespace core99::t10
