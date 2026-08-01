/*
WFLOP IMPLEMENTATION FACT DECLARATION
Implementation unit: T58 pure-C++ evaluator, binary SGA and move-limited SLP
Paper/DOI: Rethore et al.; 10.1002/we.1667
All public assets, missing facts, deterministic completions, semantic IDs,
HPC design and claim boundaries are declared in
include/core99/rethore_t58.hpp.
Public source provenance and Claim boundary are inherited verbatim from that
controlling header and contract; this is a project-native implementation.
Controlling contract: shared/contracts/core99_t58_rethore_topfarm_2014.json
Last evidence-audit date: 2026-08-01
END WFLOP IMPLEMENTATION FACT DECLARATION
*/
#include "core99/rethore_t58.hpp"

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
#include <vector>

namespace core99::t58 {
namespace {

using Clock = std::chrono::steady_clock;
constexpr double kPi = std::numbers::pi_v<double>;
constexpr int kPopulation = 20;
constexpr double kCrossover = 0.70;
constexpr double kMutation = 0.025;
constexpr int kElites = 2;
constexpr double kFitnessScaling = 2.0;
constexpr double kElectricityEuroPerMwh = 50.0;
constexpr double kInflation = 0.025;
constexpr double kInterest = 0.065;
constexpr int kLifetimeYears = 20;
constexpr int kLoanPaymentsPerYear = 1;

double elapsed(const Clock::time_point start) {
    return std::chrono::duration<double>(Clock::now() - start).count();
}

double squared_distance(const Point& a, const Point& b) {
    const double dx = a.x_m - b.x_m;
    const double dy = a.y_m - b.y_m;
    return dx * dx + dy * dy;
}

double distance(const Point& a, const Point& b) {
    return std::sqrt(squared_distance(a, b));
}

bool point_inside(const Point& point, const std::vector<Point>& polygon) {
    bool inside = false;
    for (std::size_t i = 0, j = polygon.size() - 1; i < polygon.size(); j = i++) {
        const Point& a = polygon[i];
        const Point& b = polygon[j];
        const bool crossing = ((a.y_m > point.y_m) != (b.y_m > point.y_m))
            && (point.x_m < (b.x_m - a.x_m) * (point.y_m - a.y_m)
                    / (b.y_m - a.y_m) + a.x_m);
        if (crossing) inside = !inside;
    }
    return inside;
}

Point polygon_centroid(const std::vector<Point>& polygon) {
    Point result{};
    for (const Point& point : polygon) {
        result.x_m += point.x_m;
        result.y_m += point.y_m;
    }
    result.x_m /= static_cast<double>(polygon.size());
    result.y_m /= static_cast<double>(polygon.size());
    return result;
}

double segment_distance(const Point& point, const Point& a, const Point& b) {
    const double dx = b.x_m - a.x_m;
    const double dy = b.y_m - a.y_m;
    const double scale = dx * dx + dy * dy;
    if (scale == 0.0) return distance(point, a);
    const double t = std::clamp(
        ((point.x_m - a.x_m) * dx + (point.y_m - a.y_m) * dy) / scale,
        0.0, 1.0
    );
    return distance(point, {a.x_m + t * dx, a.y_m + t * dy});
}

double boundary_violation(const Point& point, const std::vector<Point>& polygon) {
    if (point_inside(point, polygon)) return 0.0;
    double best = std::numeric_limits<double>::infinity();
    for (std::size_t i = 0; i < polygon.size(); ++i) {
        best = std::min(best, segment_distance(
            point, polygon[i], polygon[(i + 1U) % polygon.size()]));
    }
    return best;
}

Point project_inside(Point point, const std::vector<Point>& polygon) {
    if (point_inside(point, polygon)) return point;
    const Point center = polygon_centroid(polygon);
    for (int iteration = 0; iteration < 160; ++iteration) {
        point.x_m = 0.96 * point.x_m + 0.04 * center.x_m;
        point.y_m = 0.96 * point.y_m + 0.04 * center.y_m;
        if (point_inside(point, polygon)) return point;
    }
    return center;
}

double minimum_spacing(const std::vector<Point>& layout) {
    double result = std::numeric_limits<double>::infinity();
    for (std::size_t i = 0; i < layout.size(); ++i) {
        for (std::size_t j = i + 1; j < layout.size(); ++j) {
            result = std::min(result, distance(layout[i], layout[j]));
        }
    }
    return std::isfinite(result) ? result : 0.0;
}

std::uint64_t mix_hash(std::uint64_t hash, const std::uint64_t value) {
    hash ^= value;
    return hash * 1099511628211ULL;
}

std::uint64_t scientific_hash(
    const std::vector<Point>& layout,
    const Evaluation& evaluation,
    const std::uint64_t fes
) {
    std::uint64_t hash = 1469598103934665603ULL;
    for (const Point& point : layout) {
        const double x = std::round(point.x_m * 1.0e8) / 1.0e8;
        const double y = std::round(point.y_m * 1.0e8) / 1.0e8;
        hash = mix_hash(hash, std::bit_cast<std::uint64_t>(x));
        hash = mix_hash(hash, std::bit_cast<std::uint64_t>(y));
    }
    hash = mix_hash(hash, std::bit_cast<std::uint64_t>(
        evaluation.financial_balance_meur));
    return mix_hash(hash, fes);
}

double weibull_cdf(const double speed, const double scale, const double shape) {
    if (speed <= 0.0) return 0.0;
    return 1.0 - std::exp(-std::pow(speed / scale, shape));
}

}  // namespace

struct Problem::Data {
    struct WindState {
        double direction_degrees = 0.0;
        double speed_mps = 0.0;
        double probability = 0.0;
        double turbulence = 0.1;
    };

