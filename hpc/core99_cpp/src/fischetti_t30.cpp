/*
WFLOP IMPLEMENTATION FACT DECLARATION
Implementation unit: T30 pure-C++/HiGHS packed-matrix proximity search
Paper DOI: 10.1007/s10732-015-9283-4
Public source: none found; open author thesis 20.500.12608/17839.
Missing/conflicts/reconstruction/HPC/claim boundary:
include/core99/fischetti_t30.hpp.
Semantic IDs and Contract: shared/contracts/core99_t30_fischetti_proxy_2016.json.
Pinned open MIP replacement: HiGHS revision
04024d701f79feb8e2f18bc3df0dffc04ef05088.
Last evidence-audit date: 2026-07-31
END WFLOP IMPLEMENTATION FACT DECLARATION
*/
#include "core99/fischetti_t30.hpp"

#include "Highs.h"
#include "fode/executor.hpp"

#include <algorithm>
#include <bit>
#include <chrono>
#include <cmath>
#include <cstring>
#include <fstream>
#include <iomanip>
#include <limits>
#include <numbers>
#include <numeric>
#include <random>
#include <sstream>
#include <stdexcept>

namespace core99::t30 {
namespace {

using Clock = std::chrono::steady_clock;
constexpr double kSide = 3000.0;
constexpr double kSpacing = 400.0;
constexpr double kRotor = 93.0;
constexpr double kWakeExpansion = 0.04;
constexpr double kBig = 10000.0;
constexpr int kBearingBins = 72;
constexpr int kDistanceBins = 601;

std::uint64_t splitmix(std::uint64_t value) {
    value += 0x9e3779b97f4a7c15ULL;
    value = (value ^ (value >> 30U)) * 0xbf58476d1ce4e5b9ULL;
    value = (value ^ (value >> 27U)) * 0x94d049bb133111ebULL;
    return value ^ (value >> 31U);
}

double unit(std::uint64_t value) {
    return static_cast<double>(splitmix(value) >> 11U)
        * (1.0 / 9007199254740992.0);
}

std::size_t packed_index(int n, int first, int second) {
    if (first == second) throw std::invalid_argument("T30 diagonal pair");
    if (first > second) std::swap(first, second);
    return static_cast<std::size_t>(first)
             * static_cast<std::size_t>(2 * n - first - 1) / 2U
         + static_cast<std::size_t>(second - first - 1);
}

double swt_power(double speed) {
    if (speed <= 3.0 || speed >= 25.0) return 0.0;
    if (speed >= 16.0) return 2.3;
    const double numerator = speed * speed * speed - 27.0;
    const double denominator = 4096.0 - 27.0;
    return 2.3 * std::clamp(numerator / denominator, 0.0, 1.0);
}

double circle_overlap(double separation, double first, double second) {
    if (separation >= first + second) return 0.0;
    if (separation <= std::abs(first - second)) {
        const double radius = std::min(first, second);
        return std::numbers::pi * radius * radius;
    }
    const double a = std::acos(std::clamp(
        (separation * separation + first * first - second * second)
            / (2.0 * separation * first),
        -1.0, 1.0
    ));
    const double b = std::acos(std::clamp(
        (separation * separation + second * second - first * first)
            / (2.0 * separation * second),
        -1.0, 1.0
    ));
    const double radical = std::max(
        0.0,
        (-separation + first + second)
        * (separation + first - second)
        * (separation - first + second)
        * (separation + first + second)
    );
    return first * first * a + second * second * b
        - 0.5 * std::sqrt(radical);
}

struct WindState {
    double direction = 0.0;
    double speed = 0.0;
    double probability = 0.0;
};

std::vector<WindState> declared_wind() {
    std::vector<WindState> states;
    std::array<double, 36> direction_weight{};
    double direction_total = 0.0;
    for (int direction = 0; direction < 36; ++direction) {
        const double radians =
            (direction * 10.0 - 240.0) * std::numbers::pi / 180.0;
        direction_weight[direction] =
            std::max(0.05, 1.0 + 0.55 * std::cos(radians));
        direction_total += direction_weight[direction];
    }
    std::array<double, 14> speed_weight{};
    double speed_total = 0.0;
    for (int speed = 0; speed < 14; ++speed) {
        const double low = 2.5 + speed;
        const double high = low + 1.0;
        const auto cdf = [](double value) {
            return 1.0 - std::exp(-std::pow(value / 10.0, 2.0));
        };
        speed_weight[speed] = cdf(high) - cdf(low);
        speed_total += speed_weight[speed];
    }
    for (int direction = 0; direction < 36; ++direction) {
        for (int speed = 0; speed < 14; ++speed) {
            states.push_back({
                direction * 10.0,
                3.0 + speed,
                direction_weight[direction] / direction_total
                    * speed_weight[speed] / speed_total,
            });
        }
    }
    return states;
}

double directed_loss(
    double dx,
    double dy,
    const WindState& state
) {
    const double angle = state.direction * std::numbers::pi / 180.0;
    const double flow_x = -std::sin(angle);
    const double flow_y = -std::cos(angle);
    const double downstream = dx * flow_x + dy * flow_y;
    if (!(downstream > 0.0)) return 0.0;
    const double crosswind = std::abs(-dx * flow_y + dy * flow_x);
    const double source_radius = 0.5 * kRotor;
    const double wake_radius = source_radius + kWakeExpansion * downstream;
    const double overlap = circle_overlap(
        crosswind, wake_radius, source_radius
    ) / (std::numbers::pi * source_radius * source_radius);
    if (!(overlap > 0.0)) return 0.0;
    constexpr double thrust = 0.8;
    const double deficit =
        (1.0 - std::sqrt(1.0 - thrust))
        * std::pow(source_radius / wake_radius, 2.0)
        * overlap;
    return std::max(
        0.0,
        swt_power(state.speed)
            - swt_power(state.speed * std::max(0.0, 1.0 - deficit))
    );
}

std::vector<float> loss_lookup() {
    const auto wind = declared_wind();
    std::vector<float> table(
        static_cast<std::size_t>(kBearingBins) * kDistanceBins,
        0.0F
    );
    for (int bearing = 0; bearing < kBearingBins; ++bearing) {
        const double angle =
            bearing * 5.0 * std::numbers::pi / 180.0;
        for (int bin = 0; bin < kDistanceBins; ++bin) {
            const double distance = bin * 5.0;
            const double dx = distance * std::cos(angle);
            const double dy = distance * std::sin(angle);
            double loss = 0.0;
            for (const auto& state : wind) {
                loss += state.probability
                    * (directed_loss(dx, dy, state)
                       + directed_loss(-dx, -dy, state));
            }
            table[static_cast<std::size_t>(bearing) * kDistanceBins + bin] =
                static_cast<float>(loss <= 0.01 ? 0.0 : loss);
        }
    }
    return table;
}

double average_free_power() {
    double value = 0.0;
    for (const auto& state : declared_wind()) {
        value += state.probability * swt_power(state.speed);
    }
    return value;
}

struct CacheHeader {
    char magic[8]{};
    std::uint32_t version = 1;
    std::uint32_t sites = 0;
    std::uint32_t instance = 0;
    std::uint32_t reserved = 0;
};

void write_cache(
    const std::filesystem::path& path,
    int sites,
    int instance,
    const std::vector<float>& values
) {
    if (path.empty()) return;
    std::filesystem::create_directories(path.parent_path());
    std::ofstream stream(path, std::ios::binary | std::ios::trunc);
    if (!stream) throw std::runtime_error("cannot write T30 matrix cache");
    CacheHeader header;
    std::memcpy(header.magic, "T30PAIR", 7);
    header.sites = static_cast<std::uint32_t>(sites);
    header.instance = static_cast<std::uint32_t>(instance);
    stream.write(reinterpret_cast<const char*>(&header), sizeof(header));
    stream.write(
        reinterpret_cast<const char*>(values.data()),
        static_cast<std::streamsize>(values.size() * sizeof(float))
    );
}

bool read_cache(
    const std::filesystem::path& path,
    int sites,
    int instance,
    std::vector<float>& values
) {
    if (path.empty() || !std::filesystem::exists(path)) return false;
    std::ifstream stream(path, std::ios::binary);
    CacheHeader header;
    stream.read(reinterpret_cast<char*>(&header), sizeof(header));
    if (
        !stream
        || std::memcmp(header.magic, "T30PAIR", 7) != 0
        || header.version != 1
        || header.sites != static_cast<std::uint32_t>(sites)
        || header.instance != static_cast<std::uint32_t>(instance)
    ) {
        return false;
    }
    stream.read(
        reinterpret_cast<char*>(values.data()),
        static_cast<std::streamsize>(values.size() * sizeof(float))
    );
    return static_cast<bool>(stream);
}

double minimum_spacing(
    const Problem& problem,
    const std::vector<int>& selected
) {
    double value = std::numeric_limits<double>::infinity();
    for (std::size_t i = 0; i < selected.size(); ++i) {
        for (std::size_t j = i + 1; j < selected.size(); ++j) {
            const auto& first = problem.positions()[selected[i]];
            const auto& second = problem.positions()[selected[j]];
            value = std::min(
                value,
                std::hypot(first.x_m - second.x_m, first.y_m - second.y_m)
            );
        }
    }
    return value;
}

struct SearchState {
    std::vector<unsigned char> active;
    std::vector<double> contribution;
    std::vector<int> selected;
    double objective = 0.0;
};

void rebuild_selected(SearchState& state) {
    state.selected.clear();
    for (int i = 0; i < static_cast<int>(state.active.size()); ++i) {
        if (state.active[i]) state.selected.push_back(i);
    }
}

bool feasible(
    const Problem& problem,
    const SearchState& state
) {
    for (std::size_t i = 0; i < state.selected.size(); ++i) {
        for (std::size_t j = i + 1; j < state.selected.size(); ++j) {
            if (problem.spacing_conflict(
                    state.selected[i], state.selected[j])) {
                return false;
            }
        }
    }
    return true;
}

bool accept_incumbent(
    const Problem& problem,
    const SearchState& state,
    SearchState& incumbent
) {
    if (
        state.objective <= incumbent.objective + 1e-12
        || !feasible(problem, state)
    ) {
        return false;
    }
    incumbent = state;
    return true;
}

bool best_flip(
    const Problem& problem,
    SearchState& state,
    int local_minimum,
    int local_maximum,
    std::uint64_t& delta_evaluations
) {
    constexpr double huge = 1000000.0;
    const int cardinality = static_cast<int>(state.selected.size());
    int best_site = -1;
    double best_score = -std::numeric_limits<double>::infinity();
    double best_delta = 0.0;
    for (int site = 0; site < problem.size(); ++site) {
        const bool active = state.active[site] != 0;
        const double delta = active
            ? -problem.free_power_mw(site) + state.contribution[site]
            : problem.free_power_mw(site) - state.contribution[site];
        double flip = 0.0;
        if (!active && cardinality >= local_maximum) flip = -huge;
        else if (active && cardinality <= local_minimum) flip = -huge;
        else if (!active && cardinality < local_minimum) flip = huge;
        else if (active && cardinality > local_maximum) flip = huge;
        const double score = delta + flip;
        if (score > best_score) {
            best_score = score;
            best_delta = delta;
            best_site = site;
        }
    }
    delta_evaluations += static_cast<std::uint64_t>(problem.size());
    if (best_site < 0 || !(best_score > 1e-12)) return false;
    const bool adding = state.active[best_site] == 0;
    state.active[best_site] = adding ? 1 : 0;
    state.objective += best_delta;
    for (int site = 0; site < problem.size(); ++site) {
        if (site == best_site) continue;
        const double loss = problem.pair_loss_mw(site, best_site);
        state.contribution[site] += adding ? loss : -loss;
    }
    rebuild_selected(state);
    return true;
}

bool best_swap(
    const Problem& problem,
    SearchState& state,
    std::uint64_t& delta_evaluations
) {
    const int n = problem.size();
    const int selected_count = static_cast<int>(state.selected.size());
    const std::uint64_t combinations =
        static_cast<std::uint64_t>(selected_count) * n;
    std::vector<double> delta(static_cast<std::size_t>(combinations), -kBig);
    const auto evaluate = [&](int flat) {
        const int slot = flat / n;
        const int candidate = flat % n;
        if (state.active[candidate]) return;
        const int removed = state.selected[slot];
        delta[flat] = state.contribution[removed]
            - state.contribution[candidate]
            + problem.pair_loss_mw(removed, candidate);
    };
    for (int flat = 0; flat < static_cast<int>(combinations); ++flat) {
        evaluate(flat);
    }
    delta_evaluations += combinations;
    const auto best_it = std::max_element(delta.begin(), delta.end());
    if (best_it == delta.end() || !(*best_it > 1e-12)) return false;
    const int flat = static_cast<int>(best_it - delta.begin());
    const int slot = flat / n;
    const int added = flat % n;
    const int removed = state.selected[slot];
    state.active[removed] = 0;
    state.active[added] = 1;
    state.selected[slot] = added;
    state.objective += *best_it;
    for (int site = 0; site < n; ++site) {
        if (site == removed) {
            state.contribution[site] +=
                problem.pair_loss_mw(site, added);
        } else if (site == added) {
            state.contribution[site] -=
                problem.pair_loss_mw(site, removed);
        } else {
            state.contribution[site] +=
                problem.pair_loss_mw(site, added)
                - problem.pair_loss_mw(site, removed);
        }
    }
    return true;
}

struct MipResult {
    std::vector<int> selected;
    std::string status;
    double seconds = 0.0;
};

MipResult proximity_mip(
    const Problem& problem,
    const SearchState& incumbent,
    const Configuration& config,
    double time_limit,
    bool simplified
) {
    std::vector<int> subset = incumbent.selected;
    std::vector<unsigned char> included(
        static_cast<std::size_t>(problem.size()), 0
    );
    for (const int site : subset) included[site] = 1;
    std::vector<int> remainder;
    remainder.reserve(problem.size());
    for (int site = 0; site < problem.size(); ++site) {
        if (!included[site]) remainder.push_back(site);
    }
    std::mt19937_64 rng(config.seed + 0x30f15cULL);
    std::shuffle(remainder.begin(), remainder.end(), rng);
    const int target = std::min(2000, problem.size());
    for (const int site : remainder) {
        if (static_cast<int>(subset.size()) >= target) break;
        subset.push_back(site);
    }
    std::sort(subset.begin(), subset.end());
    const int m = static_cast<int>(subset.size());
    const int slack_column = 2 * m;
    HighsModel model;
    model.lp_.num_col_ = 2 * m + 1;
    model.lp_.sense_ = ObjSense::kMinimize;
    model.lp_.col_cost_.assign(static_cast<std::size_t>(2 * m + 1), 0.0);
    model.lp_.col_lower_.assign(static_cast<std::size_t>(2 * m + 1), 0.0);
    model.lp_.col_upper_.assign(
        static_cast<std::size_t>(2 * m + 1), kHighsInf
    );
    model.lp_.integrality_.assign(
        static_cast<std::size_t>(2 * m + 1), HighsVarType::kContinuous
    );
    for (int local = 0; local < m; ++local) {
        model.lp_.col_upper_[local] = 1.0;
        model.lp_.integrality_[local] = HighsVarType::kInteger;
        model.lp_.col_cost_[local] = incumbent.active[subset[local]] ? -1.0 : 1.0;
    }
    model.lp_.col_cost_[slack_column] = 1e6;

    auto& matrix = model.lp_.a_matrix_;
    matrix.format_ = MatrixFormat::kRowwise;
    matrix.start_.clear();
    matrix.start_.push_back(0);
    std::vector<double> lower;
    std::vector<double> upper;
    auto append = [&](std::vector<std::pair<int,double>> entries,
                      double lo, double hi) {
        std::sort(entries.begin(), entries.end());
        for (const auto& [column, value] : entries) {
            matrix.index_.push_back(column);
            matrix.value_.push_back(value);
        }
        matrix.start_.push_back(static_cast<HighsInt>(matrix.index_.size()));
        lower.push_back(lo);
        upper.push_back(hi);
    };

    for (int i = 0; i < m; ++i) {
        for (int j = i + 1; j < m; ++j) {
            if (problem.spacing_conflict(subset[i], subset[j])) {
                append({{i,1.0},{j,1.0}}, -kHighsInf, 1.0);
            }
        }
    }
    if (!simplified) {
        for (int i = 0; i < m; ++i) {
            std::vector<std::pair<int,double>> entries;
            entries.reserve(static_cast<std::size_t>(m + 2));
            double big_m = 0.0;
            for (int j = 0; j < m; ++j) {
                if (i == j) continue;
                const double combined =
                    problem.pair_loss_mw(subset[i], subset[j]);
                if (combined <= 0.0 || combined >= 1000.0) continue;
                const double directed = 0.5 * combined;
                entries.emplace_back(j, directed);
                big_m += directed;
            }
            entries.emplace_back(m + i, -1.0);
            entries.emplace_back(i, big_m);
            append(entries, -kHighsInf, big_m);
        }
    }
    const double theta = std::max(1e-4, std::abs(incumbent.objective) * 1e-4);
    std::vector<std::pair<int,double>> cutoff;
    cutoff.reserve(static_cast<std::size_t>(2 * m + 1));
    for (int i = 0; i < m; ++i) {
        cutoff.emplace_back(i, problem.free_power_mw(subset[i]));
        cutoff.emplace_back(m + i, -1.0);
    }
    // Use the equivalent dimensional slack s = theta * xi. A unit
    // coefficient avoids the severe scaling failure triggered by tiny theta
    // in the original dimensionless formulation.
    cutoff.emplace_back(slack_column, 1.0);
    append(cutoff, incumbent.objective + theta, kHighsInf);
    model.lp_.num_row_ = static_cast<HighsInt>(lower.size());
    model.lp_.row_lower_ = std::move(lower);
    model.lp_.row_upper_ = std::move(upper);
    {
        std::vector<double> feasible(
            static_cast<std::size_t>(2 * m + 1), 0.0
        );
        feasible[slack_column] = incumbent.objective + theta;
        for (int row = 0; row < model.lp_.num_row_; ++row) {
            double value = 0.0;
            for (
                HighsInt entry = matrix.start_[row];
                entry < matrix.start_[row + 1];
                ++entry
            ) {
                value += matrix.value_[entry]
                    * feasible[static_cast<std::size_t>(matrix.index_[entry])];
            }
            if (
                value + 1e-9 < model.lp_.row_lower_[row]
                || value - 1e-9 > model.lp_.row_upper_[row]
            ) {
                throw std::runtime_error(
                    "T30 internal zero-layout feasibility failure row "
                    + std::to_string(row)
                    + " value=" + std::to_string(value)
                    + " lower=" + std::to_string(model.lp_.row_lower_[row])
                    + " upper=" + std::to_string(model.lp_.row_upper_[row])
                    + " entries="
                    + std::to_string(matrix.start_[row + 1] - matrix.start_[row])
                    + " last_col=" + std::to_string(
                        matrix.index_[matrix.start_[row + 1] - 1]
                    )
                    + " last_value=" + std::to_string(
                        matrix.value_[matrix.start_[row + 1] - 1]
                    )
                );
            }
        }
    }

    Highs highs;
    highs.setOptionValue("output_flag", false);
    highs.setOptionValue("threads", config.workers);
    highs.setOptionValue("parallel", kHighsOnString);
    highs.setOptionValue("time_limit", std::max(0.05, time_limit));
    highs.setOptionValue("random_seed", static_cast<int>(config.seed % 2147483647));
    highs.setOptionValue("mip_rel_gap", 0.0);
    const auto begin = Clock::now();
    if (highs.passModel(model) == HighsStatus::kError) {
        throw std::runtime_error("HiGHS rejected T30 proximity model");
    }
    HighsSolution start;
    start.col_value.assign(static_cast<std::size_t>(2 * m + 1), 0.0);
    for (int i = 0; i < m; ++i) {
        if (!incumbent.active[subset[i]]) continue;
        start.col_value[i] = 1.0;
        double interference = 0.0;
        for (const int other : incumbent.selected) {
            if (other != subset[i]) {
                const double loss = problem.pair_loss_mw(subset[i], other);
                if (loss < 1000.0) interference += 0.5 * loss;
            }
        }
        start.col_value[m + i] = interference;
    }
    start.col_value[slack_column] = theta;
    highs.setSolution(start);
    if (highs.run() == HighsStatus::kError) {
        throw std::runtime_error("HiGHS failed T30 proximity model");
    }
    MipResult result;
    result.seconds = std::chrono::duration<double>(Clock::now() - begin).count();
    result.status = std::string(simplified ? "simplified:" : "complete:")
        + highs.modelStatusToString(highs.getModelStatus());
    const auto& solution = highs.getSolution();
    if (solution.value_valid) {
        for (int i = 0; i < m; ++i) {
            if (solution.col_value[i] >= 0.5) result.selected.push_back(subset[i]);
        }
    }
    highs.resetGlobalScheduler(true);
    return result;
}

std::uint64_t hash_result(const Result& result) {
    std::uint64_t hash = 1469598103934665603ULL;
    auto mix = [&](std::uint64_t value) {
        hash ^= value;
        hash *= 1099511628211ULL;
    };
    mix(static_cast<std::uint64_t>(result.sites));
    mix(static_cast<std::uint64_t>(result.instance));
    mix(std::bit_cast<std::uint64_t>(result.best_objective_mw));
    for (const int site : result.selected) mix(static_cast<std::uint64_t>(site));
    return hash;
}

}  // namespace

Problem::Problem(
    int sites,
    int instance,
    int workers,
    const std::filesystem::path& matrix_cache
) : sites_(sites), instance_(instance) {
    if (
        sites < 2 || instance < 0 || instance >= 10 || workers < 1
    ) {
        throw std::invalid_argument("invalid T30 problem configuration");
    }
    positions_.resize(static_cast<std::size_t>(sites_));
    const std::uint64_t root =
        0x2016f15cULL ^ static_cast<std::uint64_t>(sites_) << 16U
        ^ static_cast<std::uint64_t>(instance_);
    for (int site = 0; site < sites_; ++site) {
        positions_[site] = {
            kSide * unit(root + static_cast<std::uint64_t>(2 * site)),
            kSide * unit(root + static_cast<std::uint64_t>(2 * site + 1)),
        };
    }
    free_power_mw_ = average_free_power();
    const std::size_t pairs =
        static_cast<std::size_t>(sites_) * (sites_ - 1) / 2U;
    packed_loss_.resize(pairs);
    const auto start = Clock::now();
    if (!read_cache(matrix_cache, sites_, instance_, packed_loss_)) {
        const auto lookup = loss_lookup();
        fode::PersistentExecutor executor(workers);
        executor.parallel_for(0, sites_, [&](int first) {
            for (int second = first + 1; second < sites_; ++second) {
                const double dx =
                    positions_[second].x_m - positions_[first].x_m;
                const double dy =
                    positions_[second].y_m - positions_[first].y_m;
                const double distance = std::hypot(dx, dy);
                float loss = 0.0F;
                if (distance < kSpacing) {
                    loss = static_cast<float>(kBig);
                } else {
                    double bearing = std::atan2(dy, dx)
                        * 180.0 / std::numbers::pi;
                    if (bearing < 0.0) bearing += 360.0;
                    const int angle_bin = static_cast<int>(
                        std::llround(bearing / 5.0)
                    ) % kBearingBins;
                    const int distance_bin = std::clamp(
                        static_cast<int>(std::llround(distance / 5.0)),
                        0, kDistanceBins - 1
                    );
                    loss = lookup[
                        static_cast<std::size_t>(angle_bin) * kDistanceBins
                        + distance_bin
                    ];
                }
                packed_loss_[packed_index(sites_, first, second)] = loss;
            }
        });
        matrix_observed_workers_ =
            executor.work_receipt().distinct_participants;
        write_cache(matrix_cache, sites_, instance_, packed_loss_);
    }
    matrix_seconds_ =
        std::chrono::duration<double>(Clock::now() - start).count();
    matrix_hash_ = 1469598103934665603ULL;
    for (const float value : packed_loss_) {
        matrix_hash_ ^= std::bit_cast<std::uint32_t>(value);
        matrix_hash_ *= 1099511628211ULL;
    }
}

int Problem::size() const noexcept { return sites_; }
const std::vector<Position>& Problem::positions() const noexcept {
    return positions_;
}
double Problem::free_power_mw(int) const noexcept { return free_power_mw_; }
double Problem::pair_loss_mw(int first, int second) const noexcept {
    if (first == second) return 0.0;
    return packed_loss_[packed_index(sites_, first, second)];
}
bool Problem::spacing_conflict(int first, int second) const noexcept {
    return first != second && pair_loss_mw(first, second) >= 1000.0;
}
double Problem::evaluate(const std::vector<int>& selected) const {
    double value = free_power_mw_ * selected.size();
    for (std::size_t i = 0; i < selected.size(); ++i) {
        for (std::size_t j = i + 1; j < selected.size(); ++j) {
            value -= pair_loss_mw(selected[i], selected[j]);
        }
    }
    return value;
}
double Problem::matrix_seconds() const noexcept { return matrix_seconds_; }
int Problem::matrix_observed_workers() const noexcept {
    return matrix_observed_workers_;
}
std::uint64_t Problem::matrix_hash() const noexcept { return matrix_hash_; }

Result run(const Problem& problem, const Configuration& config) {
    if (config.workers < 1 || config.time_limit_seconds <= 0.0) {
        throw std::invalid_argument("invalid T30 run configuration");
    }
    const auto end_to_end_start = Clock::now();
    fode::PersistentExecutor executor(config.workers);
    SearchState state;
    state.active.assign(static_cast<std::size_t>(problem.size()), 0);
    state.contribution.assign(static_cast<std::size_t>(problem.size()), 0.0);
    state.objective = 0.0;
    std::uint64_t delta_evaluations = 0;
    std::uint64_t moves = 0;
    const auto optimization_start = Clock::now();
    SearchState best = state;

    const auto local_start = Clock::now();
    const double local_budget = config.fixed_moves > 0
        ? config.time_limit_seconds
        : std::min(60.0, std::max(0.05, 0.10 * config.time_limit_seconds));
    const auto deadline = optimization_start
        + std::chrono::duration_cast<Clock::duration>(
            std::chrono::duration<double>(local_budget)
        );
    std::mt19937_64 rng(config.seed);
    const std::uint64_t move_limit = config.fixed_moves > 0
        ? config.fixed_moves
        : std::numeric_limits<std::uint64_t>::max();
    auto local_mode = [&](int stagnation_limit) {
        int stagnant = 0;
        int local_minimum = 0;
        int local_maximum = std::numeric_limits<int>::max();
        while (
            moves < move_limit && Clock::now() < deadline
            && stagnant < stagnation_limit
        ) {
            if (best_flip(
                    problem, state, local_minimum, local_maximum,
                    delta_evaluations)) {
                ++moves;
                if (accept_incumbent(problem, state, best)) stagnant = 0;
                else ++stagnant;
                continue;
            }
            while (
                moves < move_limit && Clock::now() < deadline
                && best_swap(problem, state, delta_evaluations)
            ) {
                ++moves;
                if (accept_incumbent(problem, state, best)) stagnant = 0;
                else ++stagnant;
            }
            if (moves >= move_limit || Clock::now() >= deadline) break;
            const double rho = std::generate_canonical<double, 53>(rng);
            const int cardinality = static_cast<int>(state.selected.size());
            const int incumbent_cardinality =
                static_cast<int>(best.selected.size());
            int forced = cardinality <= incumbent_cardinality
                ? static_cast<int>(cardinality * (1.0 + rho / 2.0) + 10.0)
                : static_cast<int>(cardinality * (1.0 - rho / 2.0) - 10.0);
            forced = std::clamp(forced, 0, problem.size());
            local_minimum = forced;
            local_maximum = forced;
        }
        while (
            moves < move_limit && Clock::now() < deadline
            && best_swap(problem, best, delta_evaluations)
        ) {
            ++moves;
            state = best;
        }
    };
    local_mode(10000);
    const double initial = best.objective;
    state = best;
    local_mode(100);
    const double local_seconds =
        std::chrono::duration<double>(Clock::now() - local_start).count();

    MipResult mip;
    double mip_seconds = 0.0;
    if (config.fixed_moves == 0 || config.fixed_moves >= moves) {
        // The paper first solves the simplified proximity model, with
        // interference constraints removed, and judges every candidate using
        // the true Jensen objective. It switches permanently to the complete
        // model when cardinality no longer increases or true profit falls.
        bool simplified = true;
        int proximity_calls = 0;
        while (proximity_calls < 64) {
            const double elapsed = std::chrono::duration<double>(
                Clock::now() - optimization_start
            ).count();
            const double remaining = config.time_limit_seconds - elapsed;
            if (remaining <= 0.05) break;
            const double call_limit = simplified
                ? std::min(60.0, remaining)
                : remaining;
            const int incumbent_turbines = static_cast<int>(best.selected.size());
            const double incumbent_objective = best.objective;
            auto stage = proximity_mip(
                problem, best, config, call_limit, simplified
            );
            mip_seconds += stage.seconds;
            if (!mip.status.empty()) mip.status += ";";
            mip.status += stage.status;
            mip.selected = stage.selected;
            ++proximity_calls;

            bool accepted = false;
            if (!stage.selected.empty()) {
                const double objective = problem.evaluate(stage.selected);
                if (objective > best.objective + 1e-10) {
                    best.active.assign(
                        static_cast<std::size_t>(problem.size()), 0
                    );
                    for (const int site : stage.selected) best.active[site] = 1;
                    best.selected = std::move(stage.selected);
                    best.objective = objective;
                    accepted = true;
                }
            }
            if (simplified) {
                const bool increased_cardinality =
                    static_cast<int>(mip.selected.size()) > incumbent_turbines;
                const bool improved_true_profit =
                    accepted && best.objective > incumbent_objective + 1e-10;
                simplified =
                    increased_cardinality && improved_true_profit;
                // The next call stays simplified only while both paper switch
                // conditions remain false. Once switched, all later calls use
                // the complete formulation.
                continue;
            }
            break;
        }
    }
    std::sort(best.selected.begin(), best.selected.end());
    Result result;
    result.sites = problem.size();
    result.instance = config.instance;
    result.turbines = static_cast<int>(best.selected.size());
    result.requested_workers = config.workers;
    result.observed_workers = std::max(
        problem.matrix_observed_workers(),
        executor.work_receipt().distinct_participants
    );
    result.initial_objective_mw = initial;
    result.best_objective_mw = best.objective;
    result.minimum_spacing_m = minimum_spacing(problem, best.selected);
    result.matrix_seconds = problem.matrix_seconds();
    result.local_search_seconds = local_seconds;
    result.mip_seconds = mip_seconds;
    result.optimization_seconds =
        std::chrono::duration<double>(Clock::now() - optimization_start).count();
    result.end_to_end_seconds =
        std::chrono::duration<double>(Clock::now() - end_to_end_start).count();
    result.pair_evaluations =
        static_cast<std::uint64_t>(problem.size())
        * static_cast<std::uint64_t>(problem.size() - 1) / 2U;
    result.delta_evaluations = delta_evaluations;
    result.moves = moves;
    result.selected = best.selected;
    result.mip_status = mip.status;
    result.scientific_hash = hash_result(result) ^ problem.matrix_hash();
    if (
        result.minimum_spacing_m + 1e-8 < kSpacing
        || std::abs(problem.evaluate(result.selected) - result.best_objective_mw)
            > 1e-8
    ) {
        throw std::runtime_error("T30 final semantic validation failed");
    }
    return result;
}

std::string to_json(const Result& result) {
    std::ostringstream out;
    out << std::setprecision(17)
        << "{\"schema_version\":1,\"corpus_id\":\"T30\","
        << "\"method_semantic_id\":"
        << "\"fischetti2016_proxy_highs_reconstruction_v1\","
        << "\"problem_semantic_id\":"
        << "\"fischetti2016_random50_jensen_declared_v1\","
        << "\"protocol_semantic_id\":\"fischetti2016_5x10x7_proxy_v1\","
        << "\"sites\":" << result.sites
        << ",\"instance\":" << result.instance
        << ",\"turbines\":" << result.turbines
        << ",\"requested_workers\":" << result.requested_workers
        << ",\"observed_workers\":" << result.observed_workers
        << ",\"initial_objective_mw\":" << result.initial_objective_mw
        << ",\"best_objective_mw\":" << result.best_objective_mw
        << ",\"minimum_spacing_m\":" << result.minimum_spacing_m
        << ",\"matrix_seconds\":" << result.matrix_seconds
        << ",\"local_search_seconds\":" << result.local_search_seconds
        << ",\"mip_seconds\":" << result.mip_seconds
        << ",\"optimization_seconds\":" << result.optimization_seconds
        << ",\"end_to_end_seconds\":" << result.end_to_end_seconds
        << ",\"pair_evaluations\":" << result.pair_evaluations
        << ",\"delta_evaluations\":" << result.delta_evaluations
        << ",\"moves\":" << result.moves
        << ",\"mip_status\":\"" << result.mip_status << "\""
        << ",\"scientific_hash\":" << result.scientific_hash
        << ",\"selected\":[";
    for (std::size_t i = 0; i < result.selected.size(); ++i) {
        if (i) out << ",";
        out << result.selected[i];
    }
    out << "]}";
    return out.str();
}

}  // namespace core99::t30
