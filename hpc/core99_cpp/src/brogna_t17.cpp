/*
WFLOP IMPLEMENTATION FACT DECLARATION
Implementation unit: T17 pure-C++ complex-terrain evaluator and double-stage RS
Paper title/DOI: A New Wake Model and Comparison of Eight Algorithms for
Layout Optimization of Wind Farms in Complex Terrain;
10.1016/j.apenergy.2019.114189
Public source: no author code/data; PyWake commit
5b07481ec9b3633a74844651648f266ba82a8b32 (MIT) supplies the declared open
WAsP proxy and an independent same-lineage streamline implementation
Missing/conflicts/reconstruction: include/core99/brogna_t17.hpp
Semantic IDs: t17_brogna_private_site_open_flow_proxy_v1;
t17_double_stage_rs_declared_reconstruction_v1
Controlling contract: shared/contracts/core99_t17_brogna_2020.json
HPC design: immutable terrain fields are stored sector-major; one persistent
team evaluates twelve independent wind directions; source-target screening,
streamline integration, rotor quadrature, and fixed-order reductions are
performed without per-evaluation thread creation
Claim boundary: declared academic reproduction, not author-data replay
Last evidence-audit date: 2026-07-31
END WFLOP IMPLEMENTATION FACT DECLARATION
*/
#include "core99/brogna_t17.hpp"

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
#include <random>
#include <stdexcept>
#include <utility>

namespace core99::t17 {
namespace {

using Clock = std::chrono::steady_clock;
constexpr int kTurbines = 25;
constexpr int kSectors = 12;
constexpr double kWidthM = 6000.0;
constexpr double kHeightM = 4000.0;
constexpr double kDiameterM = 93.0;
constexpr double kHubHeightM = 67.0;
constexpr double kMinimumSpacingM = 4.0 * kDiameterM;
constexpr double kReferenceSpeedMps = 7.0;
constexpr double kStreamlineStepM = 25.0;
constexpr double kStreamlineMaximumM = 40.0 * kDiameterM;
constexpr double kStreamlineMinimumM = 2.0 * kDiameterM;
constexpr double kPi = std::numbers::pi_v<double>;

double radians(const double degrees) {
    return degrees * kPi / 180.0;
}

double thrust_coefficient(const double speed_mps) {
    if (speed_mps < 3.0 || speed_mps >= 25.0) return 0.0;
    if (speed_mps < 7.0) {
        return 0.84 - (0.84 - 0.747) * (speed_mps - 3.0) / 4.0;
    }
    if (speed_mps < 12.0) {
        return 0.747 - (0.747 - 0.30) * (speed_mps - 7.0) / 5.0;
    }
    return 0.30 - 0.20 * (speed_mps - 12.0) / 13.0;
}

std::uint64_t hash_result(
    const std::vector<Point>& layout,
    const Evaluation& evaluation
) {
    std::uint64_t hash = 1469598103934665603ULL;
    for (const Point& point : layout) {
        hash ^= std::bit_cast<std::uint64_t>(point.x_m);
        hash *= 1099511628211ULL;
        hash ^= std::bit_cast<std::uint64_t>(point.y_m);
        hash *= 1099511628211ULL;
    }
    hash ^= std::bit_cast<std::uint64_t>(evaluation.objective);
    hash *= 1099511628211ULL;
    return hash;
}

bool better(const Evaluation& left, const Evaluation& right) {
    const bool left_feasible = left.constraint_violation_m <= 1.0e-12;
    const bool right_feasible = right.constraint_violation_m <= 1.0e-12;
    if (left_feasible != right_feasible) return left_feasible;
    if (left.constraint_violation_m != right.constraint_violation_m) {
        return left.constraint_violation_m < right.constraint_violation_m;
    }
    return left.objective > right.objective;
}

template <typename T>
T read_scalar(std::ifstream& stream) {
    T value{};
    stream.read(reinterpret_cast<char*>(&value), sizeof(T));
    if (!stream) throw std::runtime_error("truncated T17 proxy");
    return value;
}

std::vector<float> read_array(std::ifstream& stream, const std::size_t count) {
    std::vector<float> values(count);
    stream.read(
        reinterpret_cast<char*>(values.data()),
        static_cast<std::streamsize>(count * sizeof(float))
    );
    if (!stream) throw std::runtime_error("truncated T17 proxy array");
    return values;
}

}  // namespace

struct Problem::FlowData {
    std::uint32_t nx = 0;
    std::uint32_t ny = 0;
    std::uint32_t sectors = 0;
    float xmin = 0.0F;
    float xmax = 0.0F;
    float ymin = 0.0F;
    float ymax = 0.0F;
    std::vector<float> elevation;
    std::vector<float> speedup;
    std::vector<float> turn_degrees;
    std::vector<float> inclination_degrees;
    std::vector<float> frequency;