    struct StateWork {
        double gross_power_mw = 0.0;
        double net_power_mw = 0.0;
        std::array<double, 20> wake_activity{};
    };

    CaseId case_id;
    std::string semantic_id = "t58_topfarm_three_case_financial_declared_v1";
    int turbines = 0;
    double rotor_diameter_m = 0.0;
    double hub_height_m = 0.0;
    double rated_power_mw = 0.0;
    bool offshore = false;
    double cable_euro_per_m = 0.0;
    double target_baseline_efficiency = 0.0;
    double wake_scale = 1.0;
    std::vector<Point> polygon;
    std::vector<Point> baseline;
    std::vector<Point> candidates;
    std::vector<WindState> fine_states;
    std::vector<WindState> coarse_states;

    explicit Data(const CaseId selected) : case_id(selected) {
        initialize_case();
        build_states();
        build_candidates();
        calibrate_wake_scale();
    }

    void initialize_case() {
        if (case_id == CaseId::fictitious_2x3) {
            turbines = 6;
            rotor_diameter_m = 126.0;
            hub_height_m = 90.0;
            rated_power_mw = 5.0;
            offshore = true;
            cable_euro_per_m = 675.0;
            polygon = {{0, 0}, {1200, 0}, {1200, 1200}, {0, 1200}};
            baseline = {{250, 300}, {600, 300}, {950, 300},
                        {250, 900}, {600, 900}, {950, 900}};
            return;
        }
        if (case_id == CaseId::stags_holt_coldham) {
            turbines = 17;
            rotor_diameter_m = 80.0;
            hub_height_m = 60.0;
            rated_power_mw = 2.0;
            offshore = false;
            cable_euro_per_m = 270.0;
            target_baseline_efficiency = 0.894;
            polygon = {{545050,299040}, {544970,299420}, {545250,300270},
                       {545589,301295}, {546380,300097}, {545810,299560}};
            // Figure 6 / report Figure 62 digitization in the published axes.
            baseline = {{545250,300220}, {545660,300130}, {546150,299850},
                        {545270,299900}, {545430,299680}, {545820,299550},
                        {545000,299430}, {545360,299270}, {545060,299050},
                        {545590,301280}, {545520,301010}, {545850,300850},
                        {545400,300620}, {546010,300630}, {546220,300330},
                        {545950,300050}, {546350,300080}};
            for (Point& point : baseline) point = project_inside(point, polygon);
            return;
        }
        turbines = 20;
        rotor_diameter_m = 76.0;
        hub_height_m = 64.0;
        rated_power_mw = 2.0;
        offshore = true;
        cable_euro_per_m = 675.0;
        target_baseline_efficiency = 0.839;
        polygon = {{729437,6179456}, {729314,6178751}, {729372,6177869},
                   {729478,6177100}, {730867,6175689}, {731220,6178318},
                   {730875,6179809}, {729741,6179777}};
        // Public OpenWAKE lineage coordinates pinned by the L0079 package.
        baseline = {
            {730458.93,6179564.99}, {730498.59,6179386.19},
            {730534.82,6179206.96}, {730567.71,6179027.04},
            {730597.40,6178846.51}, {730623.63,6178665.47},
            {730646.62,6178484.06}, {730666.30,6178302.19},
            {730682.87,6178119.86}, {730695.70,6177937.44},
            {730705.46,6177754.84}, {730711.71,6177571.92},
            {730714.74,6177389.07}, {730714.45,6177206.12},
            {730710.62,6177023.25}, {730703.70,6176840.44},
            {730693.34,6176657.73}, {730679.50,6176475.28},
            {730662.60,6176293.10}, {730642.20,6176111.30}
        };
        for (Point& point : baseline) point = project_inside(point, polygon);
    }

