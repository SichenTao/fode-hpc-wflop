/*
WFLOP IMPLEMENTATION FACT DECLARATION
Implementation unit: Y13 pure-C++/HiGHS CPU-HPC grid-WFLO consensus ADMM
Paper/DOI: Du et al.; 10.1109/TSTE.2025.3609006.
Public source: the target paper exposes no linked implementation; the cited
Frandsen-Gaussian source is recorded in include/core99/du_y13.hpp.
Missing and Reconstruction: target data, Gurobi state and algorithm details,
plus every declared completion decision, are recorded in that header.
Semantic IDs: y13_four_grid_fg36_declared_v1,
y13_l2box_consensus_admm_highs_declared_v1 and
y13_native_four_case_single_run_v1.
Claim boundary: equation-level flexible academic reproduction; full boundary
and corrections are recorded in include/core99/du_y13.hpp.
Pinned open Gurobi replacement: HiGHS revision
04024d701f79feb8e2f18bc3df0dffc04ef05088.
HPC realization: immutable Frandsen-Gaussian pair matrices are constructed by
scenario in a persistent full-core executor. At each ADMM iteration, all 36
scenario subproblems are solved independently in fixed slots; each HiGHS
instance is restricted to one internal thread so the outer full-core team owns
the machine. Consensus
and residual reductions are deterministic and ordered by grid then scenario.
Controlling contract: shared/contracts/core99_y13_du_grid_admm_2026.json.
Last evidence-audit date: 2026-08-01
END WFLOP IMPLEMENTATION FACT DECLARATION
*/
#include "core99/du_y13.hpp"

#include "Highs.h"

#include <algorithm>
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