    [[nodiscard]] std::size_t plane_size() const {
        return static_cast<std::size_t>(nx) * static_cast<std::size_t>(ny);
    }

    [[nodiscard]] double sample(
        const std::vector<float>& values,
        const int sector,
        const double x_m,
        const double y_m,
        const bool sector_major
    ) const {
        const double gx = std::clamp(
            (x_m - static_cast<double>(xmin))
                / (static_cast<double>(xmax) - static_cast<double>(xmin))
                * static_cast<double>(nx - 1U),
            0.0,
            static_cast<double>(nx - 1U)
        );
        const double gy = std::clamp(
            (y_m - static_cast<double>(ymin))
                / (static_cast<double>(ymax) - static_cast<double>(ymin))
                * static_cast<double>(ny - 1U),
            0.0,
            static_cast<double>(ny - 1U)
        );
        const std::uint32_t ix0 = static_cast<std::uint32_t>(gx);
        const std::uint32_t iy0 = static_cast<std::uint32_t>(gy);
        const std::uint32_t ix1 = std::min(ix0 + 1U, nx - 1U);
        const std::uint32_t iy1 = std::min(iy0 + 1U, ny - 1U);
        const double tx = gx - static_cast<double>(ix0);
        const double ty = gy - static_cast<double>(iy0);
        const std::size_t offset = sector_major
            ? static_cast<std::size_t>(sector) * plane_size()
            : 0U;
        const auto at = [&](const std::uint32_t ix, const std::uint32_t iy) {
            return static_cast<double>(
                values[offset + static_cast<std::size_t>(iy) * nx + ix]
            );
        };
        const double bottom = std::lerp(at(ix0, iy0), at(ix1, iy0), tx);
        const double top = std::lerp(at(ix0, iy1), at(ix1, iy1), tx);
        return std::lerp(bottom, top, ty);
    }
};

Problem::Problem(const std::string& proxy_path)
    : semantic_id_("t17_brogna_private_site_open_flow_proxy_v1") {
    std::ifstream stream(proxy_path, std::ios::binary);
    if (!stream) {
        throw std::runtime_error("cannot open T17 proxy: " + proxy_path);
    }
    std::array<char, 8> magic{};
    stream.read(magic.data(), static_cast<std::streamsize>(magic.size()));
    const std::array<char, 8> expected{'T', '1', '7', 'P', 'X', 'Y', '1', '\0'};
    if (!stream || magic != expected) {
        throw std::runtime_error("invalid T17 proxy magic");
    }
    auto data = std::make_shared<FlowData>();
    data->nx = read_scalar<std::uint32_t>(stream);
    data->ny = read_scalar<std::uint32_t>(stream);
    data->sectors = read_scalar<std::uint32_t>(stream);
    data->xmin = read_scalar<float>(stream);
    data->xmax = read_scalar<float>(stream);
    data->ymin = read_scalar<float>(stream);
    data->ymax = read_scalar<float>(stream);
    if (data->nx < 2U || data->ny < 2U || data->sectors != kSectors
        || data->xmin != 0.0F || data->xmax != static_cast<float>(kWidthM)
        || data->ymin != 0.0F || data->ymax != static_cast<float>(kHeightM)) {
        throw std::runtime_error("T17 proxy dimensions/extent mismatch");
    }
    const std::size_t plane = data->plane_size();
    data->elevation = read_array(stream, plane);
    data->speedup.reserve(kSectors * plane);
    data->turn_degrees.reserve(kSectors * plane);
    data->inclination_degrees.reserve(kSectors * plane);
    data->frequency.reserve(kSectors * plane);
    for (int sector = 0; sector < kSectors; ++sector) {
        for (auto* destination : {
                 &data->speedup,
                 &data->turn_degrees,
                 &data->inclination_degrees,
                 &data->frequency,
             }) {
            auto values = read_array(stream, plane);
            destination->insert(
                destination->end(), values.begin(), values.end()
            );
        }
    }
    char excess = '\0';
    if (stream.read(&excess, 1)) {
        throw std::runtime_error("unexpected trailing bytes in T17 proxy");
    }
    data_ = std::move(data);
}

const std::string& Problem::semantic_id() const noexcept {
    return semantic_id_;
}

double Problem::constraint_violation(const std::vector<Point>& layout) const {
    double violation = std::abs(
        static_cast<double>(layout.size()) - static_cast<double>(kTurbines)
    ) * kMinimumSpacingM;
    for (const Point& point : layout) {
        violation = std::max(violation, std::max({
            -point.x_m,
            point.x_m - kWidthM,
            -point.y_m,
            point.y_m - kHeightM,
            0.0,
        }));
    }
    for (std::size_t left = 0; left < layout.size(); ++left) {
        for (std::size_t right = left + 1U; right < layout.size(); ++right) {
            const double distance = std::hypot(
                layout[left].x_m - layout[right].x_m,
                layout[left].y_m - layout[right].y_m
            );
            violation = std::max(
                violation, std::max(0.0, kMinimumSpacingM - distance)
            );
        }
    }
    return violation;
}

double gaussian_deficit_ratio(
    const double streamwise_diameters,
    const double radial_diameters,
    const double thrust_coefficient_value
) {
    if (streamwise_diameters < 2.0 || streamwise_diameters > 40.0
        || thrust_coefficient_value <= 0.0) {
        return 0.0;
    }
    const double root = std::sqrt(
        std::max(1.0e-12, 1.0 - thrust_coefficient_value)
    );
    const double epsilon = 0.2 * std::sqrt(
        0.5 * (1.0 + root) / root
    );
    const double width = 0.042 * streamwise_diameters + epsilon;
    const double radicand = std::max(
        0.0,
        1.0 - thrust_coefficient_value / (8.0 * width * width)
    );
    const double centre = 1.0 - std::sqrt(radicand);
    const double gaussian = std::exp(
        -radial_diameters * radial_diameters / (2.0 * width * width)
    );
    const double deficit = centre * gaussian;
    return deficit >= 0.01 ? deficit : 0.0;
}

Evaluation Problem::evaluate(
    const std::vector<Point>& layout,
    const bool include_wakes,
    fode::PersistentExecutor& executor
) const {
    Evaluation evaluation;
    evaluation.includes_wakes = include_wakes;
    evaluation.constraint_violation_m = constraint_violation(layout);
    if (layout.size() != kTurbines) return evaluation;

    std::array<double, kTurbines * kSectors> speeds{};
    std::array<double, kTurbines * kSectors> frequencies{};
    std::array<double, kTurbines> frequency_sums{};
    for (int turbine = 0; turbine < kTurbines; ++turbine) {
        for (int sector = 0; sector < kSectors; ++sector) {
            const std::size_t index =
                static_cast<std::size_t>(turbine * kSectors + sector);
            speeds[index] = kReferenceSpeedMps * std::max(
                0.0,
                data_->sample(
                    data_->speedup, sector,
                    layout[turbine].x_m, layout[turbine].y_m, true
                )
            );
            frequencies[index] = std::max(
                0.0,
                data_->sample(
                    data_->frequency, sector,
                    layout[turbine].x_m, layout[turbine].y_m, true
                )
            );
            frequency_sums[turbine] += frequencies[index];
        }
    }

    std::array<double, kSectors> sector_objectives{};
    executor.parallel_for(0, kSectors, [&](const int sector) {
        struct StreamNode {
            double x_m;
            double y_m;
            double z_m;
            double distance_m;
        };
        constexpr int maximum_streamline_nodes =
            static_cast<int>(kStreamlineMaximumM / kStreamlineStepM) + 2;
        std::array<
            std::array<StreamNode, maximum_streamline_nodes>,
            kTurbines
        > streamlines{};
        std::array<int, kTurbines> streamline_lengths{};
        double sector_sum = 0.0;
        const double base_direction = 30.0 * static_cast<double>(sector);
        if (include_wakes) {
            for (int source = 0; source < kTurbines; ++source) {
                double x = layout[source].x_m;
                double y = layout[source].y_m;
                double z = data_->sample(
                    data_->elevation, 0, x, y, false
                ) + kHubHeightM;
                double distance = 0.0;
                while (distance <= kStreamlineMaximumM) {
                    const int node = streamline_lengths[source]++;
                    streamlines[source][node] = {x, y, z, distance};
                    const double turn = data_->sample(
                        data_->turn_degrees, sector, x, y, true
                    );
                    const double inclination = data_->sample(
                        data_->inclination_degrees, sector, x, y, true
                    );
                    const double azimuth = radians(base_direction + turn);
                    const double tilt = radians(inclination);
                    x += kStreamlineStepM * std::cos(tilt) * std::cos(azimuth);
                    y += kStreamlineStepM * std::cos(tilt) * std::sin(azimuth);
                    z += kStreamlineStepM * std::sin(tilt);
                    distance += kStreamlineStepM;
                    if (x < 0.0 || x > kWidthM || y < 0.0 || y > kHeightM) {
                        break;
                    }
                }
            }
        }
        for (int target = 0; target < kTurbines; ++target) {
            const std::size_t target_index =
                static_cast<std::size_t>(target * kSectors + sector);
            double effective_speed = speeds[target_index];
            if (include_wakes) {
                double squared_absolute_deficits = 0.0;
                const double target_hub_z = data_->sample(
                    data_->elevation, 0,
                    layout[target].x_m, layout[target].y_m, false
                ) + kHubHeightM;
                for (int source = 0; source < kTurbines; ++source) {
                    if (source == target) continue;
                    const std::size_t source_index =
                        static_cast<std::size_t>(source * kSectors + sector);
                    double closest_radial = std::numeric_limits<double>::max();
                    double closest_streamwise = 0.0;
                    for (
                        int node = 0;
                        node < streamline_lengths[source];
                        ++node
                    ) {
                        const StreamNode& point = streamlines[source][node];
                        const double radial = std::hypot(
                            std::hypot(
                                point.x_m - layout[target].x_m,
                                point.y_m - layout[target].y_m
                            ),
                            point.z_m - target_hub_z
                        );
                        if (radial < closest_radial) {
                            closest_radial = radial;
                            closest_streamwise = point.distance_m;
                        }
                    }
                    if (closest_streamwise < kStreamlineMinimumM
                        || closest_streamwise > kStreamlineMaximumM
                        || closest_radial > 4.0 * kDiameterM) {
                        continue;
                    }
                    const double ct = thrust_coefficient(speeds[source_index]);
                    double rotor_mean_deficit = 0.0;
                    constexpr int radial_bins = 4;
                    constexpr int angular_bins = 16;
                    for (int radial_bin = 0; radial_bin < radial_bins; ++radial_bin) {
                        const double radius_diameters =
                            0.5 * std::sqrt(
                                (static_cast<double>(radial_bin) + 0.5)
                                / static_cast<double>(radial_bins)
                            );
                        for (int angle = 0; angle < angular_bins; ++angle) {
                            const double rotor_radius = radius_diameters;
                            const double rotor_angle =
                                2.0 * kPi
                                * (static_cast<double>(angle) + 0.5)
                                / static_cast<double>(angular_bins);
                            const double centre_radial =
                                closest_radial / kDiameterM;
                            const double sample_radial = std::sqrt(
                                centre_radial * centre_radial
                                + rotor_radius * rotor_radius
                                + 2.0 * centre_radial * rotor_radius
                                    * std::cos(rotor_angle)
                            );
                            rotor_mean_deficit += gaussian_deficit_ratio(
                                closest_streamwise / kDiameterM,
                                sample_radial,
                                ct
                            );
                        }
                    }
                    rotor_mean_deficit /=
                        static_cast<double>(radial_bins * angular_bins);
                    const double absolute_deficit =
                        rotor_mean_deficit * speeds[source_index];
                    squared_absolute_deficits +=
                        absolute_deficit * absolute_deficit;
                }
                effective_speed = std::max(
                    0.0,
                    effective_speed - std::sqrt(squared_absolute_deficits)
                );
            }
            const double normalized_frequency = frequency_sums[target] > 0.0
                ? frequencies[target_index] / frequency_sums[target]
                : 1.0 / static_cast<double>(kSectors);
            sector_sum += normalized_frequency
                * effective_speed / kReferenceSpeedMps;
        }
        sector_objectives[sector] = sector_sum;
    });
    for (const double value : sector_objectives) {
        evaluation.objective += value;
    }
    return evaluation;
}

std::vector<Point> paper_figure_2_layout() {
    constexpr std::array<std::array<double, 2>, kTurbines> in_diameters{{
        {4,17},{8,19},{9,10},{14,22},{15,12},{21,21},{22,11},{25,15},{28,22},
        {32,15},{34,23},{38,11},{42,13},{42,5},{43,9},{45,25},{47,4},{49,11},
        {50,31},{53,10},{54,21},{56,3},{57,33},{58,14},{60,24},
    }};
    std::vector<Point> layout;
    layout.reserve(kTurbines);
    for (const auto& point : in_diameters) {
        layout.push_back({point[0] * kDiameterM, point[1] * kDiameterM});
    }
    return layout;
}

RunResult run_double_stage_rs(
    const Problem& problem,
    const std::uint64_t seed,
    const int workers,
    const SearchConfig& config
) {
    if (workers < 1) throw std::invalid_argument("T17 workers must be positive");
    if (config.stage2_fes < 1U) {
        throw std::invalid_argument("T17 stage2_fes must include initial wake evaluation");
    }
    const auto start = Clock::now();
    std::mt19937_64 random(seed);
    std::uniform_real_distribution<double> x_distribution(0.0, kWidthM);
    std::uniform_real_distribution<double> y_distribution(0.0, kHeightM);
    std::uniform_real_distribution<double> step_distribution(
        0.0, std::max(kWidthM, kHeightM)
    );
    std::uniform_int_distribution<int> turbine_distribution(0, kTurbines - 1);
    std::uniform_real_distribution<double> angle_distribution(0.0, 2.0 * kPi);
    fode::PersistentExecutor executor(workers);
    executor.reset_work_receipt();

    std::vector<Point> layout = paper_figure_2_layout();
    if (config.random_initial_layout) {
        layout.clear();
        layout.reserve(kTurbines);
        for (int turbine = 0; turbine < kTurbines; ++turbine) {
            bool placed = false;
            for (int attempt = 0; attempt < 100000 && !placed; ++attempt) {
                const Point candidate{x_distribution(random), y_distribution(random)};
                placed = true;
                for (const Point& existing : layout) {
                    if (std::hypot(
                            candidate.x_m - existing.x_m,
                            candidate.y_m - existing.y_m
                        ) < kMinimumSpacingM) {
                        placed = false;
                        break;
                    }
                }
                if (placed) layout.push_back(candidate);
            }
            if (!placed) {
                throw std::runtime_error("cannot construct T17 feasible random layout");
            }
        }
    }
    const std::vector<Point> initial_layout = layout;
    double evaluator_seconds = 0.0;
    const auto evaluate_timed = [&](const std::vector<Point>& candidate, bool wakes) {
        const auto evaluator_start = Clock::now();
        const Evaluation value = problem.evaluate(candidate, wakes, executor);
        evaluator_seconds += std::chrono::duration<double>(
            Clock::now() - evaluator_start
        ).count();
        return value;
    };

    RunResult result;
    result.problem_semantic_id = problem.semantic_id();
    result.method_semantic_id =
        "t17_double_stage_rs_declared_reconstruction_v1";
    result.seed = seed;
    result.requested_workers = workers;
    result.initial_wake_evaluation = evaluate_timed(initial_layout, true);
    std::uint64_t stage2_used = 1U;

    Evaluation current;
    auto optimize_stage = [&](
        const bool wakes,
        const std::uint64_t budget,
        std::uint64_t& used,
        const bool needs_current_evaluation
    ) {
        if (needs_current_evaluation && used < budget) {
            current = evaluate_timed(layout, wakes);
            ++used;
        }
        int remembered_turbine = -1;
        double remembered_angle = 0.0;
        bool repeat_accepted_direction = false;
        while (used < budget) {
            const int turbine = repeat_accepted_direction
                ? remembered_turbine
                : turbine_distribution(random);
            const double angle = repeat_accepted_direction
                ? remembered_angle
                : angle_distribution(random);
            std::vector<Point> candidate = layout;
            const double step = step_distribution(random);
            candidate[turbine].x_m += step * std::cos(angle);
            candidate[turbine].y_m += step * std::sin(angle);
            if (problem.constraint_violation(candidate) > 1.0e-12) {
                repeat_accepted_direction = false;
                continue;
            }
            const Evaluation candidate_evaluation =
                evaluate_timed(candidate, wakes);
            ++used;
            if (better(candidate_evaluation, current)) {
                layout = std::move(candidate);
                current = candidate_evaluation;
                remembered_turbine = turbine;
                remembered_angle = angle;
                repeat_accepted_direction = true;
            } else {
                repeat_accepted_direction = false;
            }
        }
    };

    std::uint64_t stage1_used = 0U;
    if (config.stage1_fes > 0U) {
        optimize_stage(false, config.stage1_fes, stage1_used, true);
        result.stage1_evaluation = current;
    } else {
        result.stage1_evaluation = result.initial_wake_evaluation;
        current = result.initial_wake_evaluation;
    }
    optimize_stage(
        true,
        config.stage2_fes,
        stage2_used,
        config.stage1_fes > 0U
    );

    const auto end = Clock::now();
    const auto receipt = executor.work_receipt();
    result.stage1_physical_fes = stage1_used;
    result.stage2_physical_fes = stage2_used;
    result.physical_fes = stage1_used + stage2_used;
    result.observed_workers = receipt.distinct_participants;
    result.evaluator_seconds = evaluator_seconds;
    result.end_to_end_seconds =
        std::chrono::duration<double>(end - start).count();
    result.algorithm_seconds = std::max(
        0.0, result.end_to_end_seconds - evaluator_seconds
    );
    result.final_evaluation = current;
    result.final_layout = std::move(layout);
    result.scientific_hash = hash_result(
        result.final_layout, result.final_evaluation
    );
    return result;
}

}  // namespace core99::t17