    void build_states() {
        std::array<double, 12> direction_probability{};
        std::array<double, 12> scale{};
        std::array<double, 12> shape{};
        std::array<double, 12> turbulence{};
        if (case_id == CaseId::stags_holt_coldham) {
            direction_probability = {0.0483,0.0765,0.0483,0.0498,0.0499,0.0577,
                                     0.0847,0.1577,0.1411,0.0990,0.0839,0.0645};
            scale = {7.05,6.47,6.01,6.18,6.64,7.21,7.63,7.98,7.86,7.60,7.26,6.99};
            shape = {2.19,2.08,2.12,2.28,2.38,2.46,2.25,2.43,2.47,2.32,2.26,2.32};
            turbulence = {0.13,0.11,0.10,0.10,0.10,0.10,0.09,0.10,0.12,0.12,0.12,0.11};
        } else if (case_id == CaseId::middelgrunden) {
            direction_probability = {0.07,0.05,0.05,0.09,0.09,0.08,
                                     0.14,0.10,0.09,0.11,0.08,0.04};
            scale = {7.54,6.77,6.86,7.27,8.02,7.44,7.34,6.74,6.87,7.07,6.76,5.92};
            shape = {2.01,2.32,3.09,2.19,3.00,2.73,2.21,2.32,2.76,2.72,2.42,2.05};
            turbulence = {0.094,0.082,0.085,0.098,0.085,0.099,
                          0.114,0.115,0.109,0.127,0.128,0.121};
        } else {
            // Figure 3(a) digitization: south-westerly sectors dominate.
            direction_probability = {0.05,0.04,0.03,0.03,0.04,0.07,
                                     0.12,0.20,0.18,0.10,0.08,0.06};
            scale.fill(8.0);
            shape.fill(2.2);
            turbulence.fill(0.10);
        }
        for (int direction = 0; direction < 12; ++direction) {
            for (int speed = 4; speed <= 26; speed += 2) {
                const double probability = direction_probability[direction]
                    * (weibull_cdf(speed + 1.0, scale[direction], shape[direction])
                       - weibull_cdf(std::max(0.0, speed - 1.0),
                                     scale[direction], shape[direction]));
                fine_states.push_back({30.0 * direction,
                                       static_cast<double>(speed),
                                       probability, turbulence[direction]});
            }
        }
        // First fidelity aggregates 2x2 direction-speed cells. The fixed
        // representative state carries the exact probability mass of its cell.
        for (int direction = 0; direction < 12; direction += 2) {
            for (int speed_index = 0; speed_index < 12; speed_index += 2) {
                double mass = 0.0;
                double weighted_speed = 0.0;
                double weighted_ti = 0.0;
                for (int dd = 0; dd < 2; ++dd) {
                    for (int ss = 0; ss < 2; ++ss) {
                        const WindState& state = fine_states[static_cast<std::size_t>(
                            (direction + dd) * 12 + speed_index + ss)];
                        mass += state.probability;
                        weighted_speed += state.probability * state.speed_mps;
                        weighted_ti += state.probability * state.turbulence;
                    }
                }
                coarse_states.push_back({30.0 * direction + 15.0,
                    mass > 0.0 ? weighted_speed / mass : 8.0,
                    mass,
                    mass > 0.0 ? weighted_ti / mass : 0.1});
            }
        }
    }

    void build_candidates() {
        double min_x = polygon.front().x_m;
        double max_x = min_x;
        double min_y = polygon.front().y_m;
        double max_y = min_y;
        for (const Point& point : polygon) {
            min_x = std::min(min_x, point.x_m);
            max_x = std::max(max_x, point.x_m);
            min_y = std::min(min_y, point.y_m);
            max_y = std::max(max_y, point.y_m);
        }
        const double dy = rotor_diameter_m * std::sqrt(3.0) / 2.0;
        int row = 0;
        for (double y = min_y; y <= max_y + 1.0e-9; y += dy, ++row) {
            for (double x = min_x + (row % 2 ? 0.5 * rotor_diameter_m : 0.0);
                 x <= max_x + 1.0e-9; x += rotor_diameter_m) {
                const Point point{x, y};
                if (point_inside(point, polygon)) candidates.push_back(point);
            }
        }
        if (candidates.size() < static_cast<std::size_t>(4 * turbines)) {
            throw std::runtime_error("T58 candidate grid is unexpectedly small");
        }
    }

    double power_mw(const double speed) const {
        const double cut_in = case_id == CaseId::fictitious_2x3 ? 3.0 : 5.0;
        const double rated = case_id == CaseId::fictitious_2x3 ? 11.4 : 10.0;
        if (speed < cut_in || speed >= 25.0) return 0.0;
        if (speed >= rated) return rated_power_mw;
        const double ratio = (speed - cut_in) / (rated - cut_in);
        return rated_power_mw * ratio * ratio * ratio;
    }

    double thrust_coefficient(const double speed) const {
        if (speed < 3.0 || speed >= 25.0) return 0.0;
        if (speed < 10.0) return 0.84;
        return std::clamp(0.84 - 0.045 * (speed - 10.0), 0.16, 0.84);
    }

    double larsen_fraction(
        const double downstream,
        const double radius,
        const double ct,
        const double turbulence_value
    ) const {
        if (downstream <= 0.0 || ct <= 0.0) return 0.0;
        const double diameter = rotor_diameter_m;
        const double rotor_radius = 0.5 * diameter;
        const double area = kPi * rotor_radius * rotor_radius;
        const double rnb = std::max(
            1.08 * diameter,
            1.08 * diameter + 21.7 * diameter * (turbulence_value - 0.05)
        );
        const double r95 = 0.5 * (rnb + std::min(hub_height_m, rnb));
        const double denominator = std::pow(2.0 * r95 / diameter, 3.0) - 1.0;
        const double x0 = 9.5 * diameter / std::max(denominator, 1.0e-6);
        const double c1 = std::pow(rotor_radius, 2.5)
            * std::pow(105.0 / (2.0 * kPi), -0.5)
            * std::pow(ct * area * x0, -5.0 / 6.0);
        const double x = downstream + x0;
        const double wake_radius = std::pow(35.0 / (2.0 * kPi), 0.2)
            * std::pow(3.0 * c1 * c1, 0.2)
            * std::pow(ct * area * x, 1.0 / 3.0);
        if (radius >= wake_radius) return 0.0;
        const double first = std::pow(std::max(radius, 0.0), 1.5)
            * std::pow(3.0 * c1 * c1 * ct * area * x, -0.5);
        const double second = std::pow(35.0 / (2.0 * kPi), 0.3)
            * std::pow(3.0 * c1 * c1, -0.2);
        const double fraction = (1.0 / 9.0)
            * std::pow(ct * area / (x * x), 1.0 / 3.0)
            * (first - second) * (first - second);
        return std::clamp(fraction, 0.0, 0.95);
    }