namespace core99::y13 {
namespace {

using Clock = std::chrono::steady_clock;
constexpr int kScenarios = 36;
constexpr double kRatedPowerMw = 5.0;
constexpr double kRotorDiameterM = 126.0;
constexpr double kCellPitchM = 5.0 * kRotorDiameterM;
constexpr double kHubHeightM = 90.0;
constexpr double kRoughnessM = 0.0002;
constexpr double kAirDensity = 1.225;
constexpr double kPowerCoefficient = 0.45;
constexpr double kThrustCoefficient = 0.80;
constexpr double kHoursPerYear = 8760.0;
constexpr double kInitialRho = 0.5;

double elapsed(const Clock::time_point start) {
    return std::chrono::duration<double>(Clock::now() - start).count();
}

struct WindScenario {
    double direction_degrees = 0.0;
    double speed_mps = 0.0;
    double probability = 0.0;
};

std::vector<WindScenario> reconstructed_resource() {
    std::vector<WindScenario> result(static_cast<std::size_t>(kScenarios));
    double total = 0.0;
    for (int scenario = 0; scenario < kScenarios; ++scenario) {
        const double direction = 10.0 * scenario;
        const double radians =
            (direction - 235.0) * std::numbers::pi / 180.0;
        const double secondary =
            (2.0 * direction - 40.0) * std::numbers::pi / 180.0;
        const double weight = std::max(
            0.20, 1.0 + 0.55 * std::cos(radians)
                      + 0.18 * std::cos(secondary)
        );
        result[static_cast<std::size_t>(scenario)] = {
            direction,
            12.3 + 0.75 * std::cos(radians) + 0.25 * std::cos(secondary),
            weight,
        };
        total += weight;
    }
    for (auto& scenario : result) scenario.probability /= total;
    return result;
}

double nrel5mw_style_power(const double speed) {
    if (speed < 3.0 || speed >= 25.0) return 0.0;
    const double area = std::numbers::pi * kRotorDiameterM * kRotorDiameterM
        / 4.0;
    const double aerodynamic = 0.5 * kAirDensity * area * kPowerCoefficient
        * speed * speed * speed / 1.0e6;
    return std::min(kRatedPowerMw, aerodynamic);
}

double pair_loss(
    const int source_row,
    const int source_column,
    const int target_row,
    const int target_column,
    const WindScenario& scenario
) {
    if (source_row == target_row && source_column == target_column) return 0.0;
    const double dx = (target_column - source_column) * kCellPitchM;
    const double dy = (target_row - source_row) * kCellPitchM;
    const double angle = scenario.direction_degrees
        * std::numbers::pi / 180.0;
    // Meteorological wind-from direction converted to a Cartesian flow-to
    // vector. Grid x points east and grid y points north.
    const double flow_x = -std::sin(angle);
    const double flow_y = -std::cos(angle);
    const double downstream = dx * flow_x + dy * flow_y;
    if (!(downstream > 0.0)) return 0.0;
    const double crosswind = std::abs(-dx * flow_y + dy * flow_x);
    const double rotor_radius = 0.5 * kRotorDiameterM;
    const double r0 = 0.8 * rotor_radius;
    const double alpha = 0.56 / std::log(kHubHeightM / kRoughnessM);
    const double sigma2 = r0 * r0
        + alpha * alpha * downstream * downstream;
    const double radicand = std::clamp(
        1.0 - kThrustCoefficient * rotor_radius * rotor_radius
                  / (2.0 * sigma2),
        0.0, 1.0
    );
    const double amplitude = 1.0 - std::sqrt(radicand);
    const double deficit = amplitude
        * std::exp(-crosswind * crosswind / (2.0 * sigma2));
    const double affected_speed = scenario.speed_mps
        * std::clamp(1.0 - deficit, 0.0, 1.0);
    return std::clamp(
        kRatedPowerMw - nrel5mw_style_power(affected_speed),
        0.0, kRatedPowerMw
    );
}

std::vector<int> boundary_warm_start(const int side) {
    const int n = side * side;
    const int count = n / 2;
    const double center = 0.5 * (side - 1);
    std::vector<int> order(static_cast<std::size_t>(n));
    std::iota(order.begin(), order.end(), 0);
    std::stable_sort(order.begin(), order.end(), [&](int left, int right) {
        const int left_row = left / side;
        const int left_column = left % side;
        const int right_row = right / side;
        const int right_column = right % side;
        const double left_score = std::max(
            std::abs(left_row - center), std::abs(left_column - center));
        const double right_score = std::max(
            std::abs(right_row - center), std::abs(right_column - center));
        if (left_score != right_score) return left_score > right_score;
        return left < right;
    });
    order.resize(static_cast<std::size_t>(count));
    std::sort(order.begin(), order.end());
    return order;
}

std::vector<double> binary_vector(
    const int size,
    const std::vector<int>& selected
) {
    std::vector<double> result(static_cast<std::size_t>(size), 0.0);
    for (const int index : selected) {
        if (index < 0 || index >= size) {
            throw std::invalid_argument("Y13 selected cell outside grid");
        }
        result[static_cast<std::size_t>(index)] = 1.0;
    }
    return result;
}

std::vector<int> exact_cardinality_round(
    const std::vector<double>& values,
    const int count
) {
    std::vector<int> order(values.size());
    std::iota(order.begin(), order.end(), 0);
    std::stable_sort(order.begin(), order.end(), [&](int left, int right) {
        if (values[static_cast<std::size_t>(left)]
            != values[static_cast<std::size_t>(right)]) {
            return values[static_cast<std::size_t>(left)]
                > values[static_cast<std::size_t>(right)];
        }
        return left < right;
    });
    order.resize(static_cast<std::size_t>(count));
    std::sort(order.begin(), order.end());
    return order;
}

double rounding_deviation(
    const std::vector<double>& values,
    const std::vector<int>& selected
) {
    std::vector<unsigned char> active(values.size(), 0);
    for (const int index : selected) active[static_cast<std::size_t>(index)] = 1;
    double total = 0.0;
    for (std::size_t index = 0; index < values.size(); ++index) {
        total += std::abs(values[index] - static_cast<double>(active[index]));
    }
    return total / static_cast<double>(values.size());
}

std::vector<double> project_capped_simplex(
    const std::vector<double>& values,
    const double target_sum
) {
    double low = *std::min_element(values.begin(), values.end()) - 1.0;
    double high = *std::max_element(values.begin(), values.end());
    for (int iteration = 0; iteration < 80; ++iteration) {
        const double shift = 0.5 * (low + high);
        double sum = 0.0;
        for (const double value : values) {
            sum += std::clamp(value - shift, 0.0, 1.0);
        }
        if (sum > target_sum) low = shift;
        else high = shift;
    }
    const double shift = 0.5 * (low + high);
    std::vector<double> result(values.size());
    for (std::size_t index = 0; index < values.size(); ++index) {
        result[index] = std::clamp(values[index] - shift, 0.0, 1.0);
    }
    return result;
}

std::uint64_t hash_result(const RunResult& result) {
    std::uint64_t hash = 1469598103934665603ULL;
    auto mix = [&](const std::uint64_t value) {
        hash ^= value;
        hash *= 1099511628211ULL;
    };
    mix(static_cast<std::uint64_t>(result.grid_side));
    mix(static_cast<std::uint64_t>(result.admm_iterations));
    mix(std::bit_cast<std::uint64_t>(result.final_rho));
    mix(std::bit_cast<std::uint64_t>(result.final_evaluation.net_aep_gwh));
    for (const int index : result.selected_cells) {
        mix(static_cast<std::uint64_t>(index));
    }
    return hash;
}

}  // namespace

struct Problem::Data {
    CaseId case_id = CaseId::grid_6;
    int side = 6;
    int cells = 36;
    int turbines = 18;
    std::vector<WindScenario> wind;
    // loss[(scenario*cells + target)*cells + source]
    std::vector<double> loss;
    std::vector<int> warm;
    double matrix_build_seconds = 0.0;
    int matrix_workers = 0;
};

namespace {

std::size_t loss_offset(
    const Problem::Data& data,
    const int scenario,
    const int target,
    const int source
) {
    return (static_cast<std::size_t>(scenario) * data.cells + target)
        * data.cells + source;
}

bool solve_scenario_subproblem(
    const Problem::Data& data,
    const int scenario,
    const double rho,
    const std::vector<double>& z,
    const std::vector<double>& y1,
    const std::vector<double>& y2,
    const std::vector<double>& w1,
    const std::vector<double>& w2,
    const std::vector<double>& w3,
    std::vector<double>& x,
    std::string& error
) {
    const int n = data.cells;
    double scenario_loss_sum = 0.0;
    for (int target = 0; target < n; ++target) {
        for (int source = 0; source < n; ++source) {
            scenario_loss_sum += data.loss[loss_offset(
                data, scenario, target, source)];
        }
    }
    if (scenario_loss_sum <= 1.0e-13) {
        std::vector<double> centre(static_cast<std::size_t>(n));
        for (int index = 0; index < n; ++index) {
            centre[static_cast<std::size_t>(index)] =
                (z[static_cast<std::size_t>(index)]
                 + y1[static_cast<std::size_t>(index)]
                 + y2[static_cast<std::size_t>(index)]) / 3.0
                - (w1[static_cast<std::size_t>(index)]
                   + w2[static_cast<std::size_t>(index)]
                   + w3[static_cast<std::size_t>(index)]) / (3.0 * rho);
        }
        x = project_capped_simplex(centre, data.turbines);
        return true;
    }
    constexpr int quadratic_segments = 32;
    HighsModel model;
    // x, P, and the epigraph t for x^2. Thirty-three uniformly spaced
    // tangents provide a deterministic open-solver realization of the
    // separable convex quadratic. On [0,1], the pointwise x^2 underestimation
    // is bounded by 1/(4*quadratic_segments^2).
    model.lp_.num_col_ = 3 * n;
    model.lp_.sense_ = ObjSense::kMinimize;
    model.lp_.col_cost_.assign(static_cast<std::size_t>(3 * n), 0.0);
    model.lp_.col_lower_.assign(static_cast<std::size_t>(3 * n), -kHighsInf);
    model.lp_.col_upper_.assign(static_cast<std::size_t>(3 * n), kHighsInf);
    for (int index = 0; index < n; ++index) {
        // Redundant with the separate box copy at convergence, but retaining
        // these finite bounds gives the open QP replacement the same bounded
        // continuous relaxation used by practical Gurobi implementations.
        model.lp_.col_lower_[static_cast<std::size_t>(index)] = 0.0;
        model.lp_.col_upper_[static_cast<std::size_t>(index)] = 1.0;
        model.lp_.col_cost_[static_cast<std::size_t>(index)] =
            w1[static_cast<std::size_t>(index)]
            + w2[static_cast<std::size_t>(index)]
            + w3[static_cast<std::size_t>(index)]
            - rho * (z[static_cast<std::size_t>(index)]
                     + y1[static_cast<std::size_t>(index)]
                     + y2[static_cast<std::size_t>(index)]);
        model.lp_.col_cost_[static_cast<std::size_t>(n + index)] =
            -kHoursPerYear
            * data.wind[static_cast<std::size_t>(scenario)].probability
            / 1000.0;
        model.lp_.col_lower_[static_cast<std::size_t>(n + index)] = 0.0;
        model.lp_.col_upper_[static_cast<std::size_t>(n + index)] =
            kRatedPowerMw;
        model.lp_.col_cost_[static_cast<std::size_t>(2 * n + index)] =
            1.5 * rho;
        model.lp_.col_lower_[static_cast<std::size_t>(2 * n + index)] = 0.0;
        model.lp_.col_upper_[static_cast<std::size_t>(2 * n + index)] = 1.0;
    }

    auto& matrix = model.lp_.a_matrix_;
    matrix.format_ = MatrixFormat::kRowwise;
    matrix.start_.clear();
    matrix.index_.clear();
    matrix.value_.clear();
    matrix.start_.push_back(0);
    std::vector<double> lower;
    std::vector<double> upper;
    lower.reserve(static_cast<std::size_t>(1 + 2 * n));
    upper.reserve(static_cast<std::size_t>(1 + 2 * n));
    auto append_row = [&](const std::vector<std::pair<int, double>>& entries,
                          const double lo, const double hi) {
        for (const auto& [column, value] : entries) {
            matrix.index_.push_back(column);
            matrix.value_.push_back(value);
        }
        matrix.start_.push_back(static_cast<HighsInt>(matrix.index_.size()));
        lower.push_back(lo);
        upper.push_back(hi);
    };

    std::vector<std::pair<int, double>> cardinality;
    cardinality.reserve(static_cast<std::size_t>(n));
    for (int index = 0; index < n; ++index) {
        cardinality.emplace_back(index, 1.0);
    }
    append_row(cardinality, data.turbines, data.turbines);

    for (int target = 0; target < n; ++target) {
        append_row({{target, -kRatedPowerMw}, {n + target, 1.0}},
                   -kHighsInf, 0.0);
    }
    for (int target = 0; target < n; ++target) {
        std::vector<std::pair<int, double>> entries;
        entries.reserve(static_cast<std::size_t>(n + 2));
        double tight_big_m = 0.0;
        for (int source = 0; source < n; ++source) {
            const double value = data.loss[loss_offset(
                data, scenario, target, source)];
            if (value > 1.0e-13) {
                entries.emplace_back(source, value);
                tight_big_m += value;
            }
        }
        // The printed Eq. (1d) remains active when x_target=0 and can make an
        // otherwise valid no-turbine cell infeasible. Apply the standard tight
        // binary implication completion: the row is exact at x_target=1 and
        // relaxed by the maximum possible incoming loss at x_target=0.
        if (tight_big_m > 1.0e-13) {
            entries.emplace_back(target, tight_big_m);
            entries.emplace_back(n + target, 1.0);
            std::sort(entries.begin(), entries.end());
            append_row(entries, -kHighsInf, kRatedPowerMw + tight_big_m);
        }
    }
    for (int index = 0; index < n; ++index) {
        for (int segment = 1; segment <= quadratic_segments; ++segment) {
            const double point = static_cast<double>(segment)
                / quadratic_segments;
            append_row({
                {index, -2.0 * point},
                {2 * n + index, 1.0},
            }, -point * point, kHighsInf);
        }
    }
    model.lp_.num_row_ = static_cast<HighsInt>(lower.size());
    model.lp_.row_lower_ = std::move(lower);
    model.lp_.row_upper_ = std::move(upper);

    // A half-filled vector is analytically feasible for the continuous
    // relaxation after the tight implication completion. Check the generated
    // sparse model before invoking the external solver so an indexing defect
    // cannot be misreported as a scientific infeasibility.
    std::vector<double> feasibility_probe(static_cast<std::size_t>(3 * n), 0.0);
    std::fill_n(feasibility_probe.begin(), n, 0.5);
    std::fill_n(feasibility_probe.begin() + 2 * n, n, 0.25);
    for (int row = 0; row < model.lp_.num_row_; ++row) {
        double value = 0.0;
        for (HighsInt entry = matrix.start_[static_cast<std::size_t>(row)];
             entry < matrix.start_[static_cast<std::size_t>(row + 1)]; ++entry) {
            value += matrix.value_[static_cast<std::size_t>(entry)]
                * feasibility_probe[static_cast<std::size_t>(
                    matrix.index_[static_cast<std::size_t>(entry)])];
        }
        if (value < model.lp_.row_lower_[static_cast<std::size_t>(row)] - 1.0e-9
            || value > model.lp_.row_upper_[static_cast<std::size_t>(row)]
                + 1.0e-9) {
            error = "generated Y13 subproblem has invalid feasibility probe at row "
                + std::to_string(row) + " value=" + std::to_string(value)
                + " lower=" + std::to_string(
                    model.lp_.row_lower_[static_cast<std::size_t>(row)])
                + " upper=" + std::to_string(
                    model.lp_.row_upper_[static_cast<std::size_t>(row)])
                + " entries=" + std::to_string(
                    matrix.start_[static_cast<std::size_t>(row + 1)]
                    - matrix.start_[static_cast<std::size_t>(row)])
                + " first_column=" + std::to_string(
                    matrix.index_[static_cast<std::size_t>(
                        matrix.start_[static_cast<std::size_t>(row)])])
                + " first_value=" + std::to_string(
                    matrix.value_[static_cast<std::size_t>(
                        matrix.start_[static_cast<std::size_t>(row)])])
                + " first_probe=" + std::to_string(
                    feasibility_probe[static_cast<std::size_t>(
                        matrix.index_[static_cast<std::size_t>(
                            matrix.start_[static_cast<std::size_t>(row)])])]);
            return false;
        }
    }

    Highs highs;
    highs.setOptionValue("output_flag", false);
    highs.setOptionValue("threads", 1);
    highs.setOptionValue("parallel", kHighsOffString);
    highs.setOptionValue("presolve", kHighsOnString);
    if (highs.passModel(model) == HighsStatus::kError) {
        error = "HiGHS rejected Y13 epigraph-LP scenario subproblem";
        return false;
    }
    if (highs.run() == HighsStatus::kError) {
        error = "HiGHS failed Y13 piecewise-linear convex subproblem";
        return false;
    }
    if (highs.getModelStatus() != HighsModelStatus::kOptimal) {
        error = "HiGHS Y13 subproblem status "
            + highs.modelStatusToString(highs.getModelStatus());
        return false;
    }
    const auto& solution = highs.getSolution();
    if (!solution.value_valid) {
        error = "HiGHS returned no Y13 primal solution: "
            + highs.modelStatusToString(highs.getModelStatus());
        return false;
    }
    for (int index = 0; index < n; ++index) {
        x[static_cast<std::size_t>(index)] =
            solution.col_value[static_cast<std::size_t>(index)];
        if (!std::isfinite(x[static_cast<std::size_t>(index)])) {
            error = "HiGHS returned a non-finite Y13 grid decision";
            return false;
        }
    }
    return true;
}

}  // namespace

Problem::Problem(const CaseId case_id, const int workers)
    : data_(std::make_unique<Data>()) {
    if (workers < 1) throw std::invalid_argument("Y13 workers must be positive");
    data_->case_id = case_id;
    data_->side = static_cast<int>(case_id);
    if (data_->side != 6 && data_->side != 10
        && data_->side != 16 && data_->side != 20) {
        throw std::invalid_argument("Y13 grid side must be 6, 10, 16 or 20");
    }
    data_->cells = data_->side * data_->side;
    data_->turbines = data_->cells / 2;
    data_->wind = reconstructed_resource();
    data_->warm = boundary_warm_start(data_->side);
    data_->loss.resize(
        static_cast<std::size_t>(kScenarios) * data_->cells * data_->cells,
        0.0
    );
    const auto start = Clock::now();
    fode::PersistentExecutor executor(workers);
    executor.parallel_for(0, kScenarios, [&](const int scenario) {
        for (int target = 0; target < data_->cells; ++target) {
            const int target_row = target / data_->side;
            const int target_column = target % data_->side;
            for (int source = 0; source < data_->cells; ++source) {
                const int source_row = source / data_->side;
                const int source_column = source % data_->side;
                data_->loss[loss_offset(*data_, scenario, target, source)] =
                    pair_loss(source_row, source_column, target_row,
                              target_column,
                              data_->wind[static_cast<std::size_t>(scenario)]);
            }
        }
    });
    data_->matrix_build_seconds = elapsed(start);
    data_->matrix_workers = executor.work_receipt().distinct_participants;
}

Problem::~Problem() = default;
Problem::Problem(Problem&&) noexcept = default;
Problem& Problem::operator=(Problem&&) noexcept = default;
CaseId Problem::case_id() const noexcept { return data_->case_id; }
int Problem::grid_side() const noexcept { return data_->side; }
int Problem::cell_count() const noexcept { return data_->cells; }
int Problem::turbine_count() const noexcept { return data_->turbines; }
int Problem::wind_scenario_count() const noexcept { return kScenarios; }
double Problem::cell_pitch_m() const noexcept { return kCellPitchM; }
double Problem::matrix_seconds() const noexcept {
    return data_->matrix_build_seconds;
}
int Problem::matrix_observed_workers() const noexcept {
    return data_->matrix_workers;
}
const std::vector<int>& Problem::warm_start() const noexcept {
    return data_->warm;
}

Evaluation Problem::evaluate(
    const std::vector<int>& selected,
    fode::PersistentExecutor& executor
) const {
    if (static_cast<int>(selected.size()) != data_->turbines) {
        throw std::invalid_argument("Y13 layout must preserve paper cardinality");
    }
    const auto start = Clock::now();
    std::vector<double> scenario_power(static_cast<std::size_t>(kScenarios), 0.0);
    executor.reset_work_receipt();
    executor.parallel_for(0, kScenarios, [&](const int scenario) {
        double value = 0.0;
        for (const int target : selected) {
            double cap = kRatedPowerMw;
            for (const int source : selected) {
                cap -= data_->loss[loss_offset(
                    *data_, scenario, target, source)];
            }
            value += std::clamp(cap, 0.0, kRatedPowerMw);
        }
        scenario_power[static_cast<std::size_t>(scenario)] = value;
    });
    double expected_power_mw = 0.0;
    for (int scenario = 0; scenario < kScenarios; ++scenario) {
        expected_power_mw += data_->wind[static_cast<std::size_t>(scenario)].probability
            * scenario_power[static_cast<std::size_t>(scenario)];
    }
    Evaluation result;
    result.gross_aep_gwh = selected.size() * kRatedPowerMw
        * kHoursPerYear / 1000.0;
    result.net_aep_gwh = expected_power_mw * kHoursPerYear / 1000.0;
    result.efficiency_percent = 100.0 * result.net_aep_gwh
        / result.gross_aep_gwh;
    result.turbines = static_cast<int>(selected.size());
    result.requested_workers = executor.thread_count();
    result.observed_workers = executor.work_receipt().distinct_participants;
    result.scenario_pair_lookups = static_cast<std::uint64_t>(kScenarios)
        * selected.size() * selected.size();
    result.seconds = elapsed(start);
    return result;
}

const char* case_name(const CaseId value) noexcept {
    switch (value) {
        case CaseId::grid_6: return "6x6";
        case CaseId::grid_10: return "10x10";
        case CaseId::grid_16: return "16x16";
        case CaseId::grid_20: return "20x20";
    }
    return "unknown";
}

RunResult run(const Problem& problem, const RunConfig& config) {
    if (config.workers < 1 || config.maximum_admm_iterations < 1
        || !(config.convergence_tolerance > 0.0)) {
        throw std::invalid_argument("Y13 invalid run configuration");
    }
    const auto total_start = Clock::now();
    const int n = problem.data_->cells;
    const int scenarios = kScenarios;
    const int maximum_iterations = config.smoke
        ? std::min(2, config.maximum_admm_iterations)
        : config.maximum_admm_iterations;
    fode::PersistentExecutor executor(config.workers);

    RunResult result;
    result.case_id = problem.data_->case_id;
    result.grid_side = problem.data_->side;
    result.cells = n;
    result.turbines = problem.data_->turbines;
    result.wind_scenarios = scenarios;
    result.requested_workers = config.workers;
    result.matrix_seconds = problem.data_->matrix_build_seconds;
    result.initial_evaluation = problem.evaluate(problem.data_->warm, executor);
    result.complete_layout_evaluations = 1;

    const std::vector<double> warm = binary_vector(n, problem.data_->warm);
    std::vector<double> z = warm;
    std::vector<double> x(static_cast<std::size_t>(scenarios * n));
    std::vector<double> y1(static_cast<std::size_t>(scenarios * n));
    std::vector<double> y2(static_cast<std::size_t>(scenarios * n));
    std::vector<double> w1(static_cast<std::size_t>(scenarios * n), 0.0);
    std::vector<double> w2(static_cast<std::size_t>(scenarios * n), 0.0);
    std::vector<double> w3(static_cast<std::size_t>(scenarios * n), 0.0);
    for (int scenario = 0; scenario < scenarios; ++scenario) {
        std::copy(warm.begin(), warm.end(), x.begin() + scenario * n);
        std::copy(warm.begin(), warm.end(), y1.begin() + scenario * n);
        std::copy(warm.begin(), warm.end(), y2.begin() + scenario * n);
    }

    double rho = kInitialRho;
    int subproblem_observed_workers = 0;
    const auto algorithm_start = Clock::now();
    for (int iteration = 0; iteration < maximum_iterations; ++iteration) {
        const std::vector<double> previous_z = z;
        const std::vector<double> previous_y1 = y1;
        const std::vector<double> previous_y2 = y2;
        std::vector<std::string> errors(static_cast<std::size_t>(scenarios));
        std::vector<unsigned char> passed(static_cast<std::size_t>(scenarios), 0);
        const auto subproblem_start = Clock::now();
        executor.reset_work_receipt();
        executor.parallel_for(0, scenarios, [&](const int scenario) {
            const auto offset = static_cast<std::size_t>(scenario * n);
            std::vector<double> local_x(
                x.begin() + static_cast<std::ptrdiff_t>(offset),
                x.begin() + static_cast<std::ptrdiff_t>(offset + n));
            const std::vector<double> local_y1(
                y1.begin() + static_cast<std::ptrdiff_t>(offset),
                y1.begin() + static_cast<std::ptrdiff_t>(offset + n));
            const std::vector<double> local_y2(
                y2.begin() + static_cast<std::ptrdiff_t>(offset),
                y2.begin() + static_cast<std::ptrdiff_t>(offset + n));
            const std::vector<double> local_w1(
                w1.begin() + static_cast<std::ptrdiff_t>(offset),
                w1.begin() + static_cast<std::ptrdiff_t>(offset + n));
            const std::vector<double> local_w2(
                w2.begin() + static_cast<std::ptrdiff_t>(offset),
                w2.begin() + static_cast<std::ptrdiff_t>(offset + n));
            const std::vector<double> local_w3(
                w3.begin() + static_cast<std::ptrdiff_t>(offset),
                w3.begin() + static_cast<std::ptrdiff_t>(offset + n));
            passed[static_cast<std::size_t>(scenario)] = solve_scenario_subproblem(
                *problem.data_, scenario, rho, z, local_y1, local_y2,
                local_w1, local_w2, local_w3, local_x,
                errors[static_cast<std::size_t>(scenario)]) ? 1 : 0;
            std::copy(local_x.begin(), local_x.end(),
                      x.begin() + static_cast<std::ptrdiff_t>(offset));
        });
        const double subproblem_wall = elapsed(subproblem_start);
        subproblem_observed_workers = std::max(
            subproblem_observed_workers,
            executor.work_receipt().distinct_participants
        );
        for (int scenario = 0; scenario < scenarios; ++scenario) {
            if (!passed[static_cast<std::size_t>(scenario)]) {
                throw std::runtime_error(
                    "Y13 scenario " + std::to_string(scenario)
                    + " subproblem: "
                    + errors[static_cast<std::size_t>(scenario)]);
            }
        }

        executor.parallel_for(0, scenarios, [&](const int scenario) {
            const std::size_t offset = static_cast<std::size_t>(scenario * n);
            double norm2 = 0.0;
            for (int index = 0; index < n; ++index) {
                const std::size_t slot = offset + static_cast<std::size_t>(index);
                const double box_argument = x[slot] + w2[slot] / rho;
                y1[slot] = std::clamp(box_argument, 0.0, 1.0);
                const double sphere_argument = x[slot] + w3[slot] / rho - 0.5;
                norm2 += sphere_argument * sphere_argument;
            }
            // Projection onto a sphere is set-valued at its centre. Preserve
            // the previous valid sphere direction at that exact degeneracy;
            // this deterministic completion is also the closest continuation
            // of the paper's warm-started ADMM path.
            const bool degenerate = norm2 <= 1.0e-12;
            if (degenerate) {
                norm2 = 0.0;
                for (int index = 0; index < n; ++index) {
                    const double direction = warm[static_cast<std::size_t>(index)]
                        - 0.5;
                    norm2 += direction * direction;
                }
            }
            const double scale = 0.5 * std::sqrt(static_cast<double>(n))
                / std::max(std::sqrt(norm2), 1.0e-15);
            for (int index = 0; index < n; ++index) {
                const std::size_t slot = offset + static_cast<std::size_t>(index);
                const double direction = degenerate
                    ? warm[static_cast<std::size_t>(index)] - 0.5
                    : x[slot] + w3[slot] / rho - 0.5;
                y2[slot] = 0.5 + scale * direction;
            }
        });

        for (int index = 0; index < n; ++index) {
            double total = 0.0;
            for (int scenario = 0; scenario < scenarios; ++scenario) {
                const std::size_t slot = static_cast<std::size_t>(scenario * n + index);
                total += x[slot] + w1[slot] / rho;
            }
            z[static_cast<std::size_t>(index)] = total / scenarios;
        }

        executor.parallel_for(0, scenarios, [&](const int scenario) {
            const std::size_t offset = static_cast<std::size_t>(scenario * n);
            for (int index = 0; index < n; ++index) {
                const std::size_t slot = offset + static_cast<std::size_t>(index);
                w1[slot] += rho * (x[slot] - z[static_cast<std::size_t>(index)]);
                w2[slot] += rho * (x[slot] - y1[slot]);
                w3[slot] += rho * (x[slot] - y2[slot]);
            }
        });

        double primal2 = 0.0;
        double dual2 = 0.0;
        for (int scenario = 0; scenario < scenarios; ++scenario) {
            const std::size_t offset = static_cast<std::size_t>(scenario * n);
            for (int index = 0; index < n; ++index) {
                const std::size_t slot = offset + static_cast<std::size_t>(index);
                const double xz = x[slot] - z[static_cast<std::size_t>(index)];
                const double xy1 = x[slot] - y1[slot];
                const double xy2 = x[slot] - y2[slot];
                primal2 += xz * xz + xy1 * xy1 + xy2 * xy2;
                const double dz = z[static_cast<std::size_t>(index)]
                    - previous_z[static_cast<std::size_t>(index)];
                const double dy1 = y1[slot] - previous_y1[slot];
                const double dy2 = y2[slot] - previous_y2[slot];
                dual2 += dz * dz + dy1 * dy1 + dy2 * dy2;
            }
        }
        const double denominator = std::sqrt(
            3.0 * static_cast<double>(scenarios * n));
        const double primal = std::sqrt(primal2) / denominator;
        const double dual = rho * std::sqrt(dual2) / denominator;
        const auto rounded = exact_cardinality_round(z, problem.data_->turbines);
        const double deviation = rounding_deviation(z, rounded);
        result.iterations.push_back({
            iteration + 1, rho, primal, dual, deviation, subproblem_wall
        });
        result.subproblem_seconds += subproblem_wall;
        result.scenario_subproblem_solves += scenarios;
        result.admm_iterations = iteration + 1;
        if (primal <= config.convergence_tolerance
            && dual <= config.convergence_tolerance
            && deviation <= 0.05) {
            break;
        }
        if (primal > 10.0 * dual) rho *= 2.0;
        else if (dual > 10.0 * primal) rho *= 0.5;
        else if (deviation > 0.05) rho *= 1.5;
        rho = std::clamp(rho, 1.0e-4, 1.0e4);
    }

    result.algorithm_seconds = elapsed(algorithm_start);
    result.selected_cells = exact_cardinality_round(z, problem.data_->turbines);
    result.final_rounding_deviation = rounding_deviation(z, result.selected_cells);
    result.final_rho = rho;
    result.final_evaluation = problem.evaluate(result.selected_cells, executor);
    ++result.complete_layout_evaluations;
    result.evaluator_seconds = result.initial_evaluation.seconds
        + result.final_evaluation.seconds;
    result.observed_workers = std::max({
        problem.data_->matrix_workers,
        result.initial_evaluation.observed_workers,
        result.final_evaluation.observed_workers,
        subproblem_observed_workers,
    });
    result.end_to_end_seconds = problem.data_->matrix_build_seconds
        + elapsed(total_start);
    result.scientific_hash = hash_result(result);
    if (static_cast<int>(result.selected_cells.size()) != result.turbines
        || !std::isfinite(result.final_evaluation.net_aep_gwh)
        || result.final_evaluation.net_aep_gwh <= 0.0
        || result.final_evaluation.net_aep_gwh
            > result.final_evaluation.gross_aep_gwh + 1.0e-9) {
        throw std::runtime_error("Y13 final scientific validation failed");
    }
    return result;
}

}  // namespace core99::y13
