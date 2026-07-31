/*
WFLOP IMPLEMENTATION FACT DECLARATION
Implementation unit: T68 pure-C++ FINO3 LPC evaluator and all-core APSO
Paper DOI: 10.1109/TSTE.2016.2614266
Public source, missing information, conflicts, deterministic completions,
semantic IDs, HPC design, controlling contract, and claim boundary:
include/core99/hou_t68.hpp
Claim boundary: academic flexible reconstruction, not author numerical replay.
Last evidence-audit date: 2026-07-31
END WFLOP IMPLEMENTATION FACT DECLARATION
*/
#include "core99/hou_t68.hpp"

#include "fode/executor.hpp"
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
#include <utility>

namespace core99::t68 {
namespace {

using Clock = std::chrono::steady_clock;
constexpr int kRows = 10;
constexpr int kColumns = 8;
constexpr int kTurbines = kRows * kColumns;
constexpr int kDirections = 12;
constexpr int kSpeedBins = 5;
constexpr int kWindStates = kDirections * kSpeedBins;
constexpr double kRadiusM = 63.0;
constexpr double kDiameterM = 126.0;
constexpr double kSevenDiameterM = 7.0 * kDiameterM;
constexpr double kMinimumGapM = 8.0 * kRadiusM;
constexpr double kMaximumGapM = 40.0 * kRadiusM;
constexpr double kAirDensity = 1.225;
constexpr double kWakeDecay = 0.04;
constexpr double kAnnualHours = 8760.0;
constexpr double kRatedPowerMw = 5.0;
constexpr double kRatedWindMps = 11.4;
constexpr double kCutInMps = 3.0;
constexpr double kCutOutMps = 25.0;
constexpr double kRotorMaximumRpm = 12.1;
constexpr double kCollectionVoltageV = 66.0e3;
constexpr double kExportVoltageV = 300.0e3;
constexpr double kCopperResistivityOhmM = 1.724e-8;
constexpr double kCollectionAreaM2 = 500.0e-6;
constexpr double kExportAreaM2 = 1000.0e-6;
constexpr double kExportLengthM = 55.0e3;
constexpr double kPenaltyFactor = 1.0e6;
constexpr double kDiscountRate = 0.10;
constexpr int kLifetimeYears = 20;

struct Point {
    double x = 0.0;
    double y = 0.0;
};

struct RawEvaluation {
    double gross_energy_gwh = 0.0;
    double cable_loss_gwh = 0.0;
    double cable_cost_units = 0.0;
    double pitch_violation = 0.0;
    double theta_deg = 0.0;
    double minimum_spacing_m = 0.0;
    bool feasible = false;
};

struct Calibration {
    double gross_scale = 1.0;
    double loss_scale = 1.0;
    double cable_scale = 1.0;
    double annual_oam_mdkk = 0.0;
};

struct Individual {
    std::vector<double> position;
    std::vector<double> velocity;
    std::vector<double> personal_best;
    Evaluation evaluation;
    double fitness = std::numeric_limits<double>::infinity();
    double personal_best_fitness = std::numeric_limits<double>::infinity();
};

enum class EvolutionaryState {
    exploration = 0,
    exploitation = 1,
    convergence = 2,
    jumping_out = 3,
};

double elapsed(const Clock::time_point& start) {
    return std::chrono::duration<double>(Clock::now() - start).count();
}

double cp_surface(const double beta_deg, const double lambda) {
    if (lambda <= 0.0) return 0.0;
    const double beta = std::clamp(beta_deg, 0.0, 45.0);
    // The target and cited dispatch paper omit the CP(beta,lambda) array.
    // This smooth declared surface keeps beta=0 at the NREL-lineage 0.48/8.1
    // peak, shifts the optimal TSR under pitching enough to obey 12.1 rpm,
    // and still permits rated power above 11.4 m/s as the target results do.
    const double lambda_peak = std::max(2.0, 8.1 - 0.13 * beta);
    const double cp_peak = 0.48 * (1.0 - 0.0105 * beta);
    const double displacement = lambda - lambda_peak;
    return std::clamp(
        cp_peak * std::exp(-0.055 * displacement * displacement),
        0.0,
        0.593
    );
}

struct PitchPoint {
    double lambda_opt = 0.0;
    double cp_opt = 0.0;
};

const std::array<PitchPoint, 4501>& pitch_table() {
    static const std::array<PitchPoint, 4501> table = [] {
        std::array<PitchPoint, 4501> result{};
        for (std::size_t index = 0; index < result.size(); ++index) {
            const double beta = 0.01 * static_cast<double>(index);
            double left = 0.5;
            double right = 14.0;
            for (int iteration = 0; iteration < 48; ++iteration) {
                const double one = left + (right - left) / 3.0;
                const double two = right - (right - left) / 3.0;
                if (cp_surface(beta, one) < cp_surface(beta, two)) {
                    left = one;
                } else {
                    right = two;
                }
            }
            const double lambda = 0.5 * (left + right);
            result[index] = {lambda, cp_surface(beta, lambda)};
        }
        return result;
    }();
    return table;
}

PitchPoint pitch_point(const double beta_deg) {
    const double scaled = 100.0 * std::clamp(beta_deg, 0.0, 45.0);
    const int left = std::min(4500, static_cast<int>(scaled));
    const int right = std::min(4500, left + 1);
    const double fraction = scaled - static_cast<double>(left);
    const auto& table = pitch_table();
    return {
        table[static_cast<std::size_t>(left)].lambda_opt
            + fraction * (
                table[static_cast<std::size_t>(right)].lambda_opt
                - table[static_cast<std::size_t>(left)].lambda_opt
            ),
        table[static_cast<std::size_t>(left)].cp_opt
            + fraction * (
                table[static_cast<std::size_t>(right)].cp_opt
                - table[static_cast<std::size_t>(left)].cp_opt
            ),
    };
}

double rotor_feasible_beta(const double speed_mps) {
    if (speed_mps <= 1.0e-9) return 0.0;
    const double rotor_omega = kRotorMaximumRpm
        * 2.0 * std::numbers::pi / 60.0;
    const double lambda_limit = rotor_omega * kRadiusM / speed_mps;
    if (pitch_point(0.0).lambda_opt <= lambda_limit) return 0.0;
    // The cited CP surface has a short initial nonmonotone segment. Search the
    // declared 0.01-degree table exactly rather than assuming monotonicity.
    for (int index = 1; index <= 4500; ++index) {
        const double beta = 0.01 * static_cast<double>(index);
        if (pitch_point(beta).lambda_opt <= lambda_limit) return beta;
    }
    return 45.0;
}

double cp_derivative_lambda(const double beta, const double lambda) {
    constexpr double step = 1.0e-4;
    return (
        cp_surface(beta, lambda + step)
        - cp_surface(beta, std::max(1.0e-6, lambda - step))
    ) / (2.0 * step);
}

double thrust_from_cp(const double cp) {
    double low = 0.0;
    double high = 1.0 / 3.0;
    const double target = std::clamp(cp, 0.0, 16.0 / 27.0);
    for (int iteration = 0; iteration < 36; ++iteration) {
        const double induction = 0.5 * (low + high);
        const double estimate = 4.0 * induction
            * (1.0 - induction) * (1.0 - induction);
        if (estimate < target) low = induction;
        else high = induction;
    }
    const double induction = 0.5 * (low + high);
    return std::clamp(4.0 * induction * (1.0 - induction), 0.0, 0.999);
}

double circle_overlap(
    const double first_radius,
    const double second_radius,
    const double separation
) {
    if (separation >= first_radius + second_radius) return 0.0;
    if (separation <= std::abs(first_radius - second_radius)) {
        const double radius = std::min(first_radius, second_radius);
        return std::numbers::pi * radius * radius;
    }
    const double first_cos = std::clamp(
        (separation * separation + first_radius * first_radius
            - second_radius * second_radius)
            / (2.0 * separation * first_radius),
        -1.0,
        1.0
    );
    const double second_cos = std::clamp(
        (separation * separation + second_radius * second_radius
            - first_radius * first_radius)
            / (2.0 * separation * second_radius),
        -1.0,
        1.0
    );
    const double radicand = std::max(
        0.0,
        (-separation + first_radius + second_radius)
            * (separation + first_radius - second_radius)
            * (separation - first_radius + second_radius)
            * (separation + first_radius + second_radius)
    );
    return first_radius * first_radius * std::acos(first_cos)
        + second_radius * second_radius * std::acos(second_cos)
        - 0.5 * std::sqrt(radicand);
}

std::vector<WindState> reconstructed_wind_rose() {
    // Figure-6 radial stack digitization, clockwise from North. Values are
    // relative percentage-point lengths and are normalized only once here.
    const std::array<std::array<double, 5>, 12> stacks{{
        {{0.4, 3.0, 0.3, 0.0, 0.0}},
        {{0.5, 3.0, 3.6, 0.9, 0.0}},
        {{0.5, 4.0, 5.0, 1.4, 0.0}},
        {{0.5, 4.0, 5.0, 1.5, 0.0}},
        {{0.5, 3.4, 3.5, 0.7, 0.0}},
        {{0.5, 3.3, 2.5, 0.6, 0.0}},
        {{0.5, 5.0, 2.4, 0.5, 0.0}},
        {{0.5, 5.0, 8.0, 2.4, 0.4}},
        {{0.5, 4.5, 8.4, 3.0, 0.9}},
        {{0.5, 5.0, 5.5, 1.8, 0.4}},
        {{0.5, 5.0, 5.4, 1.0, 0.2}},
        {{0.5, 4.5, 5.0, 1.0, 0.2}},
    }};
    constexpr std::array<double, 5> speeds{2.5, 7.5, 12.5, 17.5, 22.5};
    double total = 0.0;
    for (const auto& direction : stacks) {
        total += std::accumulate(direction.begin(), direction.end(), 0.0);
    }
    std::vector<WindState> result;
    result.reserve(kWindStates);
    for (int direction = 0; direction < kDirections; ++direction) {
        for (int speed = 0; speed < kSpeedBins; ++speed) {
            result.push_back({
                30.0 * static_cast<double>(direction),
                speeds[static_cast<std::size_t>(speed)],
                stacks[static_cast<std::size_t>(direction)]
                    [static_cast<std::size_t>(speed)] / total,
            });
        }
    }
    return result;
}

std::vector<double> decode_gaps_x(
    const Role role,
    const std::vector<double>& decision
) {
    if (
        role == Role::scenario_i_spacing
        || role == Role::scenario_ii_spacing_direction
        || role == Role::scenario_iv_codesign
    ) {
        return {decision.begin(), decision.begin() + 7};
    }
    return std::vector<double>(7, kSevenDiameterM);
}

std::vector<double> decode_gaps_y(
    const Role role,
    const std::vector<double>& decision
) {
    if (
        role == Role::scenario_i_spacing
        || role == Role::scenario_ii_spacing_direction
        || role == Role::scenario_iv_codesign
    ) {
        return {decision.begin() + 7, decision.begin() + 16};
    }
    return std::vector<double>(9, kSevenDiameterM);
}

double decode_theta(const Role role, const std::vector<double>& decision) {
    if (role == Role::direction_only) return decision[0];
    if (
        role == Role::scenario_ii_spacing_direction
        || role == Role::scenario_iv_codesign
    ) {
        return decision[16];
    }
    if (role == Role::scenario_iii_pitch) return -14.89;
    return 0.0;
}

int pitch_offset(const Role role) {
    if (role == Role::scenario_iii_pitch) return 0;
    if (role == Role::scenario_iv_codesign) return 17;
    return -1;
}

std::array<Point, kTurbines> make_layout(
    const std::vector<double>& gap_x,
    const std::vector<double>& gap_y,
    const double theta_deg
) {
    std::array<double, kColumns> x{};
    std::array<double, kRows> y{};
    for (int column = 1; column < kColumns; ++column) {
        x[static_cast<std::size_t>(column)] =
            x[static_cast<std::size_t>(column - 1)]
            + gap_x[static_cast<std::size_t>(column - 1)];
    }
    for (int row = 1; row < kRows; ++row) {
        y[static_cast<std::size_t>(row)] =
            y[static_cast<std::size_t>(row - 1)]
            + gap_y[static_cast<std::size_t>(row - 1)];
    }
    const double center_x = 0.5 * x.back();
    const double center_y = 0.5 * y.back();
    const double angle = theta_deg * std::numbers::pi / 180.0;
    const double cosine = std::cos(angle);
    const double sine = std::sin(angle);
    std::array<Point, kTurbines> result{};
    for (int row = 0; row < kRows; ++row) {
        for (int column = 0; column < kColumns; ++column) {
            const double original_x =
                x[static_cast<std::size_t>(column)] - center_x;
            const double original_y =
                y[static_cast<std::size_t>(row)] - center_y;
            result[static_cast<std::size_t>(row * kColumns + column)] = {
                cosine * original_x - sine * original_y,
                sine * original_x + cosine * original_y,
            };
        }
    }
    return result;
}

double cable_cost_units(const std::array<Point, kTurbines>& layout) {
    constexpr Point substation{4.5 * kSevenDiameterM, 0.0};
    double collection_km = 0.0;
    for (int row = 0; row < kRows; ++row) {
        for (int column = 0; column + 1 < kColumns; ++column) {
            const Point& left = layout[
                static_cast<std::size_t>(row * kColumns + column)
            ];
            const Point& right = layout[
                static_cast<std::size_t>(row * kColumns + column + 1)
            ];
            collection_km += std::hypot(right.x - left.x, right.y - left.y)
                / 1000.0;
        }
        const Point& terminal = layout[
            static_cast<std::size_t>(row * kColumns + kColumns - 1)
        ];
        collection_km += std::hypot(
            terminal.x - substation.x,
            terminal.y - substation.y
        ) / 1000.0;
    }
    // The unavailable Ap/Bp/Cp values are represented by one collection-unit
    // curve and a fixed 55-km export component, then Table-II calibrated.
    return 0.70 * collection_km + 5.0 * (kExportLengthM / 1000.0);
}

struct StatePhysics {
    double gross_power_mw = 0.0;
    double cable_loss_mw = 0.0;
    double pitch_violation = 0.0;
};

StatePhysics evaluate_state(
    const std::array<Point, kTurbines>& layout,
    const WindState& wind,
    const double* pitch
) {
    const double angle = wind.direction_deg * std::numbers::pi / 180.0;
    // Meteorological direction is the bearing of the source; this is the
    // downwind flow vector in the x-East/y-North frame.
    const double flow_x = -std::sin(angle);
    const double flow_y = -std::cos(angle);
    std::array<double, kTurbines> downwind{};
    std::array<double, kTurbines> crosswind{};
    std::array<int, kTurbines> order{};
    for (int turbine = 0; turbine < kTurbines; ++turbine) {
        const auto& point = layout[static_cast<std::size_t>(turbine)];
        downwind[static_cast<std::size_t>(turbine)] =
            point.x * flow_x + point.y * flow_y;
        crosswind[static_cast<std::size_t>(turbine)] =
            -point.x * flow_y + point.y * flow_x;
        order[static_cast<std::size_t>(turbine)] = turbine;
    }
    std::stable_sort(order.begin(), order.end(), [&](const int left, const int right) {
        return downwind[static_cast<std::size_t>(left)]
            < downwind[static_cast<std::size_t>(right)];
    });
    std::array<double, kTurbines> speed{};
    std::array<double, kTurbines> power{};
    std::array<double, kTurbines> thrust{};
    StatePhysics result;
    const double rotor_omega = kRotorMaximumRpm
        * 2.0 * std::numbers::pi / 60.0;
    for (int rank = 0; rank < kTurbines; ++rank) {
        const int target = order[static_cast<std::size_t>(rank)];
        double deficit_square = 0.0;
        for (int prior = 0; prior < rank; ++prior) {
            const int source = order[static_cast<std::size_t>(prior)];
            const double distance =
                downwind[static_cast<std::size_t>(target)]
                - downwind[static_cast<std::size_t>(source)];
            if (distance <= 0.0) continue;
            const double wake_radius = kRadiusM + kWakeDecay * distance;
            const double lateral = std::abs(
                crosswind[static_cast<std::size_t>(target)]
                - crosswind[static_cast<std::size_t>(source)]
            );
            if (lateral >= wake_radius + kRadiusM) continue;
            const double overlap = circle_overlap(
                wake_radius, kRadiusM, lateral
            ) / (std::numbers::pi * kRadiusM * kRadiusM);
            const double deficit = (
                1.0 - std::sqrt(std::max(
                    0.0,
                    1.0 - thrust[static_cast<std::size_t>(source)]
                ))
            ) * (kRadiusM * kRadiusM)
                / (wake_radius * wake_radius) * overlap;
            deficit_square += deficit * deficit;
        }
        const double local_speed = wind.speed_mps * std::max(
            0.0,
            1.0 - std::sqrt(deficit_square)
        );
        speed[static_cast<std::size_t>(target)] = local_speed;
        // The paper compares optimized control with MPPT. The unavailable
        // controller is reconstructed as the minimum pitch needed for the
        // published NREL rotor-speed limit; Opt4 adds nonnegative dispatch
        // pitch to that physical baseline rather than deleting MPPT pitching.
        const double additional_beta = pitch == nullptr
            ? 0.0 : pitch[static_cast<std::size_t>(target)];
        const double beta = std::min(
            45.0,
            rotor_feasible_beta(local_speed) + additional_beta
        );
        const PitchPoint optimum = pitch_point(beta);
        const double lambda_limit = local_speed > 1.0e-9
            ? rotor_omega * kRadiusM / local_speed
            : optimum.lambda_opt;
        const double lambda = std::min(optimum.lambda_opt, lambda_limit);
        const double cp = cp_surface(beta, lambda);
        thrust[static_cast<std::size_t>(target)] = thrust_from_cp(cp);
        if (local_speed >= kCutInMps && local_speed <= kCutOutMps) {
            const double aerodynamic = 0.5 * kAirDensity
                * std::numbers::pi * kRadiusM * kRadiusM
                * cp * local_speed * local_speed * local_speed / 1.0e6;
            power[static_cast<std::size_t>(target)] = std::min(
                kRatedPowerMw, aerodynamic
            );
        }
        if (pitch != nullptr) {
            result.pitch_violation += std::max(
                0.0,
                cp_derivative_lambda(beta, lambda) - 1.0e-5
            );
        }
    }
    result.gross_power_mw = std::accumulate(
        power.begin(), power.end(), 0.0
    );

    constexpr Point substation{4.5 * kSevenDiameterM, 0.0};
    for (int row = 0; row < kRows; ++row) {
        double cumulative_mw = 0.0;
        for (int column = 0; column < kColumns; ++column) {
            cumulative_mw += power[
                static_cast<std::size_t>(row * kColumns + column)
            ];
            double length = 0.0;
            const Point& current = layout[
                static_cast<std::size_t>(row * kColumns + column)
            ];
            if (column + 1 < kColumns) {
                const Point& next = layout[
                    static_cast<std::size_t>(row * kColumns + column + 1)
                ];
                length = std::hypot(next.x - current.x, next.y - current.y);
            } else {
                length = std::hypot(
                    substation.x - current.x,
                    substation.y - current.y
                );
            }
            const double current_a = cumulative_mw * 1.0e6
                / (std::sqrt(3.0) * kCollectionVoltageV);
            const double resistance = kCopperResistivityOhmM * length
                / kCollectionAreaM2;
            result.cable_loss_mw += 3.0 * current_a * current_a
                * resistance / 1.0e6;
        }
    }
    const double export_current_a = std::max(
        0.0,
        result.gross_power_mw - result.cable_loss_mw
    ) * 1.0e6 / (std::sqrt(3.0) * kExportVoltageV);
    const double export_resistance = kCopperResistivityOhmM
        * kExportLengthM / kExportAreaM2;
    result.cable_loss_mw += 3.0 * export_current_a * export_current_a
        * export_resistance / 1.0e6;
    return result;
}

RawEvaluation evaluate_raw(
    const Role role,
    const std::vector<double>& decision,
    const std::vector<WindState>& wind_states
) {
    const auto gaps_x = decode_gaps_x(role, decision);
    const auto gaps_y = decode_gaps_y(role, decision);
    const double theta = decode_theta(role, decision);
    const auto layout = make_layout(gaps_x, gaps_y, theta);
    RawEvaluation result;
    result.theta_deg = theta;
    result.minimum_spacing_m = std::min(
        *std::min_element(gaps_x.begin(), gaps_x.end()),
        *std::min_element(gaps_y.begin(), gaps_y.end())
    );
    result.feasible = result.minimum_spacing_m + 1.0e-9 >= kMinimumGapM;
    result.cable_cost_units = cable_cost_units(layout);
    const int offset = pitch_offset(role);
    for (int state = 0; state < kWindStates; ++state) {
        const double* pitch = offset < 0
            ? nullptr
            : decision.data() + offset + state * kTurbines;
        const auto physical = evaluate_state(
            layout,
            wind_states[static_cast<std::size_t>(state)],
            pitch
        );
        const double annual_weight = kAnnualHours
            * wind_states[static_cast<std::size_t>(state)].probability
            / 1000.0;
        result.gross_energy_gwh += physical.gross_power_mw * annual_weight;
        result.cable_loss_gwh += physical.cable_loss_mw * annual_weight;
        result.pitch_violation += physical.pitch_violation;
    }
    return result;
}

double annuity_factor() {
    const double compound = std::pow(1.0 + kDiscountRate, kLifetimeYears);
    return kDiscountRate * compound / (compound - 1.0);
}

const Calibration& calibration(const std::vector<WindState>& wind_states) {
    static const Calibration value = [&] {
        const std::vector<double> theta_zero{0.0};
        const RawEvaluation raw = evaluate_raw(
            Role::direction_only, theta_zero, wind_states
        );
        if (
            raw.gross_energy_gwh <= 0.0
            || raw.cable_loss_gwh <= 0.0
            || raw.cable_cost_units <= 0.0
        ) {
            throw std::runtime_error("T68 invalid Table-II calibration base");
        }
        Calibration result;
        result.gross_scale = 1972.9 / raw.gross_energy_gwh;
        result.loss_scale = 34.24 / raw.cable_loss_gwh;
        result.cable_scale = 345.25 / raw.cable_cost_units;
        const double reference_net = 1972.9 - 34.24;
        result.annual_oam_mdkk = 178.14 * reference_net / 1000.0
            - annuity_factor() * 345.25;
        return result;
    }();
    return value;
}

Evaluation finalize(
    const RawEvaluation& raw,
    const Calibration& factors
) {
    Evaluation result;
    result.gross_energy_gwh = raw.gross_energy_gwh * factors.gross_scale;
    result.cable_loss_gwh = raw.cable_loss_gwh * factors.loss_scale;
    result.net_energy_gwh = std::max(
        1.0e-12,
        result.gross_energy_gwh - result.cable_loss_gwh
    );
    result.cable_cost_mdkk = raw.cable_cost_units * factors.cable_scale;
    result.pitch_penalty_mdkk = raw.pitch_violation * kPenaltyFactor;
    result.annualized_cost_mdkk = annuity_factor()
        * (result.cable_cost_mdkk + result.pitch_penalty_mdkk)
        + factors.annual_oam_mdkk;
    result.lpc_dkk_per_mwh = 1000.0 * result.annualized_cost_mdkk
        / result.net_energy_gwh;
    result.theta_deg = raw.theta_deg;
    result.minimum_spacing_m = raw.minimum_spacing_m;
    result.feasible = raw.feasible && std::isfinite(result.lpc_dkk_per_mwh);
    if (!result.feasible) result.lpc_dkk_per_mwh = 1.0e300;
    return result;
}

double membership(const EvolutionaryState state, const double f) {
    if (state == EvolutionaryState::exploration) {
        if (f <= 0.4 || f > 0.8) return 0.0;
        if (f <= 0.6) return 5.0 * f - 2.0;
        if (f <= 0.7) return 1.0;
        return -10.0 * f + 8.0;
    }
    if (state == EvolutionaryState::exploitation) {
        if (f <= 0.2 || f > 0.6) return 0.0;
        if (f <= 0.3) return 10.0 * f - 2.0;
        if (f <= 0.4) return 1.0;
        return -5.0 * f + 3.0;
    }
    if (state == EvolutionaryState::convergence) {
        if (f <= 0.1) return 1.0;
        if (f <= 0.3) return -5.0 * f + 1.5;
        return 0.0;
    }
    if (f <= 0.7) return 0.0;
    if (f <= 0.9) return 5.0 * f - 3.5;
    return 1.0;
}

EvolutionaryState classify(
    const double f,
    const EvolutionaryState previous
) {
    std::array<double, 4> values{};
    for (int state = 0; state < 4; ++state) {
        values[static_cast<std::size_t>(state)] = membership(
            static_cast<EvolutionaryState>(state), f
        );
    }
    const int previous_index = static_cast<int>(previous);
    const double maximum = *std::max_element(values.begin(), values.end());
    if (values[static_cast<std::size_t>(previous_index)] >= maximum - 1.0e-15
        && values[static_cast<std::size_t>(previous_index)] > 0.0) {
        return previous;
    }
    const int next = (previous_index + 1) % 4;
    if (values[static_cast<std::size_t>(next)] >= maximum - 1.0e-15
        && values[static_cast<std::size_t>(next)] > 0.0) {
        return static_cast<EvolutionaryState>(next);
    }
    return static_cast<EvolutionaryState>(
        static_cast<int>(std::distance(
            values.begin(), std::max_element(values.begin(), values.end())
        ))
    );
}

std::uint64_t hash_result(
    const Individual& best,
    const int generations,
    const std::uint64_t physical_fes
) {
    std::uint64_t hash = 1469598103934665603ULL;
    auto mix = [&](const std::uint64_t value) {
        hash ^= value;
        hash *= 1099511628211ULL;
    };
    mix(static_cast<std::uint64_t>(generations));
    mix(physical_fes);
    mix(std::bit_cast<std::uint64_t>(best.personal_best_fitness));
    for (const double value : best.personal_best) {
        mix(std::bit_cast<std::uint64_t>(value));
    }
    return hash;
}

}  // namespace

std::string role_name(const Role role) {
    switch (role) {
    case Role::direction_only: return "direction_only";
    case Role::scenario_i_spacing: return "scenario_i_spacing";
    case Role::scenario_ii_spacing_direction:
        return "scenario_ii_spacing_direction";
    case Role::scenario_iii_pitch: return "scenario_iii_pitch";
    case Role::scenario_iv_codesign: return "scenario_iv_codesign";
    }
    throw std::invalid_argument("T68 unknown role");
}

Problem::Problem(const Role role)
    : role_(role), id_("t68_" + role_name(role)),
      wind_states_(reconstructed_wind_rose()) {
    if (role == Role::direction_only) {
        dimensions_ = 1;
        population_size_ = 15;
        maximum_iterations_ = 100;
        paper_repeats_ = 10;
        lower_bounds_ = {-90.0};
        upper_bounds_ = {90.0};
    } else if (role == Role::scenario_i_spacing) {
        dimensions_ = 16;
        population_size_ = 30;
        maximum_iterations_ = 50;
        paper_repeats_ = 20;
        lower_bounds_.assign(16, kMinimumGapM);
        upper_bounds_.assign(16, kMaximumGapM);
    } else if (role == Role::scenario_ii_spacing_direction) {
        dimensions_ = 17;
        population_size_ = 35;
        maximum_iterations_ = 70;
        paper_repeats_ = 20;
        lower_bounds_.assign(16, kMinimumGapM);
        upper_bounds_.assign(16, kMaximumGapM);
        lower_bounds_.push_back(-90.0);
        upper_bounds_.push_back(90.0);
    } else if (role == Role::scenario_iii_pitch) {
        dimensions_ = kTurbines * kWindStates;
        population_size_ = 100;
        maximum_iterations_ = 120;
        paper_repeats_ = 20;
        lower_bounds_.assign(static_cast<std::size_t>(dimensions_), 0.0);
        upper_bounds_.assign(static_cast<std::size_t>(dimensions_), 45.0);
    } else {
        dimensions_ = 17 + kTurbines * kWindStates;
        population_size_ = 120;
        maximum_iterations_ = 230;
        paper_repeats_ = 20;
        lower_bounds_.assign(16, kMinimumGapM);
        upper_bounds_.assign(16, kMaximumGapM);
        lower_bounds_.push_back(-90.0);
        upper_bounds_.push_back(90.0);
        lower_bounds_.insert(
            lower_bounds_.end(), kTurbines * kWindStates, 0.0
        );
        upper_bounds_.insert(
            upper_bounds_.end(), kTurbines * kWindStates, 45.0
        );
    }
}

Role Problem::role() const noexcept { return role_; }
const std::string& Problem::id() const noexcept { return id_; }
int Problem::dimensions() const noexcept { return dimensions_; }
int Problem::population_size() const noexcept { return population_size_; }
int Problem::maximum_iterations() const noexcept { return maximum_iterations_; }
int Problem::paper_repeats() const noexcept { return paper_repeats_; }
const std::vector<double>& Problem::lower_bounds() const noexcept {
    return lower_bounds_;
}
const std::vector<double>& Problem::upper_bounds() const noexcept {
    return upper_bounds_;
}
const std::vector<WindState>& Problem::wind_states() const noexcept {
    return wind_states_;
}

std::vector<double> Problem::reference_decision() const {
    std::vector<double> result(static_cast<std::size_t>(dimensions_), 0.0);
    if (role_ == Role::direction_only) {
        result[0] = 0.0;
    } else if (role_ == Role::scenario_i_spacing) {
        std::fill(result.begin(), result.end(), kSevenDiameterM);
    } else if (role_ == Role::scenario_ii_spacing_direction) {
        std::fill(result.begin(), result.begin() + 16, kSevenDiameterM);
        result[16] = -14.89;
    } else if (role_ == Role::scenario_iv_codesign) {
        std::fill(result.begin(), result.begin() + 16, kSevenDiameterM);
        result[16] = -14.89;
    }
    return result;
}

Evaluation Problem::evaluate(const std::vector<double>& decision) const {
    if (static_cast<int>(decision.size()) != dimensions_) {
        throw std::invalid_argument("T68 decision dimension mismatch");
    }
    for (int coordinate = 0; coordinate < dimensions_; ++coordinate) {
        if (
            decision[static_cast<std::size_t>(coordinate)]
                < lower_bounds_[static_cast<std::size_t>(coordinate)] - 1.0e-9
            || decision[static_cast<std::size_t>(coordinate)]
                > upper_bounds_[static_cast<std::size_t>(coordinate)] + 1.0e-9
        ) {
            Evaluation invalid;
            invalid.lpc_dkk_per_mwh = 1.0e300;
            return invalid;
        }
    }
    return finalize(
        evaluate_raw(role_, decision, wind_states_),
        calibration(wind_states_)
    );
}

std::vector<std::string> paper_case_ids() {
    std::vector<std::string> result;
    for (const Role role : {
        Role::direction_only,
        Role::scenario_i_spacing,
        Role::scenario_ii_spacing_direction,
        Role::scenario_iii_pitch,
        Role::scenario_iv_codesign,
    }) {
        result.push_back(Problem(role).id());
    }
    return result;
}

RunResult run(const Problem& problem, const RunConfig& config) {
    if (
        config.workers < 1
        || config.unchanged_iterations < 1
        || config.population_override < 0
        || config.iteration_override < 0
    ) {
        throw std::invalid_argument("T68 run configuration invalid");
    }
    const int population_size = config.population_override > 0
        ? config.population_override : problem.population_size();
    const int maximum_iterations = config.iteration_override > 0
        ? config.iteration_override : problem.maximum_iterations();
    if (population_size < 4 || maximum_iterations < 1) {
        throw std::invalid_argument("T68 population/iteration override invalid");
    }
    const auto started = Clock::now();
    fode::PersistentExecutor executor(config.workers);
    executor.reset_work_receipt();
    const fode::CounterRng rng(config.seed);
    const int dimensions = problem.dimensions();
    const auto& lower = problem.lower_bounds();
    const auto& upper = problem.upper_bounds();
    const auto reference = problem.reference_decision();
    const int problem_pitch_offset = pitch_offset(problem.role());
    std::vector<Individual> population(
        static_cast<std::size_t>(population_size)
    );
    executor.parallel_for(0, population_size, [&](const int index) {
        auto& individual = population[static_cast<std::size_t>(index)];
        individual.position.resize(static_cast<std::size_t>(dimensions));
        individual.velocity.resize(static_cast<std::size_t>(dimensions));
        for (int coordinate = 0; coordinate < dimensions; ++coordinate) {
            const double range = upper[static_cast<std::size_t>(coordinate)]
                - lower[static_cast<std::size_t>(coordinate)];
            if (index == 0) {
                individual.position[static_cast<std::size_t>(coordinate)] =
                    reference[static_cast<std::size_t>(coordinate)];
            } else if (coordinate >= problem_pitch_offset
                && problem_pitch_offset >= 0) {
                const bool active = rng.uniform(
                    0, 6801, index, coordinate, 0
                ) < 0.02;
                individual.position[static_cast<std::size_t>(coordinate)] =
                    active
                    ? 5.0 * rng.uniform(0, 6801, index, coordinate, 1)
                    : 0.0;
            } else {
                individual.position[static_cast<std::size_t>(coordinate)] =
                    lower[static_cast<std::size_t>(coordinate)]
                    + range * rng.uniform(0, 6801, index, coordinate);
            }
            if (coordinate >= problem_pitch_offset && problem_pitch_offset >= 0) {
                individual.velocity[static_cast<std::size_t>(coordinate)] =
                    individual.position[static_cast<std::size_t>(coordinate)] > 0.0
                    ? 0.5 * (
                        2.0 * rng.uniform(0, 6802, index, coordinate) - 1.0
                    ) : 0.0;
            } else {
                individual.velocity[static_cast<std::size_t>(coordinate)] =
                    0.2 * range * (
                        2.0 * rng.uniform(0, 6802, index, coordinate) - 1.0
                    );
            }
        }
    });
    double evaluator_seconds = 0.0;
    auto evaluate_population = [&] {
        const auto evaluation_started = Clock::now();
        executor.parallel_for(0, population_size, [&](const int index) {
            auto& individual = population[static_cast<std::size_t>(index)];
            individual.evaluation = problem.evaluate(individual.position);
            individual.fitness = individual.evaluation.lpc_dkk_per_mwh;
        });
        evaluator_seconds += elapsed(evaluation_started);
    };
    evaluate_population();
    std::uint64_t physical_fes = static_cast<std::uint64_t>(population_size);
    for (auto& individual : population) {
        individual.personal_best = individual.position;
        individual.personal_best_fitness = individual.fitness;
    }
    auto best_index = [&] {
        int best = 0;
        for (int index = 1; index < population_size; ++index) {
            if (population[static_cast<std::size_t>(index)]
                    .personal_best_fitness
                < population[static_cast<std::size_t>(best)]
                    .personal_best_fitness) {
                best = index;
            }
        }
        return best;
    };
    int global_index = best_index();
    std::vector<double> global_best = population[
        static_cast<std::size_t>(global_index)
    ].personal_best;
    double global_fitness = population[
        static_cast<std::size_t>(global_index)
    ].personal_best_fitness;
    double c1 = 2.0;
    double c2 = 2.0;
    EvolutionaryState state = EvolutionaryState::exploration;
    int unchanged = 0;
    int generations = 0;
    bool converged = false;
    std::vector<double> distance_matrix(
        static_cast<std::size_t>(population_size * population_size), 0.0
    );
    std::vector<double> mean_distances(
        static_cast<std::size_t>(population_size), 0.0
    );
    while (generations < maximum_iterations && !converged) {
        ++generations;
        const auto prior_best = global_fitness;

        // Zhan ESE requires every particle-particle Euclidean distance. The
        // HPC form computes each upper-triangle pair exactly once and stores
        // its symmetric entry before parallel ordered row reductions.
        executor.parallel_for(0, population_size, [&](const int left) {
            distance_matrix[static_cast<std::size_t>(
                left * population_size + left
            )] = 0.0;
            const auto& first = population[
                static_cast<std::size_t>(left)
            ].position;
            for (int right = left + 1; right < population_size; ++right) {
                const auto& second = population[
                    static_cast<std::size_t>(right)
                ].position;
                double squared = 0.0;
                #pragma omp simd reduction(+:squared)
                for (int coordinate = 0; coordinate < dimensions; ++coordinate) {
                    const double delta =
                        first[static_cast<std::size_t>(coordinate)]
                        - second[static_cast<std::size_t>(coordinate)];
                    squared += delta * delta;
                }
                const double distance = std::sqrt(squared);
                distance_matrix[static_cast<std::size_t>(
                    left * population_size + right
                )] = distance;
                distance_matrix[static_cast<std::size_t>(
                    right * population_size + left
                )] = distance;
            }
        });
        executor.parallel_for(0, population_size, [&](const int index) {
            double sum = 0.0;
            const auto offset = static_cast<std::size_t>(
                index * population_size
            );
            for (int other = 0; other < population_size; ++other) {
                sum += distance_matrix[offset + static_cast<std::size_t>(other)];
            }
            mean_distances[static_cast<std::size_t>(index)] = sum
                / static_cast<double>(population_size - 1);
        });
        const auto [minimum_it, maximum_it] = std::minmax_element(
            mean_distances.begin(), mean_distances.end()
        );
        const double denominator = *maximum_it - *minimum_it;
        const double factor = denominator > 1.0e-15
            ? (mean_distances[static_cast<std::size_t>(global_index)]
                - *minimum_it) / denominator
            : 0.0;
        state = classify(std::clamp(factor, 0.0, 1.0), state);
        const double inertia = 1.0
            / (1.0 + 1.5 * std::exp(-2.6 * factor));
        const double delta = 0.05 + 0.05 * rng.uniform(
            generations, 6803, 0
        );
        if (state == EvolutionaryState::exploration) {
            c1 += delta;
            c2 -= delta;
        } else if (state == EvolutionaryState::exploitation) {
            c1 += 0.5 * delta;
            c2 -= 0.5 * delta;
        } else if (state == EvolutionaryState::convergence) {
            c1 += 0.5 * delta;
            c2 += 0.5 * delta;
        } else {
            c1 -= delta;
            c2 += delta;
        }
        c1 = std::clamp(c1, 1.5, 2.5);
        c2 = std::clamp(c2, 1.5, 2.5);
        if (c1 + c2 > 4.0) {
            const double scale = 4.0 / (c1 + c2);
            c1 *= scale;
            c2 *= scale;
        }

        if (state == EvolutionaryState::convergence) {
            std::vector<double> candidate = global_best;
            const int coordinate = rng.integer(
                0, dimensions, generations, 6804, 0
            );
            const double sigma = 1.0 - 0.9
                * static_cast<double>(generations)
                / static_cast<double>(maximum_iterations);
            candidate[static_cast<std::size_t>(coordinate)] = std::clamp(
                candidate[static_cast<std::size_t>(coordinate)]
                    + (upper[static_cast<std::size_t>(coordinate)]
                        - lower[static_cast<std::size_t>(coordinate)])
                        * sigma * rng.normal(
                            generations, 6805, 0, coordinate
                        ),
                lower[static_cast<std::size_t>(coordinate)],
                upper[static_cast<std::size_t>(coordinate)]
            );
            const auto evaluation_started = Clock::now();
            const Evaluation candidate_evaluation = problem.evaluate(candidate);
            evaluator_seconds += elapsed(evaluation_started);
            ++physical_fes;
            const double candidate_fitness =
                candidate_evaluation.lpc_dkk_per_mwh;
            if (candidate_fitness < global_fitness) {
                global_best = candidate;
                global_fitness = candidate_fitness;
            } else {
                int worst = 0;
                for (int index = 1; index < population_size; ++index) {
                    if (population[static_cast<std::size_t>(index)].fitness
                        > population[static_cast<std::size_t>(worst)].fitness) {
                        worst = index;
                    }
                }
                auto& replaced = population[static_cast<std::size_t>(worst)];
                replaced.position = std::move(candidate);
                replaced.evaluation = candidate_evaluation;
                replaced.fitness = candidate_fitness;
                if (candidate_fitness < replaced.personal_best_fitness) {
                    replaced.personal_best = replaced.position;
                    replaced.personal_best_fitness = candidate_fitness;
                }
            }
        }

        executor.parallel_for(0, population_size, [&](const int index) {
            auto& individual = population[static_cast<std::size_t>(index)];
            for (int coordinate = 0; coordinate < dimensions; ++coordinate) {
                const std::size_t dimension = static_cast<std::size_t>(coordinate);
                const double range = upper[dimension] - lower[dimension];
                const double maximum_velocity = 0.2 * range;
                const double velocity = inertia * individual.velocity[dimension]
                    + c1 * rng.uniform(
                        generations, 6806, index, coordinate, 0
                    ) * (individual.personal_best[dimension]
                        - individual.position[dimension])
                    + c2 * rng.uniform(
                        generations, 6806, index, coordinate, 1
                    ) * (global_best[dimension] - individual.position[dimension]);
                individual.velocity[dimension] = std::clamp(
                    velocity, -maximum_velocity, maximum_velocity
                );
                const double proposed = individual.position[dimension]
                    + individual.velocity[dimension];
                individual.position[dimension] = std::clamp(
                    proposed, lower[dimension], upper[dimension]
                );
                if (proposed != individual.position[dimension]) {
                    individual.velocity[dimension] *= -0.5;
                }
            }
        });
        evaluate_population();
        physical_fes += static_cast<std::uint64_t>(population_size);
        for (auto& individual : population) {
            if (individual.fitness < individual.personal_best_fitness) {
                individual.personal_best = individual.position;
                individual.personal_best_fitness = individual.fitness;
            }
        }
        global_index = best_index();
        if (population[static_cast<std::size_t>(global_index)]
                .personal_best_fitness < global_fitness) {
            global_best = population[
                static_cast<std::size_t>(global_index)
            ].personal_best;
            global_fitness = population[
                static_cast<std::size_t>(global_index)
            ].personal_best_fitness;
        }
        unchanged = std::abs(global_fitness - prior_best)
            <= 1.0e-12 * std::max(1.0, std::abs(prior_best))
            ? unchanged + 1 : 0;
        converged = unchanged >= config.unchanged_iterations;
    }

    int final_best_index = 0;
    for (int index = 1; index < population_size; ++index) {
        if (population[static_cast<std::size_t>(index)].personal_best_fitness
            < population[static_cast<std::size_t>(final_best_index)]
                .personal_best_fitness) {
            final_best_index = index;
        }
    }
    Individual best = population[static_cast<std::size_t>(final_best_index)];
    if (global_fitness < best.personal_best_fitness) {
        best.personal_best = global_best;
        best.personal_best_fitness = global_fitness;
    }
    const Evaluation best_evaluation = problem.evaluate(best.personal_best);
    const double end_to_end = elapsed(started);
    return {
        .case_id = problem.id(),
        .method_semantic_id = "t68_zhan_apso_offshore_codesign_declared_v1",
        .problem_semantic_id =
            "t68_fino3_layout_dispatch_lpc_5role_declared_v1",
        .protocol_semantic_id =
            "t68_native_10plus4x20_repeat_declared_v1",
        .seed = config.seed,
        .requested_workers = config.workers,
        .observed_workers = executor.work_receipt().distinct_participants,
        .population_size = population_size,
        .generations = generations,
        .dimensions = dimensions,
        .physical_fes = physical_fes,
        .converged = converged,
        .evaluator_seconds = evaluator_seconds,
        .algorithm_seconds = std::max(0.0, end_to_end - evaluator_seconds),
        .end_to_end_seconds = end_to_end,
        .scientific_hash = hash_result(best, generations, physical_fes),
        .best_decision = best.personal_best,
        .best_evaluation = best_evaluation,
    };
}

}  // namespace core99::t68