    StateWork state_work(
        const std::vector<Point>& layout,
        const WindState& state
    ) const {
        StateWork result;
        const double radians = state.direction_degrees * kPi / 180.0;
        const double down_x = -std::sin(radians);
        const double down_y = -std::cos(radians);
        const double cross_x = -down_y;
        const double cross_y = down_x;
        std::array<double, 20> down{};
        std::array<double, 20> cross{};
        std::array<double, 20> effective{};
        std::array<int, 20> order{};
        for (int turbine = 0; turbine < turbines; ++turbine) {
            const Point& point = layout[static_cast<std::size_t>(turbine)];
            down[turbine] = point.x_m * down_x + point.y_m * down_y;
            cross[turbine] = point.x_m * cross_x + point.y_m * cross_y;
            order[turbine] = turbine;
            effective[turbine] = state.speed_mps;
        }
        std::stable_sort(order.begin(), order.begin() + turbines,
            [&](const int a, const int b) {
                return down[a] == down[b] ? a < b : down[a] < down[b];
            });

        constexpr std::array<double, 9> sample_x{
            0.0, 0.35, -0.35, 0.0, 0.0, 0.65, -0.65, 0.0, 0.0};
        constexpr std::array<double, 9> sample_y{
            0.0, 0.0, 0.0, 0.35, -0.35, 0.0, 0.0, 0.65, -0.65};
        for (int position = 0; position < turbines; ++position) {
            const int target = order[position];
            double rotor_mean_deficit = 0.0;
            for (int sample = 0; sample < 9; ++sample) {
                double linear_deficit = 0.0;
                const double rotor_offset = sample_x[sample]
                    * 0.5 * rotor_diameter_m;
                for (int upstream_position = 0;
                     upstream_position < position; ++upstream_position) {
                    const int upstream = order[upstream_position];
                    const double dx = down[target] - down[upstream];
                    const double radial = std::abs(
                        cross[target] + rotor_offset - cross[upstream]);
                    linear_deficit += effective[upstream] * larsen_fraction(
                        dx, radial, thrust_coefficient(effective[upstream]),
                        state.turbulence
                    );
                }
                rotor_mean_deficit += linear_deficit / 9.0;
            }
            effective[target] = std::max(
                0.0, state.speed_mps - wake_scale * rotor_mean_deficit);
            result.wake_activity[target] = state.speed_mps > 0.0
                ? std::clamp((state.speed_mps - effective[target])
                             / state.speed_mps, 0.0, 1.5)
                : 0.0;
        }

        // Paper SGA duplicate rule: exactly coincident turbines retain all
        // costs, while one duplicate turbine is treated as non-operating.
        std::array<bool, 20> operating{};
        operating.fill(true);
        for (int i = 0; i < turbines; ++i) {
            for (int j = 0; j < i; ++j) {
                if (squared_distance(layout[i], layout[j]) < 1.0e-16) {
                    operating[i] = false;
                    break;
                }
            }
        }
        for (int turbine = 0; turbine < turbines; ++turbine) {
            result.gross_power_mw += power_mw(state.speed_mps);
            if (operating[turbine]) result.net_power_mw += power_mw(effective[turbine]);
        }
        return result;
    }

    double efficiency_for_scale(const std::vector<Point>& layout) const {
        double gross = 0.0;
        double net = 0.0;
        for (const WindState& state : fine_states) {
            const StateWork work = state_work(layout, state);
            gross += state.probability * work.gross_power_mw;
            net += state.probability * work.net_power_mw;
        }
        return gross > 0.0 ? net / gross : 0.0;
    }

    void calibrate_wake_scale() {
        if (!(target_baseline_efficiency > 0.0)) return;
        double low = 0.01;
        double high = 5.0;
        for (int iteration = 0; iteration < 70; ++iteration) {
            wake_scale = 0.5 * (low + high);
            if (efficiency_for_scale(baseline) > target_baseline_efficiency) {
                low = wake_scale;
            } else {
                high = wake_scale;
            }
        }
        wake_scale = 0.5 * (low + high);
    }

    double water_depth_m(const Point& point) const {
        if (!offshore) return 8.0;
        if (case_id == CaseId::fictitious_2x3) {
            const std::array<Point, 4> deep{{{600,1020},{600,180},{230,600},{970,600}}};
            double depth = 4.0;
            for (const Point& center : deep) {
                const double r2 = squared_distance(point, center);
                depth += 4.0 * std::exp(-r2 / (2.0 * 150.0 * 150.0));
            }
            return std::clamp(depth, 4.0, 20.0);
        }
        const Point center = polygon_centroid(polygon);
        const double nx = (point.x_m - center.x_m) / 950.0;
        const double ny = (point.y_m - center.y_m) / 2050.0;
        return std::clamp(4.0 + 14.0 * (0.25 * nx * nx + ny * ny), 4.0, 20.0);
    }

    double cable_length(const std::vector<Point>& layout) const {
        const std::size_t count = layout.size();
        std::vector<double> best(count, std::numeric_limits<double>::infinity());
        std::vector<bool> used(count, false);
        best[0] = 0.0;
        double total = 0.0;
        for (std::size_t step = 0; step < count; ++step) {
            std::size_t selected = count;
            for (std::size_t index = 0; index < count; ++index) {
                if (!used[index] && (selected == count || best[index] < best[selected])) {
                    selected = index;
                }
            }
            used[selected] = true;
            total += best[selected];
            for (std::size_t index = 0; index < count; ++index) {
                if (!used[index]) {
                    best[index] = std::min(best[index],
                                           distance(layout[selected], layout[index]));
                }
            }
        }
        return total;
    }

