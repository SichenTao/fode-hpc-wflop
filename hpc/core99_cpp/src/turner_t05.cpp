/*
WFLOP IMPLEMENTATION FACT DECLARATION
Implementation unit: T05 pure-C++ mathematical-programming backend
Paper DOI: 10.1016/j.renene.2013.10.023.
Paper/source/missing/reconstruction/semantic IDs:
hpc/core99_cpp/include/core99/turner_t05.hpp.
Public source: none found; legally accessible publisher PDF consumed.
Missing: author CPLEX models, callbacks and numeric Figure-5 array.
Reconstruction: deterministic open QIP solver and digitized Case-C profile.
Claim boundary: equation-level declared open-solver reconstruction, not CPLEX
or author numerical replay.
Contract: shared/contracts/core99_t05_turner_math_programming_2014.json.
Last evidence-audit date: 2026-07-31
END WFLOP IMPLEMENTATION FACT DECLARATION
*/
#include "core99/turner_t05.hpp"

#include "fode/executor.hpp"
#include "fode/rng.hpp"

#include <algorithm>
#include <atomic>
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
#include <utility>
#include <vector>

namespace core99::t05 {
namespace {

using Clock = std::chrono::steady_clock;
constexpr int grid_side = 10;
constexpr int candidate_count = 100;
constexpr double rotor_radius_m = 20.0;
constexpr double cell_width_m = 200.0;
constexpr double thrust_coefficient = 0.88;
constexpr double entrainment = 0.1;
constexpr int default_multistarts = 4096;
constexpr std::uint64_t default_node_limit = 200000;
constexpr const char* method_id =
    "t05_turner_qip_milp_bounding_declared_open_solver_v1";

double elapsed(const Clock::time_point start) {
    return std::chrono::duration<double>(Clock::now() - start).count();
}

std::uint64_t mix_hash(std::uint64_t hash, const std::uint64_t value) {
    hash ^= value + 0x9e3779b97f4a7c15ULL + (hash << 6U) + (hash >> 2U);
    return hash;
}

std::vector<WindState> case_a_wind() {
    return {{0.0, 12.0, 1.0}};
}

std::vector<WindState> case_b_wind() {
    std::vector<WindState> states;
    for (int direction = 0; direction < 36; ++direction) {
        states.push_back({10.0 * direction, 12.0, 1.0 / 36.0});
    }
    return states;
}

std::vector<WindState> case_c_wind() {
    // Figure 5 does not publish numeric values. These visually digitized,
    // versioned profiles preserve the three bar families and the southwest
    // peak. They are normalized jointly after construction.
    constexpr double directional_profile_17[36]{
        1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,
        1,1,1,1,1,1,1,1,1.2,1.55,1.7,2.7,3.2,2.7,1.7,1.55,1.2,1
    };
    constexpr double directional_profile_12[36]{
        1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,
        1,1,1,1,1,1,1,1,1.2,1.45,1.75,1.7,2.35,1.7,1.75,1.45,1.2,1
    };
    std::vector<WindState> states;
    states.reserve(108);
    double total = 0.0;
    for (int direction = 0; direction < 36; ++direction) {
        const double p8 = 0.005;
        const double p12 = 0.008 * directional_profile_12[direction];
        const double p17 = 0.011 * directional_profile_17[direction];
        states.push_back({10.0 * direction, 8.0, p8});
        states.push_back({10.0 * direction, 12.0, p12});
        states.push_back({10.0 * direction, 17.0, p17});
        total += p8 + p12 + p17;
    }
    for (auto& state : states) state.probability /= total;
    return states;
}

PaperCase make_case(const std::string& case_id) {
    struct Spec {
        const char* id;
        const char* semantic;
        const char* formulation;
        int turbines;
        char wind;
        double published_power;
    };
    constexpr Spec specs[]{
        {"t05_case_a_k26", "t05_case_a_grid100_v1", "MILP", 26, 'A', 12980},
        {"t05_case_a_k30", "t05_case_a_grid100_v1", "MILP", 30, 'A', 14800},
        {"t05_case_b_k19", "t05_case_b_grid100_v1", "MILP", 19, 'B', 9549},
        {"t05_case_b_k39", "t05_case_b_grid100_v1", "QIP+neighbor_bound", 39, 'B', 18336},
        {"t05_case_c_k15", "t05_case_c_figure5_digitized_grid100_v1", "MILP", 15, 'C', 13494},
        {"t05_case_c_k39", "t05_case_c_figure5_digitized_grid100_v1", "QIP+neighbor_bound", 39, 'C', 32453},
    };
    for (const auto& spec : specs) {
        if (case_id != spec.id) continue;
        return {
            .case_id = spec.id,
            .problem_semantic_id = spec.semantic,
            .formulation = spec.formulation,
            .turbine_count = spec.turbines,
            .wind_states = spec.wind == 'A' ? case_a_wind()
                : (spec.wind == 'B' ? case_b_wind() : case_c_wind()),
            .published_power_kw = spec.published_power,
        };
    }
    throw std::invalid_argument("invalid T05 case id " + case_id);
}

double pair_deficit_squared(
    const int source,
    const int target,
    const double from_degrees
) {
    if (source == target) return 0.0;
    const double radians = from_degrees
        * std::numbers::pi / 180.0;
    const double flow_x = -std::sin(radians);
    const double flow_y = -std::cos(radians);
    const double source_x = (source % grid_side) * cell_width_m;
    const double source_y = (source / grid_side) * cell_width_m;
    const double target_x = (target % grid_side) * cell_width_m;
    const double target_y = (target / grid_side) * cell_width_m;
    const double delta_x = target_x - source_x;
    const double delta_y = target_y - source_y;
    const double downstream = delta_x * flow_x + delta_y * flow_y;
    if (downstream <= 1.0e-12) return 0.0;
    const double crosswind = std::abs(delta_x * flow_y - delta_y * flow_x);
    if (crosswind > rotor_radius_m + entrainment * downstream) return 0.0;
    const double induction =
        0.5 * (1.0 - std::sqrt(1.0 - thrust_coefficient));
    const double deficit = 2.0 * induction
        / std::pow(1.0 + entrainment * downstream / rotor_radius_m, 2.0);
    return deficit * deficit;
}

std::vector<double> build_interaction(
    const PaperCase& paper_case,
    fode::PersistentExecutor* executor
) {
    std::vector<double> directed(candidate_count * candidate_count, 0.0);
    executor->parallel_for(0, candidate_count, [&](const int source) {
        for (int target = 0; target < candidate_count; ++target) {
            double value = 0.0;
            for (const auto& state : paper_case.wind_states) {
                value += state.probability * pair_deficit_squared(
                    source, target, state.from_degrees
                );
            }
            directed[source * candidate_count + target] = value;
        }
    });
    std::vector<double> symmetric(candidate_count * candidate_count, 0.0);
    executor->parallel_for(0, candidate_count, [&](const int i) {
        for (int j = i + 1; j < candidate_count; ++j) {
            const double value =
                directed[i * candidate_count + j]
                + directed[j * candidate_count + i];
            symmetric[i * candidate_count + j] = value;
            symmetric[j * candidate_count + i] = value;
        }
    });
    return symmetric;
}

double layout_objective(
    const std::vector<double>& interaction,
    const Layout& layout
) {
    double value = 0.0;
    for (std::size_t a = 0; a < layout.size(); ++a) {
        for (std::size_t b = a + 1; b < layout.size(); ++b) {
            value += interaction[
                layout[a] * candidate_count + layout[b]
            ];
        }
    }
    return value;
}

Layout random_layout(
    const int turbine_count,
    const fode::CounterRng& rng,
    const std::uint64_t start
) {
    std::vector<std::pair<double, int>> priorities(candidate_count);
    for (int cell = 0; cell < candidate_count; ++cell) {
        priorities[cell] = {
            rng.uniform(0, 505, start, cell),
            cell,
        };
    }
    std::nth_element(
        priorities.begin(),
        priorities.begin() + turbine_count,
        priorities.end()
    );
    Layout result;
    result.reserve(turbine_count);
    for (int i = 0; i < turbine_count; ++i) {
        result.push_back(priorities[i].second);
    }
    std::sort(result.begin(), result.end());
    return result;
}

struct SearchCandidate {
    Layout layout;
    double objective = std::numeric_limits<double>::infinity();
    std::uint64_t evaluations = 0;
};

SearchCandidate one_swap_local_search(
    Layout layout,
    const std::vector<double>& interaction
) {
    SearchCandidate result{layout, layout_objective(interaction, layout), 0};
    std::vector<unsigned char> selected(candidate_count, 0);
    for (const int cell : layout) selected[cell] = 1;
    for (int pass = 0; pass < candidate_count; ++pass) {
        double best_delta = -1.0e-15;
        int remove_cell = -1;
        int add_cell = -1;
        for (const int occupied : result.layout) {
            for (int vacant = 0; vacant < candidate_count; ++vacant) {
                if (selected[vacant]) continue;
                double delta = 0.0;
                for (const int other : result.layout) {
                    if (other == occupied) continue;
                    delta += interaction[
                        vacant * candidate_count + other
                    ] - interaction[
                        occupied * candidate_count + other
                    ];
                }
                ++result.evaluations;
                if (
                    delta < best_delta
                    || (
                        std::abs(delta - best_delta) <= 1.0e-15
                        && std::pair(vacant, occupied)
                            < std::pair(add_cell, remove_cell)
                    )
                ) {
                    best_delta = delta;
                    remove_cell = occupied;
                    add_cell = vacant;
                }
            }
        }
        if (remove_cell < 0) break;
        selected[remove_cell] = 0;
        selected[add_cell] = 1;
        auto position = std::find(
            result.layout.begin(), result.layout.end(), remove_cell
        );
        *position = add_cell;
        std::sort(result.layout.begin(), result.layout.end());
        result.objective += best_delta;
    }
    result.objective = layout_objective(interaction, result.layout);
    return result;
}

bool candidate_better(
    const SearchCandidate& lhs,
    const SearchCandidate& rhs
) {
    if (lhs.objective < rhs.objective - 1.0e-15) return true;
    if (lhs.objective > rhs.objective + 1.0e-15) return false;
    return lhs.layout < rhs.layout;
}

struct Node {
    Layout selected;
    int next = 0;
    double cost = 0.0;
    double bound = 0.0;
};

double node_bound(
    const Node& node,
    const int target_count,
    const std::vector<double>& interaction
) {
    const int needed = target_count - static_cast<int>(node.selected.size());
    if (needed <= 0) return node.cost;
    std::vector<double> increments;
    increments.reserve(candidate_count - node.next);
    for (int candidate = node.next; candidate < candidate_count; ++candidate) {
        double increment = 0.0;
        for (const int selected : node.selected) {
            increment += interaction[
                candidate * candidate_count + selected
            ];
        }
        increments.push_back(increment);
    }
    if (static_cast<int>(increments.size()) < needed) {
        return std::numeric_limits<double>::infinity();
    }
    if (needed < static_cast<int>(increments.size())) {
        std::nth_element(
            increments.begin(),
            increments.begin() + needed,
            increments.end()
        );
    }
    return node.cost + std::accumulate(
        increments.begin(), increments.begin() + needed, 0.0
    );
}

bool node_less(const Node& lhs, const Node& rhs) {
    if (lhs.bound != rhs.bound) return lhs.bound < rhs.bound;
    if (lhs.selected.size() != rhs.selected.size()) {
        return lhs.selected.size() > rhs.selected.size();
    }
    if (lhs.next != rhs.next) return lhs.next < rhs.next;
    return lhs.selected < rhs.selected;
}

struct BranchResult {
    SearchCandidate best;
    double lower_bound = 0.0;
    std::uint64_t explored = 0;
    bool exact = false;
};

BranchResult branch_and_bound(
    SearchCandidate incumbent,
    const int turbine_count,
    const std::uint64_t node_limit,
    const std::vector<double>& interaction,
    fode::PersistentExecutor* executor
) {
    Node root;
    root.bound = node_bound(root, turbine_count, interaction);
    std::vector<Node> frontier{root};
    std::uint64_t explored = 0;
    // A worker-count-independent frontier batch preserves the same explored
    // node set and scientific result for one-core and all-core execution.
    constexpr int deterministic_batch = 4096;
    while (!frontier.empty() && explored < node_limit) {
        std::sort(frontier.begin(), frontier.end(), node_less);
        if (frontier.front().bound >= incumbent.objective - 1.0e-15) {
            frontier.clear();
            break;
        }
        const int batch = std::min<int>(
            static_cast<int>(frontier.size()), deterministic_batch
        );
        std::vector<Node> active(
            frontier.begin(), frontier.begin() + batch
        );
        frontier.erase(frontier.begin(), frontier.begin() + batch);
        std::vector<std::vector<Node>> generated(batch);
        std::vector<SearchCandidate> complete(batch);
        executor->parallel_for(0, batch, [&](const int index) {
            const Node& node = active[index];
            if (
                node.next >= candidate_count
                || node.bound >= incumbent.objective - 1.0e-15
            ) return;
            const int remaining = candidate_count - node.next;
            const int needed = turbine_count
                - static_cast<int>(node.selected.size());
            if (remaining < needed) return;

            Node include = node;
            double addition = 0.0;
            for (const int selected : include.selected) {
                addition += interaction[
                    node.next * candidate_count + selected
                ];
            }
            include.selected.push_back(node.next);
            include.cost += addition;
            ++include.next;
            if (static_cast<int>(include.selected.size()) == turbine_count) {
                complete[index] = {
                    include.selected,
                    include.cost,
                    0,
                };
            } else {
                include.bound = node_bound(
                    include, turbine_count, interaction
                );
                generated[index].push_back(std::move(include));
            }

            if (remaining > needed) {
                Node exclude = node;
                ++exclude.next;
                exclude.bound = node_bound(
                    exclude, turbine_count, interaction
                );
                generated[index].push_back(std::move(exclude));
            }
        });
        explored += active.size();
        for (int index = 0; index < batch; ++index) {
            if (
                !complete[index].layout.empty()
                && candidate_better(complete[index], incumbent)
            ) {
                incumbent = std::move(complete[index]);
            }
            for (auto& child : generated[index]) {
                if (child.bound < incumbent.objective - 1.0e-15) {
                    frontier.push_back(std::move(child));
                }
            }
        }
    }
    double lower = incumbent.objective;
    if (!frontier.empty()) {
        lower = std::min_element(
            frontier.begin(), frontier.end(), node_less
        )->bound;
    }
    return {
        .best = std::move(incumbent),
        .lower_bound = lower,
        .explored = explored,
        .exact = frontier.empty(),
    };
}

double expected_power(
    const Layout& layout,
    const std::vector<WindState>& wind_states,
    fode::PersistentExecutor* executor
) {
    std::vector<double> state_power(wind_states.size(), 0.0);
    executor->parallel_for(
        0, static_cast<int>(wind_states.size()), [&](const int state_index) {
        const auto& state = wind_states[state_index];
        double farm_power = 0.0;
        for (const int target : layout) {
            double squared_deficit = 0.0;
            for (const int source : layout) {
                squared_deficit += pair_deficit_squared(
                    source, target, state.from_degrees
                );
            }
            const double velocity = state.speed_mps * std::max(
                0.0, 1.0 - std::sqrt(squared_deficit)
            );
            farm_power += 0.3 * velocity * velocity * velocity;
        }
        state_power[state_index] = state.probability * farm_power;
    });
    return std::accumulate(state_power.begin(), state_power.end(), 0.0);
}

}  // namespace

Problem::Problem(std::string case_id) : case_(make_case(case_id)) {
    fode::PersistentExecutor serial(1);
    interaction_ = build_interaction(case_, &serial);
}

const PaperCase& Problem::paper_case() const noexcept {
    return case_;
}

bool Problem::feasible(const Layout& layout) const noexcept {
    if (static_cast<int>(layout.size()) != case_.turbine_count) return false;
    if (!std::is_sorted(layout.begin(), layout.end())) return false;
    return std::adjacent_find(layout.begin(), layout.end()) == layout.end()
        && layout.front() >= 0 && layout.back() < candidate_count;
}

double Problem::qip_objective(const Layout& layout) const {
    if (!feasible(layout)) {
        throw std::invalid_argument("T05 infeasible layout");
    }
    return layout_objective(interaction_, layout);
}

double Problem::milp_linearized_objective(const Layout& layout) const {
    if (!feasible(layout)) {
        throw std::invalid_argument("T05 infeasible layout");
    }
    std::vector<unsigned char> y(candidate_count, 0);
    for (const int cell : layout) y[cell] = 1;
    double objective = 0.0;
    for (int i = 0; i < candidate_count; ++i) {
        for (int j = i + 1; j < candidate_count; ++j) {
            const int x_ij = y[i] && y[j];
            if (x_ij > y[i] || x_ij > y[j]
                || x_ij < y[i] + y[j] - 1) {
                throw std::logic_error("T05 MILP linearization violation");
            }
            objective += interaction_[i * candidate_count + j] * x_ij;
        }
    }
    return objective;
}

double Problem::expected_power_kw(const Layout& layout) const {
    if (!feasible(layout)) {
        throw std::invalid_argument("T05 infeasible layout");
    }
    fode::PersistentExecutor serial(1);
    return expected_power(layout, case_.wind_states, &serial);
}

RunResult Problem::optimize(const RunConfig& config) const {
    if (config.workers < 1 || config.workers > 256) {
        throw std::invalid_argument("T05 workers outside [1,256]");
    }
    const int starts = config.multistarts < 0
        ? default_multistarts : config.multistarts;
    const std::uint64_t node_limit = config.node_limit == 0
        ? default_node_limit : config.node_limit;
    if (starts < 1 || node_limit < 1) {
        throw std::invalid_argument("T05 invalid search budget");
    }
    const auto total_start = Clock::now();
    fode::PersistentExecutor executor(config.workers);
    executor.reset_work_receipt();

    const auto assembly_start = Clock::now();
    const auto interaction = build_interaction(case_, &executor);
    const double assembly_seconds = elapsed(assembly_start);

    const auto incumbent_start = Clock::now();
    const fode::CounterRng rng(config.seed);
    std::vector<SearchCandidate> candidates(starts);
    executor.parallel_for(0, starts, [&](const int start) {
        candidates[start] = one_swap_local_search(
            random_layout(case_.turbine_count, rng, start),
            interaction
        );
    });
    SearchCandidate incumbent = candidates.front();
    std::uint64_t local_evaluations = 0;
    for (const auto& candidate : candidates) {
        local_evaluations += candidate.evaluations;
        if (candidate_better(candidate, incumbent)) incumbent = candidate;
    }
    const double incumbent_seconds = elapsed(incumbent_start);

    const auto branch_start = Clock::now();
    auto branch = branch_and_bound(
        incumbent, case_.turbine_count, node_limit, interaction, &executor
    );
    const double branch_seconds = elapsed(branch_start);

    const auto local_start = Clock::now();
    auto polished = one_swap_local_search(branch.best.layout, interaction);
    local_evaluations += polished.evaluations;
    if (candidate_better(polished, branch.best)) {
        branch.best = std::move(polished);
    }
    const double local_seconds = elapsed(local_start);

    const auto power_start = Clock::now();
    const double power = expected_power(
        branch.best.layout, case_.wind_states, &executor
    );
    const double power_seconds = elapsed(power_start);
    const double milp_objective = [&]() {
        double value = 0.0;
        std::vector<unsigned char> occupied(candidate_count, 0);
        for (const int cell : branch.best.layout) occupied[cell] = 1;
        for (int i = 0; i < candidate_count; ++i) {
            for (int j = i + 1; j < candidate_count; ++j) {
                value += interaction[i * candidate_count + j]
                    * static_cast<double>(occupied[i] && occupied[j]);
            }
        }
        return value;
    }();
    const double denominator = std::max(
        std::abs(branch.best.objective), 1.0e-15
    );
    const double gap = std::max(
        0.0, (branch.best.objective - branch.lower_bound) / denominator
    );
    const auto receipt = executor.work_receipt();
    std::uint64_t hash = 1469598103934665603ULL;
    for (const int cell : branch.best.layout) hash = mix_hash(hash, cell);
    hash = mix_hash(hash, std::bit_cast<std::uint64_t>(
        branch.best.objective
    ));
    hash = mix_hash(hash, std::bit_cast<std::uint64_t>(power));
    hash = mix_hash(hash, branch.explored);
    return {
        .case_id = case_.case_id,
        .problem_semantic_id = case_.problem_semantic_id,
        .method_semantic_id = method_id,
        .formulation = case_.formulation,
        .seed = config.seed,
        .requested_workers = config.workers,
        .observed_workers = receipt.distinct_participants,
        .turbine_count = case_.turbine_count,
        .multistarts = starts,
        .node_limit = node_limit,
        .explored_nodes = branch.explored,
        .local_candidate_evaluations = local_evaluations,
        .qip_objective = branch.best.objective,
        .milp_linearized_objective = milp_objective,
        .admissible_lower_bound = branch.lower_bound,
        .relative_gap = gap,
        .expected_power_kw = power,
        .published_power_kw = case_.published_power_kw,
        .exact_certificate = branch.exact,
        .best_layout = branch.best.layout,
        .interaction_assembly_seconds = assembly_seconds,
        .incumbent_search_seconds = incumbent_seconds,
        .branch_and_bound_seconds = branch_seconds,
        .local_search_seconds = local_seconds,
        .power_evaluation_seconds = power_seconds,
        .end_to_end_seconds = elapsed(total_start),
        .scientific_hash = hash,
    };
}

std::vector<std::string> paper_case_ids() {
    return {
        "t05_case_a_k26",
        "t05_case_a_k30",
        "t05_case_b_k19",
        "t05_case_b_k39",
        "t05_case_c_k15",
        "t05_case_c_k39",
    };
}

}  // namespace core99::t05
