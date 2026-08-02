/*
WFLOP IMPLEMENTATION FACT DECLARATION
Implementation unit: T63 pure-C++ field generation, exact MILP linearization,
and iterative CFD-row update lifecycle
Paper/DOI: Wind Farm Layout Optimization on Complex Terrains - Integrating a
CFD Wake Model with Mixed-Integer Programming;
10.1016/j.apenergy.2016.06.085
Public source: no author source/data; MILP backend is HiGHS v1.15.1 commit
04024d701f79feb8e2f18bc3df0dffc04ef05088, MIT
Missing/conflicts/reconstruction: include/core99/kuo_t63.hpp
Semantic IDs: t63_carleton_figure_proxy_cfd_surrogate_v1;
t63_iterative_cfd_mip_highs_reconstruction_v1
Controlling contract: shared/contracts/core99_t63_kuo_2016.json
HPC design: all source-target approximate and terrain-aware rows are generated
once over a persistent case-local team; compact row-major arrays feed an exact
binary-product MILP linearization; sequential MIP iterations preserve the
paper lifecycle while five independent relaxation cases partition node cores
Claim boundary: academic declared proxy reproduction, not author CFD/Gurobi
Last evidence-audit date: 2026-07-31
END WFLOP IMPLEMENTATION FACT DECLARATION
*/
#include "core99/kuo_t63.hpp"

#include "Highs.h"

#include <algorithm>
#include <array>
#include <bit>
#include <chrono>
#include <cmath>
#include <cstddef>
#include <cstdint>
#include <fstream>
#include <limits>
#include <numbers>
#include <stdexcept>
#include <string>
#include <utility>
#include <vector>