    Evaluation reduce(
        const std::vector<Point>& layout,
        const EvaluationSettings& settings,
        const std::vector<WindState>& states,
        const std::vector<StateWork>& work
    ) const {
        Evaluation result;
        std::array<std::array<double, 4>, 20> load_damage{};
        constexpr std::array<double, 4> wohler{12.0, 8.0, 1.0, 4.0};
        constexpr std::array<double, 4> load_gain{1.20, 0.90, -0.25, 1.45};
        for (std::size_t index = 0; index < states.size(); ++index) {
            const double annual_hours = 8760.0 * states[index].probability;
            result.gross_aep_mwh_per_year += annual_hours * work[index].gross_power_mw;
            result.net_aep_mwh_per_year += annual_hours * work[index].net_power_mw;
            for (int turbine = 0; turbine < turbines; ++turbine) {
                for (int component = 0; component < 4; ++component) {
                    const double ratio = std::max(0.05,
                        1.0 + load_gain[component] * work[index].wake_activity[turbine]);
                    load_damage[turbine][component] += states[index].probability
                        * std::pow(ratio, wohler[component]);
                }
            }
        }
        result.energy_efficiency_percent = result.gross_aep_mwh_per_year > 0.0
            ? 100.0 * result.net_aep_mwh_per_year / result.gross_aep_mwh_per_year
            : 0.0;
        result.power_value_meur = result.net_aep_mwh_per_year
            * kLifetimeYears * kElectricityEuroPerMwh / 1.0e6;

        const double turbine_cost_meur = rated_power_mw;
        if (offshore) {
            for (const Point& point : layout) {
                result.foundation_cost_meur += 0.20 * turbine_cost_meur
                    + (water_depth_m(point) - 8.0) * 0.02 * turbine_cost_meur;
            }
        }
        result.cable_length_m = cable_length(layout);
        result.cable_cost_meur = settings.cable_scale * result.cable_length_m
            * cable_euro_per_m / 1.0e6;

        constexpr std::array<double, 4> component_fraction{0.20,0.10,0.20,0.20};
        const std::array<double, 4> replacement_fraction = offshore
            ? std::array<double,4>{0.25,0.30,0.30,0.50}
            : std::array<double,4>{0.05,0.075,0.075,0.10};
        constexpr double sigma = 0.15;
        for (int turbine = 0; turbine < turbines; ++turbine) {
            for (int component = 0; component < 4; ++component) {
                const double load_ratio = std::pow(
                    std::max(load_damage[turbine][component], 1.0e-18),
                    1.0 / wohler[component]
                );
                const double degradation = load_ratio / 2.5;
                result.degradation_cost_meur += settings.fatigue_scale
                    * component_fraction[component] * turbine_cost_meur * degradation;
                const double probability_first = 0.5 * std::erfc(
                    -(std::log(std::max(degradation, 1.0e-12))
                      - std::log(1.0) + sigma * sigma) / (sigma * std::sqrt(2.0)));
                const double probability_second = 0.5 * std::erfc(
                    -(std::log(std::max(degradation, 1.0e-12))
                      - std::log(2.0) + sigma * sigma) / (sigma * std::sqrt(2.0)));
                result.maintenance_cost_meur += settings.fatigue_scale
                    * replacement_fraction[component] * turbine_cost_meur
                    * (probability_first + probability_second);
            }
        }
        const double finance_factor = std::pow(
            1.0 + (kInterest - kInflation) / kLoanPaymentsPerYear,
            kLifetimeYears * kLoanPaymentsPerYear
        );
        result.financial_balance_meur = result.power_value_meur
            - result.degradation_cost_meur - result.maintenance_cost_meur
            - finance_factor * (result.foundation_cost_meur + result.cable_cost_meur);

        result.minimum_spacing_m = minimum_spacing(layout);
        double violation = std::max(0.0, rotor_diameter_m - result.minimum_spacing_m);
        for (const Point& point : layout) {
            violation = std::max(violation, boundary_violation(point, polygon));
        }
        result.maximum_constraint_violation_m = violation;
        result.feasible = violation <= 1.0e-7;
        return result;
    }

    Evaluation evaluate_serial(
        const std::vector<Point>& layout,
        const EvaluationSettings& settings
    ) const {
        const auto& states = settings.fidelity == Fidelity::level1_coarse
            ? coarse_states : fine_states;
        std::vector<StateWork> work(states.size());
        for (std::size_t index = 0; index < states.size(); ++index) {
            work[index] = state_work(layout, states[index]);
        }
        Evaluation result = reduce(layout, settings, states, work);
        result.requested_workers = 1;
        result.observed_workers = 1;
        return result;
    }

    Evaluation evaluate_parallel(
        const std::vector<Point>& layout,
        const EvaluationSettings& settings,
        fode::PersistentExecutor& executor
    ) const {
        const auto start = Clock::now();
        const auto& states = settings.fidelity == Fidelity::level1_coarse
            ? coarse_states : fine_states;
        std::vector<StateWork> work(states.size());
        executor.reset_work_receipt();
        executor.parallel_for(0, static_cast<int>(states.size()), [&](const int index) {
            work[static_cast<std::size_t>(index)] = state_work(
                layout, states[static_cast<std::size_t>(index)]);
        });
        Evaluation result = reduce(layout, settings, states, work);
        const auto receipt = executor.work_receipt();
        result.requested_workers = executor.thread_count();
        result.observed_workers = receipt.distinct_participants;
        result.seconds = elapsed(start);
        return result;
    }

    std::vector<Evaluation> evaluate_layouts(
        const std::vector<std::vector<Point>>& layouts,
        const EvaluationSettings& settings,
        fode::PersistentExecutor& executor
    ) const {
        std::vector<Evaluation> output(layouts.size());
        executor.parallel_for(0, static_cast<int>(layouts.size()), [&](const int index) {
            output[static_cast<std::size_t>(index)] = evaluate_serial(
                layouts[static_cast<std::size_t>(index)], settings);
        });
        return output;
    }

    std::vector<Point> repair(std::vector<Point> layout) const {
        for (Point& point : layout) point = project_inside(point, polygon);
        const Point center = polygon_centroid(polygon);
        for (int pass = 0; pass < 12; ++pass) {
            bool changed = false;
            for (int i = 0; i < turbines; ++i) {
                for (int j = i + 1; j < turbines; ++j) {
                    double dx = layout[i].x_m - layout[j].x_m;
                    double dy = layout[i].y_m - layout[j].y_m;
                    double length = std::sqrt(dx * dx + dy * dy);
                    if (length + 1.0e-9 >= rotor_diameter_m) continue;
                    if (length < 1.0e-12) {
                        dx = layout[i].x_m - center.x_m + 0.137 * (i + 1);
                        dy = layout[i].y_m - center.y_m + 0.173 * (j + 1);
                        length = std::hypot(dx, dy);
                    }
                    const double push = 0.505 * (rotor_diameter_m - length) / length;
                    layout[i].x_m += push * dx;
                    layout[i].y_m += push * dy;
                    layout[j].x_m -= push * dx;
                    layout[j].y_m -= push * dy;
                    layout[i] = project_inside(layout[i], polygon);
                    layout[j] = project_inside(layout[j], polygon);
                    changed = true;
                }
            }
            if (!changed) break;
        }
        return layout;
    }
};

namespace {

int nearest_candidate(const Problem::Data& data, const Point& point) {
    int best = 0;
    double value = squared_distance(point, data.candidates.front());
    for (int index = 1; index < static_cast<int>(data.candidates.size()); ++index) {
        const double candidate = squared_distance(
            point, data.candidates[static_cast<std::size_t>(index)]);
        if (candidate < value) {
            value = candidate;
            best = index;
        }
    }
    return best;
}

std::vector<Point> decode(
    const Problem::Data& data,
    const std::vector<int>& chromosome
) {
    std::vector<Point> result;
    result.reserve(chromosome.size());
    for (const int index : chromosome) {
        result.push_back(data.candidates[static_cast<std::size_t>(index)]);
    }
    return result;
}

double penalized(const Evaluation& evaluation) {
    return evaluation.financial_balance_meur
        - 1.0e3 * evaluation.maximum_constraint_violation_m;
}

struct SgaOutput {
    std::vector<Point> layout;
    Evaluation evaluation;
    StageReceipt receipt;
};

SgaOutput run_sga(
    const Problem::Data& data,
    fode::PersistentExecutor& executor,
    const std::uint64_t seed,
    const int generations
) {
    const auto stage_start = Clock::now();
    const fode::CounterRng rng(seed);
    const int candidate_count = static_cast<int>(data.candidates.size());
    int bits = 1;
    while ((1 << bits) < candidate_count && bits < 30) ++bits;
    std::vector<std::vector<int>> population(
        kPopulation, std::vector<int>(static_cast<std::size_t>(data.turbines)));
    for (int turbine = 0; turbine < data.turbines; ++turbine) {
        population[0][turbine] = nearest_candidate(data, data.baseline[turbine]);
    }
    for (int individual = 1; individual < kPopulation; ++individual) {
        for (int turbine = 0; turbine < data.turbines; ++turbine) {
            population[individual][turbine] = rng.integer(
                0, candidate_count, 0, 1, individual, turbine);
        }
    }
    EvaluationSettings settings;
    settings.fidelity = Fidelity::level1_coarse;
    double evaluator_seconds = 0.0;
    auto evaluate_population = [&](const std::vector<std::vector<int>>& values) {
        std::vector<std::vector<Point>> layouts;
        layouts.reserve(values.size());
        for (const auto& value : values) layouts.push_back(decode(data, value));
        const auto begin = Clock::now();
        auto result = data.evaluate_layouts(layouts, settings, executor);
        evaluator_seconds += elapsed(begin);
        return result;
    };
    std::vector<Evaluation> evaluations = evaluate_population(population);
    std::uint64_t fes = kPopulation;
    const double start_balance = evaluations[0].financial_balance_meur;

    auto select_parent = [&](const std::vector<int>& order,
                             const int generation,
                             const int child,
                             const int draw) {
        // Linear-rank selection with the report scaling value s=2.
        const double total = static_cast<double>(kPopulation * kPopulation);
        double target = rng.uniform(generation, 2, child, draw) * total;
        for (int rank = 0; rank < kPopulation; ++rank) {
            const double weight = kFitnessScaling * (kPopulation - rank) - 1.0;
            if ((target -= weight) <= 0.0) return order[rank];
        }
        return order.back();
    };

    for (int generation = 1; generation <= generations; ++generation) {
        std::vector<int> order(kPopulation);
        std::iota(order.begin(), order.end(), 0);
        std::stable_sort(order.begin(), order.end(), [&](const int a, const int b) {
            return penalized(evaluations[a]) == penalized(evaluations[b])
                ? a < b : penalized(evaluations[a]) > penalized(evaluations[b]);
        });
        std::vector<std::vector<int>> next(kPopulation);
        next[0] = population[order[0]];
        next[1] = population[order[1]];
        const int total_bits = bits * data.turbines;
        for (int child = kElites; child < kPopulation; ++child) {
            const int first = select_parent(order, generation, child, 0);
            const int second = select_parent(order, generation, child, 1);
            const bool crossover = rng.uniform(generation, 3, child) < kCrossover;
            const int cut = crossover
                ? rng.integer(1, total_bits, generation, 4, child) : total_bits;
            next[child].assign(static_cast<std::size_t>(data.turbines), 0);
            for (int turbine = 0; turbine < data.turbines; ++turbine) {
                int gene = 0;
                for (int bit = 0; bit < bits; ++bit) {
                    const int global = turbine * bits + bit;
                    const int source = global < cut ? first : second;
                    int value = (population[source][turbine] >> bit) & 1;
                    if (rng.uniform(generation, 5, child, global) < kMutation) {
                        value ^= 1;
                    }
                    gene |= value << bit;
                }
                next[child][turbine] = gene % candidate_count;
            }
        }
        population = std::move(next);
        evaluations = evaluate_population(population);
        fes += kPopulation;
    }
    int best = 0;
    for (int index = 1; index < kPopulation; ++index) {
        if (penalized(evaluations[index]) > penalized(evaluations[best])) best = index;
    }
    EvaluationSettings fine;
    fine.fidelity = Fidelity::level2_fine;
    const auto fine_start = Clock::now();
    Evaluation final = data.evaluate_parallel(decode(data, population[best]), fine, executor);
    evaluator_seconds += elapsed(fine_start);
    ++fes;
    return {decode(data, population[best]), final,
        {"sga_level1_binary", generations, fes, start_balance,
         evaluations[best].financial_balance_meur,
         evaluator_seconds, elapsed(stage_start)}};
}

struct SlpOutput {
    std::vector<Point> layout;
    Evaluation evaluation;
    StageReceipt receipt;
};

SlpOutput run_slp(
    const Problem::Data& data,
    fode::PersistentExecutor& executor,
    std::vector<Point> layout,
    const int iterations
) {
    const auto stage_start = Clock::now();
    EvaluationSettings settings;
    settings.fidelity = Fidelity::level2_fine;
    double evaluator_seconds = 0.0;
    const auto initial_start = Clock::now();
    layout = data.repair(std::move(layout));
    Evaluation current = data.evaluate_parallel(layout, settings, executor);
    evaluator_seconds += elapsed(initial_start);
    const double start_balance = current.financial_balance_meur;
    std::uint64_t fes = 1;

    double min_x = data.polygon.front().x_m;
    double max_x = min_x;
    double min_y = data.polygon.front().y_m;
    double max_y = min_y;
    for (const Point& point : data.polygon) {
        min_x = std::min(min_x, point.x_m);
        max_x = std::max(max_x, point.x_m);
        min_y = std::min(min_y, point.y_m);
        max_y = std::max(max_y, point.y_m);
    }
    const double span = std::max(max_x - min_x, max_y - min_y);
    double move_fraction = data.case_id == CaseId::stags_holt_coldham
        ? 0.00625 : 0.025;
    constexpr double minimum_move = 0.0001;

    for (int iteration = 0; iteration < iterations; ++iteration) {
        const double step = std::max(0.5, move_fraction * span);
        std::vector<std::vector<Point>> probes;
        probes.reserve(static_cast<std::size_t>(4 * data.turbines));
        for (int turbine = 0; turbine < data.turbines; ++turbine) {
            for (int axis = 0; axis < 2; ++axis) {
                for (int sign : {-1, 1}) {
                    auto probe = layout;
                    double& coordinate = axis == 0 ? probe[turbine].x_m
                                                   : probe[turbine].y_m;
                    coordinate += sign * step;
                    probes.push_back(data.repair(std::move(probe)));
                }
            }
        }
        const auto probe_start = Clock::now();
        const auto probe_values = data.evaluate_layouts(probes, settings, executor);
        evaluator_seconds += elapsed(probe_start);
        fes += probes.size();
        std::vector<double> gradient(static_cast<std::size_t>(2 * data.turbines));
        for (int variable = 0; variable < 2 * data.turbines; ++variable) {
            gradient[variable] = (penalized(probe_values[2 * variable + 1])
                                - penalized(probe_values[2 * variable]))
                                / (2.0 * step);
        }
        std::vector<std::vector<Point>> trials;
        for (const double fraction : {1.0, 0.5, 0.25, 0.125}) {
            auto trial = layout;
            for (int turbine = 0; turbine < data.turbines; ++turbine) {
                trial[turbine].x_m += fraction * step
                    * (gradient[2 * turbine] >= 0.0 ? 1.0 : -1.0);
                trial[turbine].y_m += fraction * step
                    * (gradient[2 * turbine + 1] >= 0.0 ? 1.0 : -1.0);
            }
            trials.push_back(data.repair(std::move(trial)));
        }
        const auto trial_start = Clock::now();
        const auto trial_values = data.evaluate_layouts(trials, settings, executor);
        evaluator_seconds += elapsed(trial_start);
        fes += trials.size();
        int best = -1;
        for (int index = 0; index < static_cast<int>(trials.size()); ++index) {
            if (trial_values[index].feasible
                && trial_values[index].financial_balance_meur
                    > current.financial_balance_meur + 1.0e-12
                && (best < 0 || trial_values[index].financial_balance_meur
                    > trial_values[best].financial_balance_meur)) {
                best = index;
            }
        }
        if (best >= 0) {
            layout = std::move(trials[static_cast<std::size_t>(best)]);
            current = trial_values[static_cast<std::size_t>(best)];
            move_fraction = std::min(0.025, 1.15 * move_fraction);
        } else {
            move_fraction = std::max(minimum_move, 0.5 * move_fraction);
        }
    }
    return {layout, current,
        {"slp_level2_move_limited", iterations, fes, start_balance,
         current.financial_balance_meur, evaluator_seconds, elapsed(stage_start)}};
}

}  // namespace

Problem::Problem(const CaseId case_id) : data_(std::make_unique<Data>(case_id)) {}
Problem::~Problem() = default;
Problem::Problem(Problem&&) noexcept = default;
Problem& Problem::operator=(Problem&&) noexcept = default;
CaseId Problem::case_id() const noexcept { return data_->case_id; }
const std::string& Problem::semantic_id() const noexcept { return data_->semantic_id; }
int Problem::turbine_count() const noexcept { return data_->turbines; }
int Problem::fine_wind_state_count() const noexcept {
    return static_cast<int>(data_->fine_states.size());
}
int Problem::candidate_count() const noexcept {
    return static_cast<int>(data_->candidates.size());
}
double Problem::rotor_diameter_m() const noexcept { return data_->rotor_diameter_m; }
const std::vector<Point>& Problem::baseline_layout() const noexcept { return data_->baseline; }
const std::vector<Point>& Problem::polygon() const noexcept { return data_->polygon; }

Evaluation Problem::evaluate(
    const std::vector<Point>& layout,
    const EvaluationSettings& settings,
    fode::PersistentExecutor& executor
) const {
    if (layout.size() != static_cast<std::size_t>(data_->turbines)) {
        throw std::invalid_argument("T58 layout turbine count mismatch");
    }
    return data_->evaluate_parallel(layout, settings, executor);
}

const char* case_name(const CaseId value) noexcept {
    switch (value) {
        case CaseId::fictitious_2x3: return "fictitious_2x3_offshore";
        case CaseId::stags_holt_coldham: return "stags_holt_coldham";
        case CaseId::middelgrunden: return "middelgrunden";
    }
    return "unknown";
}

const char* method_name(const Method value) noexcept {
    switch (value) {
        case Method::slp_only: return "slp_only";
        case Method::sga_only: return "sga_only";
        case Method::sga_slp: return "sga_slp_multifidelity";
    }
    return "unknown";
}

const char* fidelity_name(const Fidelity value) noexcept {
    return value == Fidelity::level1_coarse ? "level1_coarse" : "level2_fine";
}

RunResult run(const Problem& problem, const RunConfig& config) {
    if (config.workers <= 0) throw std::invalid_argument("T58 workers must be positive");
    if (problem.case_id() != CaseId::fictitious_2x3
        && config.method != Method::sga_slp) {
        throw std::invalid_argument("T58 real-site paper roles use SGA+SLP");
    }
    const auto started = Clock::now();
    fode::PersistentExecutor executor(config.workers);
    executor.reset_work_receipt();
    RunResult result;
    result.case_id = problem.case_id();
    result.method = config.method;
    result.seed = config.seed;
    result.requested_workers = config.workers;
    result.sga_population = config.method == Method::slp_only ? 0 : kPopulation;
    result.method_semantic_id = config.method == Method::slp_only
        ? "t58_slp_declared_v1"
        : config.method == Method::sga_only
            ? "t58_sga_declared_v1"
            : "t58_sga_slp_multifidelity_declared_v1";
    EvaluationSettings fine;
    result.initial_evaluation = problem.data_->evaluate_parallel(
        problem.data_->baseline, fine, executor);
    result.evaluator_seconds += result.initial_evaluation.seconds;

    const int default_sga = problem.case_id() == CaseId::fictitious_2x3 ? 150 : 1000;
    const int default_slp = problem.case_id() == CaseId::fictitious_2x3 ? 50
        : problem.case_id() == CaseId::stags_holt_coldham ? 30 : 20;
    const int sga_generations = config.sga_generations_override > 0
        ? config.sga_generations_override : (config.smoke ? 3 : default_sga);
    const int slp_iterations = config.slp_iterations_override > 0
        ? config.slp_iterations_override : (config.smoke ? 2 : default_slp);

    std::vector<Point> layout = problem.data_->baseline;
    Evaluation final = result.initial_evaluation;
    if (config.method != Method::slp_only) {
        SgaOutput output = run_sga(*problem.data_, executor, config.seed, sga_generations);
        layout = std::move(output.layout);
        final = output.evaluation;
        result.sga_generations = sga_generations;
        result.physical_fes += output.receipt.physical_fes;
        result.evaluator_seconds += output.receipt.evaluator_seconds;
        result.stages.push_back(output.receipt);
    }
    if (config.method != Method::sga_only) {
        SlpOutput output = run_slp(*problem.data_, executor, std::move(layout), slp_iterations);
        layout = std::move(output.layout);
        final = output.evaluation;
        result.slp_iterations = slp_iterations;
        result.physical_fes += output.receipt.physical_fes;
        result.evaluator_seconds += output.receipt.evaluator_seconds;
        result.stages.push_back(output.receipt);
    }
    result.final_layout = std::move(layout);
    result.final_evaluation = final;
    result.end_to_end_seconds = elapsed(started);
    result.algorithm_seconds = std::max(0.0,
        result.end_to_end_seconds - result.evaluator_seconds);
    result.observed_workers = executor.work_receipt().distinct_participants;
    result.scientific_hash = scientific_hash(
        result.final_layout, result.final_evaluation, result.physical_fes);
    return result;
}

}  // namespace core99::t58