namespace core99::t63 {
namespace {

using Clock = std::chrono::steady_clock;
constexpr int kGrid = 20;
constexpr int kCells = kGrid * kGrid;
constexpr int kSectors = 12;
constexpr int kTurbines = 20;
constexpr double kCellPitchM = 140.0;
constexpr double kRotorDiameterM = 80.0;
constexpr double kHubHeightM = 77.0;
constexpr double kMinimumSpacingM = 5.0 * kRotorDiameterM;
constexpr double kThrustCoefficient = 0.8;
constexpr double kPi = std::numbers::pi_v<double>;
constexpr double kInfinity = 1.0e30;

template <typename T>
T read_scalar(std::ifstream& stream) {
    T value{};
    stream.read(reinterpret_cast<char*>(&value), sizeof(T));
    if (!stream) throw std::runtime_error("truncated T63 proxy");
    return value;
}

std::vector<float> read_array(std::ifstream& stream, const std::size_t count) {
    std::vector<float> values(count);
    stream.read(
        reinterpret_cast<char*>(values.data()),
        static_cast<std::streamsize>(count * sizeof(float))
    );
    if (!stream) throw std::runtime_error("truncated T63 proxy array");
    return values;
}

struct Pair {
    int left;
    int right;
    double loss;
};

struct MipReceipt {
    std::vector<int> selected;
    double objective = 0.0;
    double dual_bound = 0.0;
    double gap = 0.0;
    double seconds = 0.0;
    std::string status;
};

std::uint64_t hash_result(
    const std::vector<int>& layout,
    const double objective,
    const double relaxation
) {
    std::uint64_t hash = 1469598103934665603ULL;
    for (const int cell : layout) {
        hash ^= static_cast<std::uint64_t>(cell);
        hash *= 1099511628211ULL;
    }
    hash ^= std::bit_cast<std::uint64_t>(objective);
    hash *= 1099511628211ULL;
    hash ^= std::bit_cast<std::uint64_t>(relaxation);
    hash *= 1099511628211ULL;
    return hash;
}

double objective_for_layout(
    const std::vector<int>& layout,
    const std::vector<double>& base,
    const std::vector<double>& loss
) {
    double objective = 0.0;
    for (const int cell : layout) objective += base[cell];
    for (std::size_t left = 0; left < layout.size(); ++left) {
        for (std::size_t right = left + 1U; right < layout.size(); ++right) {
            const int i = layout[left];
            const int j = layout[right];
            objective -= loss[static_cast<std::size_t>(i) * kCells + j];
            objective -= loss[static_cast<std::size_t>(j) * kCells + i];
        }
    }
    return objective;
}

void append_row(
    HighsSparseMatrix& matrix,
    std::vector<double>& lower,
    std::vector<double>& upper,
    const std::initializer_list<std::pair<int, double>>& entries,
    const double row_lower,
    const double row_upper
) {
    for (const auto& [column, value] : entries) {
        matrix.index_.push_back(column);
        matrix.value_.push_back(value);
    }
    matrix.start_.push_back(static_cast<HighsInt>(matrix.index_.size()));
    lower.push_back(row_lower);
    upper.push_back(row_upper);
}

MipReceipt solve_mip(
    const Problem& problem,
    const std::vector<double>& base,
    const std::vector<double>& directed_loss,
    const int workers,
    const double time_limit_seconds
) {
    std::vector<Pair> pairs;
    pairs.reserve(50000U);
    for (int left = 0; left < kCells; ++left) {
        for (int right = left + 1; right < kCells; ++right) {
            const double combined =
                directed_loss[static_cast<std::size_t>(left) * kCells + right]
                + directed_loss[
                    static_cast<std::size_t>(right) * kCells + left
                ];
            if (combined > 1.0e-10) {
                pairs.push_back({left, right, combined});
            }
        }
    }

    HighsModel model;
    const int columns = kCells + static_cast<int>(pairs.size());
    model.lp_.num_col_ = columns;
    model.lp_.sense_ = ObjSense::kMaximize;
    model.lp_.col_cost_.assign(static_cast<std::size_t>(columns), 0.0);
    model.lp_.col_lower_.assign(static_cast<std::size_t>(columns), 0.0);
    model.lp_.col_upper_.assign(static_cast<std::size_t>(columns), 1.0);
    model.lp_.integrality_.assign(
        static_cast<std::size_t>(columns), HighsVarType::kInteger
    );
    for (int cell = 0; cell < kCells; ++cell) {
        model.lp_.col_cost_[cell] = base[cell];
    }
    for (std::size_t index = 0; index < pairs.size(); ++index) {
        model.lp_.col_cost_[kCells + index] = -pairs[index].loss;
    }

    auto& matrix = model.lp_.a_matrix_;
    matrix.format_ = MatrixFormat::kRowwise;
    matrix.start_.clear();
    matrix.start_.push_back(0);
    std::vector<double> row_lower;
    std::vector<double> row_upper;
    row_lower.reserve(1U + 3U * pairs.size() + 4000U);
    row_upper.reserve(row_lower.capacity());

    for (int cell = 0; cell < kCells; ++cell) {
        matrix.index_.push_back(cell);
        matrix.value_.push_back(1.0);
    }
    matrix.start_.push_back(static_cast<HighsInt>(matrix.index_.size()));
    row_lower.push_back(static_cast<double>(kTurbines));
    row_upper.push_back(static_cast<double>(kTurbines));

    for (int left = 0; left < kCells; ++left) {
        for (int right = left + 1; right < kCells; ++right) {
            if (!problem.spacing_conflict(left, right)) continue;
            append_row(
                matrix, row_lower, row_upper,
                {{left, 1.0}, {right, 1.0}},
                -kInfinity, 1.0
            );
        }
    }
    for (std::size_t index = 0; index < pairs.size(); ++index) {
        const int y = kCells + static_cast<int>(index);
        const Pair& pair = pairs[index];
        append_row(
            matrix, row_lower, row_upper,
            {{y, 1.0}, {pair.left, -1.0}},
            -kInfinity, 0.0
        );
        append_row(
            matrix, row_lower, row_upper,
            {{y, 1.0}, {pair.right, -1.0}},
            -kInfinity, 0.0
        );
        append_row(
            matrix, row_lower, row_upper,
            {{pair.left, 1.0}, {pair.right, 1.0}, {y, -1.0}},
            -kInfinity, 1.0
        );
    }
    model.lp_.num_row_ = static_cast<HighsInt>(row_lower.size());
    model.lp_.row_lower_ = std::move(row_lower);
    model.lp_.row_upper_ = std::move(row_upper);

    Highs highs;
    highs.setOptionValue("output_flag", false);
    highs.setOptionValue("threads", workers);
    highs.setOptionValue("parallel", kHighsOnString);
    highs.setOptionValue("mip_rel_gap", 0.0);
    highs.setOptionValue("mip_abs_gap", 0.0);
    highs.setOptionValue("time_limit", time_limit_seconds);
    highs.setOptionValue("random_seed", 63);
    const auto start = Clock::now();
    const HighsStatus pass_status = highs.passModel(model);
    if (pass_status == HighsStatus::kError) {
        throw std::runtime_error("HiGHS rejected T63 MILP");
    }
    std::vector<int> seed_layout;
    seed_layout.reserve(kTurbines);
    std::vector<int> order(kCells);
    for (int cell = 0; cell < kCells; ++cell) order[cell] = cell;
    std::stable_sort(order.begin(), order.end(), [&](const int left, const int right) {
        return base[left] > base[right];
    });
    for (const int cell : order) {
        bool feasible = true;
        for (const int selected : seed_layout) {
            if (problem.spacing_conflict(cell, selected)) {
                feasible = false;
                break;
            }
        }
        if (feasible) seed_layout.push_back(cell);
        if (seed_layout.size() == kTurbines) break;
    }
    if (seed_layout.size() != kTurbines) {
        seed_layout.clear();
        for (int row = 0; row < kGrid && seed_layout.size() < kTurbines; row += 3) {
            for (
                int column = 0;
                column < kGrid && seed_layout.size() < kTurbines;
                column += 3
            ) {
                seed_layout.push_back(row * kGrid + column);
            }
        }
    }
    HighsSolution initial_solution;
    initial_solution.col_value.assign(
        static_cast<std::size_t>(columns), 0.0
    );
    std::vector<bool> seeded(kCells, false);
    for (const int cell : seed_layout) {
        initial_solution.col_value[cell] = 1.0;
        seeded[cell] = true;
    }
    for (std::size_t index = 0; index < pairs.size(); ++index) {
        initial_solution.col_value[kCells + index] =
            seeded[pairs[index].left] && seeded[pairs[index].right]
            ? 1.0
            : 0.0;
    }
    if (highs.setSolution(initial_solution) == HighsStatus::kError) {
        throw std::runtime_error("HiGHS rejected T63 feasible start");
    }
    const HighsStatus run_status = highs.run();
    const double seconds =
        std::chrono::duration<double>(Clock::now() - start).count();
    if (run_status == HighsStatus::kError) {
        throw std::runtime_error("HiGHS failed T63 MILP");
    }
    const HighsSolution& solution = highs.getSolution();
    if (!solution.value_valid || solution.col_value.size() < kCells) {
        throw std::runtime_error(
            "HiGHS returned no T63 incumbent; status="
            + highs.modelStatusToString(highs.getModelStatus())
        );
    }
    MipReceipt receipt;
    receipt.seconds = seconds;
    receipt.status = highs.modelStatusToString(highs.getModelStatus());
    const HighsInfo& info = highs.getInfo();
    receipt.objective = info.objective_function_value;
    receipt.dual_bound = info.mip_dual_bound;
    receipt.gap = info.mip_gap;
    for (int cell = 0; cell < kCells; ++cell) {
        if (solution.col_value[cell] >= 0.5) receipt.selected.push_back(cell);
    }
    highs.resetGlobalScheduler(true);
    if (receipt.selected.size() != kTurbines) {
        throw std::runtime_error("HiGHS T63 incumbent has wrong cardinality");
    }
    std::sort(receipt.selected.begin(), receipt.selected.end());
    return receipt;
}

}  // namespace

struct Problem::Data {
    std::vector<double> elevation;
    std::array<double, kSectors> probability{};
    std::array<double, kCells * kSectors> speed{};

    [[nodiscard]] static int row(const int cell) { return cell / kGrid; }
    [[nodiscard]] static int column(const int cell) { return cell % kGrid; }
    [[nodiscard]] static double x(const int cell) {
        return (static_cast<double>(column(cell)) + 0.5) * kCellPitchM;
    }
    [[nodiscard]] static double y(const int cell) {
        return (static_cast<double>(row(cell)) + 0.5) * kCellPitchM;
    }
    [[nodiscard]] double gradient_x(const int cell) const {
        const int r = row(cell);
        const int c = column(cell);
        const int left = r * kGrid + std::max(0, c - 1);
        const int right = r * kGrid + std::min(kGrid - 1, c + 1);
        const double width = static_cast<double>(
            std::min(kGrid - 1, c + 1) - std::max(0, c - 1)
        ) * kCellPitchM;
        return (elevation[right] - elevation[left]) / width;
    }
    [[nodiscard]] double gradient_y(const int cell) const {
        const int r = row(cell);
        const int c = column(cell);
        const int bottom = std::max(0, r - 1) * kGrid + c;
        const int top = std::min(kGrid - 1, r + 1) * kGrid + c;
        const double width = static_cast<double>(
            std::min(kGrid - 1, r + 1) - std::max(0, r - 1)
        ) * kCellPitchM;
        return (elevation[top] - elevation[bottom]) / width;
    }
};

Problem::Problem(const std::string& proxy_path)
    : semantic_id_("t63_carleton_figure_proxy_cfd_surrogate_v1") {
    std::ifstream stream(proxy_path, std::ios::binary);
    if (!stream) throw std::runtime_error("cannot open T63 proxy: " + proxy_path);
    std::array<char, 8> magic{};
    stream.read(magic.data(), static_cast<std::streamsize>(magic.size()));
    const std::array<char, 8> expected{'T','6','3','P','X','Y','1','\0'};
    if (!stream || magic != expected) {
        throw std::runtime_error("invalid T63 proxy magic");
    }
    const std::uint32_t grid = read_scalar<std::uint32_t>(stream);
    const std::uint32_t sectors = read_scalar<std::uint32_t>(stream);
    if (grid != kGrid || sectors != kSectors) {
        throw std::runtime_error("T63 proxy dimensions mismatch");
    }
    auto data = std::make_shared<Data>();
    const auto elevations = read_array(stream, kCells);
    const auto probabilities = read_array(stream, kSectors);
    data->elevation.assign(elevations.begin(), elevations.end());
    double probability_sum = 0.0;
    for (int sector = 0; sector < kSectors; ++sector) {
        data->probability[sector] = probabilities[sector];
        probability_sum += probabilities[sector];
    }
    if (!(probability_sum > 0.999 && probability_sum < 1.001)) {
        throw std::runtime_error("T63 wind probabilities do not sum to one");
    }
    for (int cell = 0; cell < kCells; ++cell) {
        const double height_above_sea =
            data->elevation[cell] + kHubHeightM;
        const double power_law_argument = std::max(
            1.0e-6, (height_above_sea - 139.0) / 50.0
        );
        const double speed = 6.0 * std::pow(power_law_argument, 0.16);
        for (int sector = 0; sector < kSectors; ++sector) {
            data->speed[cell * kSectors + sector] = speed;
        }
    }
    char excess = '\0';
    if (stream.read(&excess, 1)) {
        throw std::runtime_error("unexpected trailing bytes in T63 proxy");
    }
    data_ = std::move(data);
}

const std::string& Problem::semantic_id() const noexcept {
    return semantic_id_;
}

int Problem::grid_size() const noexcept { return kGrid; }
int Problem::turbine_count() const noexcept { return kTurbines; }

double Problem::elevation_m(const int cell) const {
    if (cell < 0 || cell >= kCells) {
        throw std::out_of_range("T63 cell");
    }
    return data_->elevation[cell];
}

double Problem::wind_probability(const int sector) const {
    if (sector < 0 || sector >= kSectors) {
        throw std::out_of_range("T63 sector");
    }
    return data_->probability[sector];
}

double Problem::background_speed_mps(
    const int cell,
    const int sector
) const {
    if (cell < 0 || cell >= kCells || sector < 0 || sector >= kSectors) {
        throw std::out_of_range("T63 speed index");
    }
    return data_->speed[cell * kSectors + sector];
}

bool Problem::spacing_conflict(
    const int left_cell,
    const int right_cell
) const {
    if (left_cell == right_cell) return true;
    return std::hypot(
        Data::x(left_cell) - Data::x(right_cell),
        Data::y(left_cell) - Data::y(right_cell)
    ) < kMinimumSpacingM - 1.0e-12;
}

RunResult run(const Problem& problem, const RunConfig& config) {
    if (config.workers < 1 || config.maximum_iterations < 1
        || !(config.relaxation >= 0.0 && config.relaxation <= 1.0)
        || !(config.mip_time_limit_seconds > 0.0)) {
        throw std::invalid_argument("invalid T63 run configuration");
    }
    const auto run_start = Clock::now();
    fode::PersistentExecutor executor(config.workers);
    executor.reset_work_receipt();
    std::vector<double> base(kCells, 0.0);
    std::vector<double> approximate(
        static_cast<std::size_t>(kCells) * kCells, 0.0
    );
    std::vector<double> terrain_aware(
        static_cast<std::size_t>(kCells) * kCells, 0.0
    );
    for (int cell = 0; cell < kCells; ++cell) {
        for (int sector = 0; sector < kSectors; ++sector) {
            const double speed = problem.data_->speed[cell * kSectors + sector];
            base[cell] += problem.data_->probability[sector] * speed * speed;
        }
    }
    const auto field_start = Clock::now();
    executor.parallel_for(0, kCells, [&](const int source) {
        const double axial_induction =
            0.5 * (1.0 - std::sqrt(1.0 - kThrustCoefficient));
        for (int target = 0; target < kCells; ++target) {
            if (source == target) continue;
            double approximate_loss = 0.0;
            double true_loss = 0.0;
            for (int sector = 0; sector < kSectors; ++sector) {
                const double from = sector * 30.0 * kPi / 180.0;
                const double base_dx = -std::sin(from);
                const double base_dy = -std::cos(from);
                const double displacement_x =
                    Problem::Data::x(target) - Problem::Data::x(source);
                const double displacement_y =
                    Problem::Data::y(target) - Problem::Data::y(source);
                const double target_speed =
                    problem.data_->speed[target * kSectors + sector];
                const auto loss_for = [&](
                    const bool corrected
                ) {
                    double dx = base_dx;
                    double dy = base_dy;
                    double decay = 0.075;
                    double vertical_scale = 0.0;
                    if (corrected) {
                        const double gx = problem.data_->gradient_x(source);
                        const double gy = problem.data_->gradient_y(source);
                        const double cross_slope = gx * (-dy) + gy * dx;
                        const double turn = std::clamp(
                            std::atan(1.5 * cross_slope),
                            -20.0 * kPi / 180.0,
                            20.0 * kPi / 180.0
                        );
                        const double rotated_x =
                            dx * std::cos(turn) - dy * std::sin(turn);
                        const double rotated_y =
                            dx * std::sin(turn) + dy * std::cos(turn);
                        dx = rotated_x;
                        dy = rotated_y;
                        const double along_slope = gx * dx + gy * dy;
                        decay += 0.08 * std::abs(along_slope);
                        vertical_scale = 0.35;
                    }
                    const double downstream =
                        displacement_x * dx + displacement_y * dy;
                    if (!(downstream > 0.0)) return 0.0;
                    const double crosswind =
                        std::abs(-displacement_x * dy + displacement_y * dx);
                    const double elevation_difference =
                        problem.data_->elevation[target]
                        - problem.data_->elevation[source];
                    const double radial = std::hypot(
                        crosswind, vertical_scale * elevation_difference
                    );
                    const double wake_radius =
                        0.5 * kRotorDiameterM + decay * downstream;
                    if (radial > wake_radius) return 0.0;
                    const double deficit = 2.0 * axial_induction / std::pow(
                        1.0 + 2.0 * decay * downstream / kRotorDiameterM,
                        2.0
                    );
                    const double wake_speed =
                        target_speed * std::max(0.0, 1.0 - deficit);
                    return target_speed * target_speed
                        - wake_speed * wake_speed;
                };
                const double probability = problem.data_->probability[sector];
                approximate_loss += probability * loss_for(false);
                true_loss += probability * loss_for(true);
            }
            approximate[
                static_cast<std::size_t>(source) * kCells + target
            ] = approximate_loss;
            terrain_aware[
                static_cast<std::size_t>(source) * kCells + target
            ] = true_loss;
        }
    });
    const double field_seconds = std::chrono::duration<double>(
        Clock::now() - field_start
    ).count();
    const auto executor_receipt = executor.work_receipt();

    const std::vector<double> zero_loss(
        static_cast<std::size_t>(kCells) * kCells, 0.0
    );
    MipReceipt upper = solve_mip(
        problem, base, zero_loss, config.workers,
        config.mip_time_limit_seconds
    );
    const double no_wake_upper =
        objective_for_layout(upper.selected, base, zero_loss);
    double total_mip_seconds = upper.seconds;

    std::vector<bool> cfd_known(kCells, false);
    std::vector<int> final_layout;
    std::vector<IterationReceipt> history;
    for (int iteration = 1; iteration <= config.maximum_iterations; ++iteration) {
        std::vector<double> active_loss(approximate.size(), 0.0);
        executor.parallel_for(0, kCells, [&](const int source) {
            const auto source_offset =
                static_cast<std::size_t>(source) * kCells;
            for (int target = 0; target < kCells; ++target) {
                active_loss[source_offset + target] = cfd_known[source]
                    ? terrain_aware[source_offset + target]
                    : config.relaxation * approximate[source_offset + target];
            }
        });
        MipReceipt mip = solve_mip(
            problem, base, active_loss, config.workers,
            config.mip_time_limit_seconds
        );
        total_mip_seconds += mip.seconds;
        int newly_known = 0;
        for (const int cell : mip.selected) {
            if (!cfd_known[cell]) {
                cfd_known[cell] = true;
                ++newly_known;
            }
        }
        const int cumulative = static_cast<int>(std::count(
            cfd_known.begin(), cfd_known.end(), true
        ));
        history.push_back({
            iteration,
            newly_known,
            cumulative,
            newly_known * kSectors,
            objective_for_layout(mip.selected, base, active_loss),
            mip.dual_bound,
            mip.gap,
            mip.seconds,
            mip.status,
            mip.selected,
        });
        final_layout = std::move(mip.selected);
        if (newly_known == 0) break;
    }
    if (final_layout.empty()) {
        throw std::runtime_error("T63 produced no layout");
    }

    RunResult result;
    result.problem_semantic_id = problem.semantic_id();
    result.method_semantic_id =
        "t63_iterative_cfd_mip_highs_reconstruction_v1";
    result.relaxation = config.relaxation;
    result.requested_workers = config.workers;
    result.observed_workers = executor_receipt.distinct_participants;
    result.iterations = static_cast<int>(history.size());
    result.cfd_locations = static_cast<int>(std::count(
        cfd_known.begin(), cfd_known.end(), true
    ));
    result.cfd_simulations = result.cfd_locations * kSectors;
    result.final_true_objective = objective_for_layout(
        final_layout, base, terrain_aware
    );
    result.no_wake_upper_bound = no_wake_upper;
    result.layout_efficiency =
        result.final_true_objective / result.no_wake_upper_bound;
    result.field_generation_seconds = field_seconds;
    result.mip_seconds = total_mip_seconds;
    result.end_to_end_seconds = std::chrono::duration<double>(
        Clock::now() - run_start
    ).count();
    result.final_layout = std::move(final_layout);
    result.history = std::move(history);
    result.scientific_hash = hash_result(
        result.final_layout, result.final_true_objective, result.relaxation
    );
    return result;
}

}  // namespace core99::t63
