/*
WFLOP IMPLEMENTATION FACT DECLARATION
Implementation unit: T12 WindFLO evaluator and four paper optimizers
Paper DOI: 10.1016/j.renene.2018.03.052
Public source: https://github.com/d9w/WindFLO revision
9e85a67bb2ca019768ea51dd0b634a46c8406ba2, MIT license
Provided/missing/conflicting facts and Reconstruction decisions:
include/core99/windflo_t12.hpp
Method/problem semantic IDs: t12_four_competition_methods_v1;
t12_windflo_2015_five_scenarios_v1
Controlling contract: shared/contracts/core99_t12_windflo_2015.json
Independent oracle: scripts/validate_core99_t12.py
HPC design: persistent workers evaluate independent candidate layouts; a
single coarse layout evaluates its 24 wind directions in parallel; search
state and FES are committed in deterministic paper order
Claim boundary: academic declared reproduction, not author-exact RNG replay
Last evidence-audit date: 2026-07-31
END WFLOP IMPLEMENTATION FACT DECLARATION
*/
#include "core99/windflo_t12.hpp"

#include "core99/t12_windflo_data.hpp"
#include "fode/rng.hpp"

#include <algorithm>
#include <array>
#include <bit>
#include <chrono>
#include <cmath>
#include <cstddef>
#include <deque>
#include <limits>
#include <map>
#include <numbers>
#include <numeric>
#include <stdexcept>
#include <unordered_map>
#include <utility>
#include <vector>

namespace core99::t12 {
namespace {

using Clock = std::chrono::steady_clock;
constexpr double kRadius = 38.5;
constexpr double kMinimumSpacing = 308.0;
constexpr double kInvalid = std::numeric_limits<double>::infinity();
constexpr double kPi = std::numbers::pi;

double seconds_since(const Clock::time_point start) {
    return std::chrono::duration<double>(Clock::now() - start).count();
}

double square(double value) noexcept {
    return value * value;
}

double distance(const Point& left, const Point& right) noexcept {
    return std::hypot(left.x - right.x, left.y - right.y);
}

double weibull_cdf(double value, double scale, double shape) noexcept {
    if (value <= 0.0 || scale <= 0.0 || shape <= 0.0) {
        return 0.0;
    }
    return 1.0 - std::exp(-std::pow(value / scale, shape));
}

double power_kw(double speed) noexcept {
    if (speed < 3.5) {
        return 0.0;
    }
    if (speed <= 14.0) {
        return 140.86 * speed - 500.0;
    }
    if (speed < 20.0) {
        return 1500.0;
    }
    return 0.0;
}

std::pair<std::size_t, std::size_t> obstacle_range(int scenario) {
    return {
        data::kObstacleOffsets[static_cast<std::size_t>(scenario)],
        data::kObstacleOffsets[static_cast<std::size_t>(scenario + 1)]
    };
}

double violation_serial(
    const Problem& problem,
    const std::vector<Point>& layout
) {
    if (layout.empty()) {
        return 1.0;
    }
    double violation = 0.0;
    for (const Point& point : layout) {
        if (point.x < 0.0) {
            violation += -point.x;
        } else if (point.x > problem.width()) {
            violation += point.x - problem.width();
        }
        if (point.y < 0.0) {
            violation += -point.y;
        } else if (point.y > problem.height()) {
            violation += point.y - problem.height();
        }
        const auto [begin, end] = obstacle_range(
            problem.scenario_index()
        );
        for (std::size_t obstacle = begin; obstacle < end; ++obstacle) {
            const std::size_t offset = 4 * obstacle;
            const double xmin = data::kObstacles[offset];
            const double ymin = data::kObstacles[offset + 1];
            const double xmax = data::kObstacles[offset + 2];
            const double ymax = data::kObstacles[offset + 3];
            if (
                point.x > xmin && point.x < xmax
                && point.y > ymin && point.y < ymax
            ) {
                violation += std::min({
                    point.x - xmin,
                    xmax - point.x,
                    point.y - ymin,
                    ymax - point.y
                });
            }
        }
    }
    for (std::size_t first = 0; first < layout.size(); ++first) {
        for (std::size_t second = first + 1; second < layout.size(); ++second) {
            violation += std::max(
                0.0,
                kMinimumSpacing - distance(layout[first], layout[second])
            );
        }
    }
    return violation;
}

std::vector<double> direction_outputs(
    const Problem& problem,
    const std::vector<Point>& layout,
    std::size_t direction
) {
    const std::size_t scenario =
        static_cast<std::size_t>(problem.scenario_index());
    const double theta =
        (15.0 * static_cast<double>(direction) + 7.5) * kPi / 180.0;
    const double cosine = std::cos(theta);
    const double sine = std::sin(theta);
    const double alpha = std::atan(0.075);
    const double rk_ratio = kRadius / 0.075;
    const double deficit_scale = 1.0 - std::sqrt(1.0 - 0.8);
    std::vector<double> outputs(layout.size(), 0.0);
    for (std::size_t turbine = 0; turbine < layout.size(); ++turbine) {
        double squared_deficit = 0.0;
        for (std::size_t other = 0; other < layout.size(); ++other) {
            if (turbine == other) {
                continue;
            }
            const double dx = layout[turbine].x - layout[other].x;
            const double dy = layout[turbine].y - layout[other].y;
            const double numerator = dx * cosine + dy * sine + rk_ratio;
            const double denominator = std::hypot(
                dx + rk_ratio * cosine,
                dy + rk_ratio * sine
            );
            if (denominator <= 0.0) {
                continue;
            }
            const double beta = std::acos(std::clamp(
                numerator / denominator,
                -1.0,
                1.0
            ));
            if (beta < alpha) {
                const double projected =
                    std::abs(dx * cosine + dy * sine);
                const double loss = deficit_scale
                    / square(1.0 + (0.075 / kRadius) * projected);
                squared_deficit += loss * loss;
            }
        }
        const double scale =
            data::kWeibullScale[scenario * data::kDirections + direction]
            * (1.0 - std::sqrt(squared_deficit));
        const double shape =
            data::kWeibullShape[scenario * data::kDirections + direction];
        double expected = 0.0;
        for (int interval = 1; interval <= 21; ++interval) {
            const double lower = 3.5 + 0.5 * static_cast<double>(interval - 1);
            const double upper = lower + 0.5;
            const double speed = 0.5 * (lower + upper);
            expected += (
                weibull_cdf(upper, scale, shape)
                - weibull_cdf(lower, scale, shape)
            ) * power_kw(speed);
        }
        expected += 1500.0
            * (1.0 - weibull_cdf(14.0, scale, shape));
        const double density =
            data::kDirectionDensity[
                scenario * data::kDirections + direction
            ];
        outputs[turbine] = expected * 15.0 * density;
    }
    return outputs;
}

Evaluation finish_evaluation(
    const Problem& problem,
    const std::vector<Point>& layout,
    const std::array<std::vector<double>, data::kDirections>& direction
) {
    Evaluation result;
    result.constraint_violation_m =
        violation_serial(problem, layout);
    if (result.constraint_violation_m > 1.0e-10) {
        result.energy_cost = kInvalid;
        return result;
    }
    result.turbine_fitness.assign(layout.size(), 0.0);
    for (std::size_t wind = 0; wind < data::kDirections; ++wind) {
        for (std::size_t turbine = 0; turbine < layout.size(); ++turbine) {
            result.turbine_fitness[turbine] += direction[wind][turbine];
        }
    }
    result.energy_output_kw = std::accumulate(
        result.turbine_fitness.begin(),
        result.turbine_fitness.end(),
        0.0
    );
    const double turbines = static_cast<double>(layout.size());
    const double wake_free =
        data::kWakeFreeEnergy[
            static_cast<std::size_t>(problem.scenario_index())
        ];
    result.wake_free_ratio =
        result.energy_output_kw / (wake_free * turbines);
    const double capital =
        (
            750000.0 * turbines
            + 8000000.0 * std::floor(turbines / 30.0)
        ) * (
            0.666667 + 0.333333 * std::exp(-0.00174 * turbines * turbines)
        ) + 20000.0 * turbines;
    const double annuity = (1.0 - std::pow(1.03, -20.0)) / 0.03;
    result.energy_cost =
        capital / annuity / (8760.0 * result.energy_output_kw)
        + 0.1 / turbines;
    for (double& value : result.turbine_fitness) {
        value /= wake_free;
    }
    return result;
}

Evaluation evaluate_serial(
    const Problem& problem,
    const std::vector<Point>& layout
) {
    std::array<std::vector<double>, data::kDirections> outputs;
    if (violation_serial(problem, layout) > 1.0e-10) {
        Evaluation invalid;
        invalid.energy_cost = kInvalid;
        invalid.constraint_violation_m =
            violation_serial(problem, layout);
        return invalid;
    }
    for (std::size_t direction = 0; direction < data::kDirections; ++direction) {
        outputs[direction] = direction_outputs(problem, layout, direction);
    }
    return finish_evaluation(problem, layout, outputs);
}

std::uint64_t mix_hash(std::uint64_t hash, std::uint64_t value) {
    hash ^= value + 0x9e3779b97f4a7c15ULL + (hash << 6) + (hash >> 2);
    return hash;
}

std::uint64_t result_hash(
    const std::string& algorithm,
    int scenario,
    const std::vector<Point>& layout,
    const Evaluation& evaluation
) {
    std::uint64_t hash = 0xcbf29ce484222325ULL;
    for (unsigned char value : algorithm) {
        hash = mix_hash(hash, value);
    }
    hash = mix_hash(hash, static_cast<std::uint64_t>(scenario));
    hash = mix_hash(hash, std::bit_cast<std::uint64_t>(
        evaluation.energy_cost
    ));
    for (const Point& point : layout) {
        hash = mix_hash(hash, std::bit_cast<std::uint64_t>(point.x));
        hash = mix_hash(hash, std::bit_cast<std::uint64_t>(point.y));
    }
    return hash;
}

struct SearchContext {
    const Problem& problem;
    fode::PersistentExecutor executor;
    std::uint64_t limit;
    std::uint64_t fes = 0;
    double evaluator_seconds = 0.0;

    SearchContext(
        const Problem& selected_problem,
        std::uint64_t selected_limit,
        int workers
    )
        : problem(selected_problem),
          executor(workers),
          limit(selected_limit) {}

    std::vector<Evaluation> evaluate(
        const std::vector<std::vector<Point>>& layouts
    ) {
        const std::size_t count = std::min<std::size_t>(
            layouts.size(),
            static_cast<std::size_t>(limit - fes)
        );
        std::vector<std::vector<Point>> admitted(
            layouts.begin(),
            layouts.begin() + static_cast<std::ptrdiff_t>(count)
        );
        const auto start = Clock::now();
        auto values = problem.evaluate_candidates(admitted, executor);
        evaluator_seconds += seconds_since(start);
        fes += count;
        return values;
    }
};

bool better(const Evaluation& left, const Evaluation& right) {
    if (std::isfinite(left.energy_cost) != std::isfinite(right.energy_cost)) {
        return std::isfinite(left.energy_cost);
    }
    if (left.energy_cost != right.energy_cost) {
        return left.energy_cost < right.energy_cost;
    }
    return left.constraint_violation_m < right.constraint_violation_m;
}

bool inside_obstacle(const Problem& problem, const Point& point) {
    const auto coordinates = problem.obstacle_coordinates();
    for (std::size_t offset = 0; offset < coordinates.size(); offset += 4) {
        if (
            point.x > coordinates[offset]
            && point.x < coordinates[offset + 2]
            && point.y > coordinates[offset + 1]
            && point.y < coordinates[offset + 3]
        ) {
            return true;
        }
    }
    return false;
}

std::vector<Point> repair_spacing(
    std::vector<Point> points,
    const Problem& problem,
    const fode::CounterRng& rng,
    std::uint64_t event
) {
    for (std::size_t index = points.size(); index > 1; --index) {
        const std::size_t selected = static_cast<std::size_t>(rng.integer(
            0,
            static_cast<int>(index),
            event,
            91,
            index
        ));
        std::swap(points[index - 1], points[selected]);
    }
    std::vector<Point> accepted;
    std::unordered_map<std::uint64_t, std::vector<std::size_t>> buckets;
    const auto cell = [](const Point& point) {
        return std::pair{
            static_cast<int>(std::floor(point.x / kMinimumSpacing)),
            static_cast<int>(std::floor(point.y / kMinimumSpacing))
        };
    };
    const auto key = [](int x, int y) {
        return (
            static_cast<std::uint64_t>(static_cast<std::uint32_t>(x))
            << 32U
        ) | static_cast<std::uint32_t>(y);
    };
    for (const Point& point : points) {
        if (!problem.valid_point(point)) {
            continue;
        }
        bool valid = true;
        const auto [cell_x, cell_y] = cell(point);
        for (int dx = -1; dx <= 1 && valid; ++dx) {
            for (int dy = -1; dy <= 1 && valid; ++dy) {
                const auto found = buckets.find(key(cell_x + dx, cell_y + dy));
                if (found == buckets.end()) {
                    continue;
                }
                for (std::size_t index : found->second) {
                    if (
                        distance(point, accepted[index])
                        <= kMinimumSpacing + 0.5
                    ) {
                        valid = false;
                        break;
                    }
                }
            }
        }
        if (valid) {
            buckets[key(cell_x, cell_y)].push_back(accepted.size());
            accepted.push_back(point);
        }
    }
    return accepted;
}

std::vector<Point> rhomboid_layout(
    const Problem& problem,
    const std::array<double, 4>& parameter,
    const fode::CounterRng& rng,
    std::uint64_t event
) {
    const double angle_first = parameter[0] * kPi / 180.0;
    const double angle_second =
        (parameter[0] + parameter[1]) * kPi / 180.0;
    const Point first{
        parameter[2] * std::cos(angle_first),
        parameter[2] * std::sin(angle_first)
    };
    const Point second{
        parameter[3] * std::cos(angle_second),
        parameter[3] * std::sin(angle_second)
    };
    const int reach = static_cast<int>(std::ceil(
        2.0 * std::max(problem.width(), problem.height())
        / std::min(parameter[2], parameter[3])
    )) + 3;
    std::vector<Point> points;
    for (int i = -reach; i <= reach; ++i) {
        for (int j = -reach; j <= reach; ++j) {
            const Point point{
                static_cast<double>(i) * first.x
                    + static_cast<double>(j) * second.x,
                static_cast<double>(i) * first.y
                    + static_cast<double>(j) * second.y
            };
            if (problem.valid_point(point)) {
                points.push_back(point);
            }
        }
    }
    return repair_spacing(std::move(points), problem, rng, event);
}

std::vector<Point> cma_layout(
    const Problem& problem,
    const std::array<double, 5>& parameter
) {
    const double dx = kMinimumSpacing
        + std::pow(0.2 * parameter[0], 4.0)
            * (problem.width() - kMinimumSpacing);
    const double dy = kMinimumSpacing
        + std::pow(0.2 * parameter[1], 4.0)
            * (problem.height() - kMinimumSpacing);
    const double theta = -kPi + 2.0 * kPi * parameter[2];
    const double cosine = std::cos(theta);
    const double sine = std::sin(theta);
    const int nx = static_cast<int>(std::floor(
        4.0 * problem.width() / dx
    ));
    const int ny = static_cast<int>(std::floor(
        4.0 * problem.height() / dy
    ));
    std::vector<Point> result;
    for (int ix = 0; ix <= nx; ++ix) {
        for (int iy = 0; iy <= ny; ++iy) {
            const double raw_x =
                static_cast<double>(ix) * dx - 2.0 * problem.width();
            const double raw_y =
                static_cast<double>(iy) * dy - 2.0 * problem.height();
            const Point point{
                raw_x * cosine - raw_y * sine
                    + (0.5 + 0.2 * parameter[3]) * problem.width(),
                raw_x * sine + raw_y * cosine
                    + (0.5 + 0.2 * parameter[4]) * problem.height()
            };
            if (problem.valid_point(point)) {
                result.push_back(point);
            }
        }
    }
    return result;
}

std::vector<Point> sshh_layout(
    const Problem& problem,
    int step_x,
    int step_y,
    int shift
) {
    constexpr int minimum_cells = 79;
    const double interval = 0.10001 * kRadius;
    const int rows = static_cast<int>(std::ceil(problem.width() / interval));
    const int columns =
        static_cast<int>(std::ceil(problem.height() / interval));
    const int stride_x = minimum_cells + 1 + std::max(0, step_x);
    const int stride_y = minimum_cells + 1 + std::max(0, step_y);
    std::vector<Point> result;
    for (int row = 0; row < rows; row += stride_x) {
        int column_number = 0;
        for (int column = 0; column < columns; column += stride_y) {
            int shifted = (
                row + column_number * std::max(0, shift)
            ) % rows;
            ++column_number;
            const Point point{
                static_cast<double>(shifted) * interval,
                static_cast<double>(column) * interval
            };
            if (problem.valid_point(point)) {
                bool valid = true;
                for (const Point& other : result) {
                    if (distance(point, other) < kMinimumSpacing) {
                        valid = false;
                        break;
                    }
                }
                if (valid) {
                    result.push_back(point);
                }
            }
        }
    }
    return result;
}

std::vector<Point> goldman_grid(
    const Problem& problem,
    const std::array<int, 4>& arguments
) {
    constexpr int angle_slice = 36;
    constexpr int magnitude_slice = 16;
    const double padded = 8.001 * kRadius;
    const auto vector_from = [&](int angle, int magnitude) {
        const double radians =
            static_cast<double>(angle) * kPi / angle_slice;
        const double length =
            padded + static_cast<double>(magnitude) * padded
                / magnitude_slice;
        return Point{length * std::cos(radians), length * std::sin(radians)};
    };
    const Point column = vector_from(arguments[2], arguments[0]);
    const Point row = vector_from(arguments[3], arguments[1]);
    if (
        distance({0.0, 0.0}, column) < kMinimumSpacing
        || distance({0.0, 0.0}, row) < kMinimumSpacing
        || distance(column, row) < kMinimumSpacing
    ) {
        return {};
    }
    const Point origin{problem.width() / 2.0, problem.height() / 2.0};
    const int reach = static_cast<int>(std::ceil(
        2.0 * std::max(problem.width(), problem.height())
        / std::min(
            distance({0.0, 0.0}, column),
            distance({0.0, 0.0}, row)
        )
    )) + 3;
    std::vector<Point> result;
    for (int first = -reach; first <= reach; ++first) {
        for (int second = -reach; second <= reach; ++second) {
            const Point point{
                origin.x + static_cast<double>(first) * column.x
                    + static_cast<double>(second) * row.x,
                origin.y + static_cast<double>(first) * column.y
                    + static_cast<double>(second) * row.y
            };
            if (problem.valid_point(point)) {
                for (const Point& other : result) {
                    if (distance(point, other) < kMinimumSpacing) {
                        return {};
                    }
                }
                result.push_back(point);
            }
        }
    }
    return result;
}

struct Candidate {
    std::vector<Point> layout;
    Evaluation evaluation;
};

Candidate best_of(
    const std::vector<std::vector<Point>>& layouts,
    const std::vector<Evaluation>& evaluations
) {
    Candidate best;
    best.evaluation.energy_cost = kInvalid;
    for (std::size_t index = 0; index < evaluations.size(); ++index) {
        if (better(evaluations[index], best.evaluation)) {
            best = {layouts[index], evaluations[index]};
        }
    }
    return best;
}

Candidate run_cmaes(
    SearchContext& context,
    const fode::CounterRng& rng
) {
    constexpr int dimension = 5;
    constexpr int lambda = 8;
    constexpr int mu = 4;
    std::array<double, dimension> mean{};
    mean.fill(0.5);
    std::array<std::array<double, dimension>, dimension> covariance{};
    for (int index = 0; index < dimension; ++index) {
        covariance[index][index] = 1.0;
    }
    std::array<double, mu> weights{
        std::log(4.5 / 1.0),
        std::log(4.5 / 2.0),
        std::log(4.5 / 3.0),
        std::log(4.5 / 4.0)
    };
    const double sum_weights =
        std::accumulate(weights.begin(), weights.end(), 0.0);
    for (double& weight : weights) {
        weight /= sum_weights;
    }
    const double mueff = 1.0 / std::accumulate(
        weights.begin(),
        weights.end(),
        0.0,
        [](double sum, double value) { return sum + value * value; }
    );
    const double cc = (4.0 + mueff / dimension)
        / (dimension + 4.0 + 2.0 * mueff / dimension);
    const double c1 = 2.0
        / (square(dimension + 1.3) + mueff);
    const double cmu = std::min(
        1.0 - c1,
        2.0 * (mueff - 2.0 + 1.0 / mueff)
            / (square(dimension + 2.0) + mueff)
    );
    double sigma = 0.3;
    std::array<double, dimension> path{};
    Candidate global;
    global.evaluation.energy_cost = kInvalid;
    std::uint64_t generation = 0;
    while (context.fes < context.limit) {
        std::array<std::array<double, dimension>, dimension> lower{};
        bool positive = true;
        for (int row = 0; row < dimension && positive; ++row) {
            for (int column = 0; column <= row; ++column) {
                double value = covariance[row][column];
                for (int inner = 0; inner < column; ++inner) {
                    value -= lower[row][inner] * lower[column][inner];
                }
                if (row == column) {
                    if (value <= 1.0e-12) {
                        positive = false;
                        break;
                    }
                    lower[row][column] = std::sqrt(value);
                } else {
                    lower[row][column] = value / lower[column][column];
                }
            }
        }
        if (!positive) {
            for (auto& row : covariance) {
                row.fill(0.0);
            }
            for (int index = 0; index < dimension; ++index) {
                covariance[index][index] = 1.0;
            }
            continue;
        }
        struct Sample {
            std::array<double, dimension> x{};
            std::array<double, dimension> y{};
            std::vector<Point> layout;
            Evaluation evaluation;
        };
        std::vector<Sample> samples(
            std::min<std::uint64_t>(lambda, context.limit - context.fes)
        );
        std::vector<std::vector<Point>> layouts;
        for (std::size_t member = 0; member < samples.size(); ++member) {
            std::array<double, dimension> normal{};
            for (int index = 0; index < dimension; ++index) {
                normal[index] = rng.normal(
                    generation,
                    300,
                    member,
                    index
                );
            }
            for (int row = 0; row < dimension; ++row) {
                for (int column = 0; column <= row; ++column) {
                    samples[member].y[row] +=
                        lower[row][column] * normal[column];
                }
                samples[member].x[row] = std::clamp(
                    mean[row] + sigma * samples[member].y[row],
                    0.0,
                    1.0
                );
            }
            samples[member].layout =
                cma_layout(context.problem, samples[member].x);
            layouts.push_back(samples[member].layout);
        }
        const auto values = context.evaluate(layouts);
        for (std::size_t member = 0; member < values.size(); ++member) {
            samples[member].evaluation = values[member];
            if (better(values[member], global.evaluation)) {
                global = {layouts[member], values[member]};
            }
        }
        std::sort(
            samples.begin(),
            samples.end(),
            [](const Sample& left, const Sample& right) {
                return better(left.evaluation, right.evaluation);
            }
        );
        const auto old_mean = mean;
        mean.fill(0.0);
        for (int parent = 0; parent < std::min<int>(mu, samples.size()); ++parent) {
            for (int coordinate = 0; coordinate < dimension; ++coordinate) {
                mean[coordinate] +=
                    weights[parent] * samples[parent].x[coordinate];
            }
        }
        for (int coordinate = 0; coordinate < dimension; ++coordinate) {
            const double step =
                (mean[coordinate] - old_mean[coordinate]) / sigma;
            path[coordinate] =
                (1.0 - cc) * path[coordinate]
                + std::sqrt(cc * (2.0 - cc) * mueff) * step;
        }
        for (int row = 0; row < dimension; ++row) {
            for (int column = 0; column <= row; ++column) {
                double rank_mu = 0.0;
                for (
                    int parent = 0;
                    parent < std::min<int>(mu, samples.size());
                    ++parent
                ) {
                    rank_mu += weights[parent]
                        * samples[parent].y[row]
                        * samples[parent].y[column];
                }
                covariance[row][column] =
                    (1.0 - c1 - cmu) * covariance[row][column]
                    + c1 * path[row] * path[column]
                    + cmu * rank_mu;
                covariance[column][row] = covariance[row][column];
            }
        }
        const double path_norm = std::sqrt(std::inner_product(
            path.begin(),
            path.end(),
            path.begin(),
            0.0
        ));
        const double expected_norm =
            std::sqrt(static_cast<double>(dimension))
            * (
                1.0 - 1.0 / (4.0 * dimension)
                + 1.0 / (21.0 * dimension * dimension)
            );
        sigma *= std::exp(0.3 * (path_norm / expected_norm - 1.0));
        sigma = std::clamp(sigma, 1.0e-4, 1.0);
        ++generation;
    }
    return global;
}

int sshh_parameter(
    int kind,
    const fode::CounterRng& rng,
    std::uint64_t iteration,
    std::uint64_t draw
) {
    if (kind == 0) {
        return 1;
    }
    if (kind == 1) {
        return rng.integer(1, 11, iteration, 410, draw);
    }
    return rng.integer(10, 80, iteration, 411, draw);
}

Candidate run_sshh(
    SearchContext& context,
    const fode::CounterRng& rng
) {
    std::array<std::vector<int>, 5> next;
    std::array<std::vector<int>, 5> acceptance;
    std::array<std::vector<int>, 5> parameters;
    for (int index = 0; index < 5; ++index) {
        next[index] = {0, 1, 2, 3, 4};
        acceptance[index] = {0, 1};
        parameters[index] = {0, 1, 2};
    }
    int step_x = 0;
    int step_y = 0;
    int shift = 0;
    auto current_layout = sshh_layout(context.problem, step_x, step_y, shift);
    auto values = context.evaluate({current_layout});
    Candidate current{current_layout, values.front()};
    Candidate best = current;
    int previous_heuristic = rng.integer(0, 5, 0, 420, 0);
    std::uint64_t iteration = 1;
    while (context.fes < context.limit) {
        const auto& next_options = next[previous_heuristic];
        const int heuristic = next_options[static_cast<std::size_t>(
            rng.integer(
                0,
                static_cast<int>(next_options.size()),
                iteration,
                421,
                0
            )
        )];
        const auto& acceptance_options = acceptance[heuristic];
        const int accept_flag = acceptance_options[static_cast<std::size_t>(
            rng.integer(
                0,
                static_cast<int>(acceptance_options.size()),
                iteration,
                422,
                0
            )
        )];
        const auto& parameter_options = parameters[heuristic];
        const int parameter_kind = parameter_options[static_cast<std::size_t>(
            rng.integer(
                0,
                static_cast<int>(parameter_options.size()),
                iteration,
                423,
                0
            )
        )];
        const int old_x = step_x;
        const int old_y = step_y;
        const int old_shift = shift;
        auto signed_change = [&](int magnitude, std::uint64_t draw) {
            return rng.uniform(iteration, 424, draw) < 0.5
                ? -magnitude
                : magnitude;
        };
        auto maybe_reset = [&](int& value, std::uint64_t draw) {
            if (rng.uniform(iteration, 425, draw) < 0.30) {
                value = 0;
            }
        };
        int p = sshh_parameter(parameter_kind, rng, iteration, 1);
        if (heuristic == 0) {
            maybe_reset(shift, 0);
            step_x = std::max(0, step_x + signed_change(p, 1));
        } else if (heuristic == 1) {
            maybe_reset(shift, 2);
            step_y = std::max(0, step_y + signed_change(p, 3));
        } else if (heuristic == 2) {
            maybe_reset(step_x, 4);
            maybe_reset(step_y, 5);
            shift = std::max(0, shift + signed_change(p, 6));
        } else if (heuristic == 3) {
            maybe_reset(shift, 7);
            step_x = std::max(0, step_x + signed_change(p, 8));
            p = sshh_parameter(parameter_kind, rng, iteration, 9);
            step_y = std::max(0, step_y + signed_change(p, 10));
        } else {
            step_x = std::max(0, step_x + signed_change(p, 11));
            p = sshh_parameter(parameter_kind, rng, iteration, 12);
            step_y = std::max(0, step_y + signed_change(p, 13));
            p = sshh_parameter(parameter_kind, rng, iteration, 14);
            shift = std::max(0, shift + signed_change(p, 15));
        }
        auto trial_layout =
            sshh_layout(context.problem, step_x, step_y, shift);
        values = context.evaluate({trial_layout});
        const Evaluation trial = values.front();
        const bool improved_best = better(trial, best.evaluation);
        if (
            better(trial, current.evaluation)
            || (
                std::isfinite(trial.energy_cost)
                && std::isfinite(best.evaluation.energy_cost)
                && trial.energy_cost
                    <= 1.01 * best.evaluation.energy_cost
            )
        ) {
            current = {std::move(trial_layout), trial};
            if (improved_best) {
                best = current;
                if (next[previous_heuristic].size() < 100000) {
                    next[previous_heuristic].push_back(heuristic);
                    acceptance[heuristic].push_back(accept_flag);
                    parameters[heuristic].push_back(parameter_kind);
                }
            }
        } else {
            step_x = old_x;
            step_y = old_y;
            shift = old_shift;
        }
        if (accept_flag == 0) {
            previous_heuristic = heuristic;
        }
        ++iteration;
    }
    return best;
}

struct GoldmanKeyHash {
    std::size_t operator()(const std::array<int, 4>& value) const noexcept {
        std::size_t result = 0;
        for (int item : value) {
            result = result * 131U + static_cast<std::size_t>(item + 1);
        }
        return result;
    }
};

Candidate run_goldman(SearchContext& context) {
    std::unordered_map<std::array<int, 4>, Candidate, GoldmanKeyHash> cache;
    Candidate global;
    global.evaluation.energy_cost = kInvalid;
    auto evaluate_keys = [&](const std::vector<std::array<int, 4>>& keys) {
        std::vector<std::array<int, 4>> missing;
        std::vector<std::vector<Point>> full_layouts;
        for (const auto& key : keys) {
            if (!cache.contains(key)) {
                auto layout = goldman_grid(context.problem, key);
                if (!layout.empty()) {
                    missing.push_back(key);
                    full_layouts.push_back(std::move(layout));
                } else {
                    Candidate invalid;
                    invalid.evaluation.energy_cost = kInvalid;
                    cache.emplace(key, std::move(invalid));
                }
            }
        }
        const auto full_values = context.evaluate(full_layouts);
        std::vector<std::vector<Point>> trimmed_layouts;
        std::vector<std::size_t> trim_parent;
        for (std::size_t index = 0; index < full_values.size(); ++index) {
            Candidate candidate{full_layouts[index], full_values[index]};
            cache[missing[index]] = candidate;
            if (better(candidate.evaluation, global.evaluation)) {
                global = candidate;
            }
            const std::size_t remove =
                (full_layouts[index].size() + 1U) % 30U;
            if (
                remove > 0 && remove < full_layouts[index].size()
                && context.fes + trimmed_layouts.size() < context.limit
            ) {
                std::vector<std::size_t> order(full_layouts[index].size());
                std::iota(order.begin(), order.end(), 0U);
                std::sort(
                    order.begin(),
                    order.end(),
                    [&](std::size_t left, std::size_t right) {
                        return full_values[index].turbine_fitness[left]
                            > full_values[index].turbine_fitness[right];
                    }
                );
                order.resize(order.size() - remove);
                std::vector<Point> trimmed;
                for (std::size_t selected : order) {
                    trimmed.push_back(full_layouts[index][selected]);
                }
                trimmed_layouts.push_back(std::move(trimmed));
                trim_parent.push_back(index);
            }
        }
        const auto trimmed_values = context.evaluate(trimmed_layouts);
        for (std::size_t index = 0; index < trimmed_values.size(); ++index) {
            Candidate candidate{
                trimmed_layouts[index],
                trimmed_values[index]
            };
            auto& stored = cache[missing[trim_parent[index]]];
            if (better(candidate.evaluation, stored.evaluation)) {
                stored = candidate;
            }
            if (better(candidate.evaluation, global.evaluation)) {
                global = candidate;
            }
        }
    };
    const std::array<std::array<int, 4>, 2> starts{{
        {0, 32, 0, 18},
        {32, 0, 0, 18}
    }};
    const std::array<int, 4> maxima{64, 64, 35, 35};
    const std::array<std::array<int, 4>, 2> orders{{
        {2, 3, 0, 1},
        {3, 2, 1, 0}
    }};
    for (int pass = 0; pass < 2 && context.fes < context.limit; ++pass) {
        auto current = starts[pass];
        evaluate_keys({current});
        bool changed = true;
        while (changed && context.fes < context.limit) {
            changed = false;
            for (int variable : orders[pass]) {
                std::vector<std::array<int, 4>> keys;
                for (int value = 0; value <= maxima[variable]; ++value) {
                    auto key = current;
                    key[variable] = value;
                    keys.push_back(key);
                }
                evaluate_keys(keys);
                auto best_key = current;
                for (const auto& key : keys) {
                    if (
                        cache.contains(key)
                        && better(
                            cache[key].evaluation,
                            cache[best_key].evaluation
                        )
                    ) {
                        best_key = key;
                    }
                }
                if (best_key != current) {
                    current = best_key;
                    changed = true;
                }
                if (context.fes >= context.limit) {
                    break;
                }
            }
        }
    }
    return global;
}

struct Surrogate {
    std::array<std::array<double, 720>, 6> directional{};
    std::array<double, 6> distances{};
    double single_energy = 0.0;

    double score(const std::vector<Point>& layout) const {
        if (layout.empty()) {
            return -kInvalid;
        }
        double total = static_cast<double>(layout.size()) * single_energy;
        for (std::size_t first = 0; first < layout.size(); ++first) {
            for (
                std::size_t second = first + 1;
                second < layout.size();
                ++second
            ) {
                const double d = distance(layout[first], layout[second]);
                if (d > distances.back()) {
                    continue;
                }
                std::size_t radial = 0;
                while (
                    radial + 1 < distances.size()
                    && d > distances[radial + 1]
                ) {
                    ++radial;
                }
                double angle = std::atan2(
                    layout[second].y - layout[first].y,
                    layout[second].x - layout[first].x
                ) * 180.0 / kPi;
                if (angle < 0.0) {
                    angle += 360.0;
                }
                const std::size_t angular = static_cast<std::size_t>(
                    std::floor(2.0 * angle)
                ) % 720U;
                total -= std::max(
                    0.0,
                    2.0 * single_energy
                        - directional[radial][angular]
                );
            }
        }
        return total;
    }

    double pair_energy(double separation, double angle_degrees) const {
        if (separation > distances.back()) {
            return 2.0 * single_energy;
        }
        std::size_t radial = 0;
        while (
            radial + 1 < distances.size()
            && separation > distances[radial + 1]
        ) {
            ++radial;
        }
        double normalized = std::fmod(angle_degrees, 360.0);
        if (normalized < 0.0) {
            normalized += 360.0;
        }
        const std::size_t angular = static_cast<std::size_t>(
            std::floor(2.0 * normalized)
        ) % 720U;
        return directional[radial][angular];
    }
};

double usable_area(const Problem& problem) {
    double area = problem.width() * problem.height();
    const auto obstacles = problem.obstacle_coordinates();
    for (std::size_t offset = 0; offset < obstacles.size(); offset += 4) {
        area -=
            (obstacles[offset + 2] - obstacles[offset])
            * (obstacles[offset + 3] - obstacles[offset + 1]);
    }
    return area;
}

double rhomboid_surrogate_score(
    const Problem& problem,
    const std::array<double, 4>& parameter,
    const Surrogate& surrogate,
    double problem_usable_area
) {
    const double first_angle = parameter[0] * kPi / 180.0;
    const double second_angle =
        (parameter[0] + parameter[1]) * kPi / 180.0;
    const Point first{
        parameter[2] * std::cos(first_angle),
        parameter[2] * std::sin(first_angle)
    };
    const Point second{
        parameter[3] * std::cos(second_angle),
        parameter[3] * std::sin(second_angle)
    };
    const double cell_area = std::abs(
        first.x * second.y - first.y * second.x
    );
    if (cell_area <= 1.0e-9) {
        return -kInvalid;
    }
    const double estimated_turbines = std::max(
        1.0,
        std::floor(problem_usable_area / cell_area)
    );
    double pair_loss_per_turbine = 0.0;
    const int reach = static_cast<int>(
        std::ceil(surrogate.distances.back()
                  / std::min(parameter[2], parameter[3]))
    ) + 2;
    for (int i = -reach; i <= reach; ++i) {
        for (int j = -reach; j <= reach; ++j) {
            if (i == 0 && j == 0) {
                continue;
            }
            const Point displacement{
                static_cast<double>(i) * first.x
                    + static_cast<double>(j) * second.x,
                static_cast<double>(i) * first.y
                    + static_cast<double>(j) * second.y
            };
            const double separation = std::hypot(
                displacement.x,
                displacement.y
            );
            if (separation < kMinimumSpacing) {
                return -kInvalid;
            }
            if (separation <= surrogate.distances.back()) {
                const double angle =
                    std::atan2(displacement.y, displacement.x)
                    * 180.0 / kPi;
                pair_loss_per_turbine += std::max(
                    0.0,
                    2.0 * surrogate.single_energy
                        - surrogate.pair_energy(separation, angle)
                );
            }
        }
    }
    const double per_turbine = std::max(
        1.0e-12,
        surrogate.single_energy - 0.5 * pair_loss_per_turbine
    );
    const double energy = estimated_turbines * per_turbine;
    const double capital =
        (
            750000.0 * estimated_turbines
            + 8000000.0 * std::floor(estimated_turbines / 30.0)
        ) * (
            0.666667
            + 0.333333 * std::exp(
                -0.00174 * estimated_turbines * estimated_turbines
            )
        ) + 20000.0 * estimated_turbines;
    const double annuity = (1.0 - std::pow(1.03, -20.0)) / 0.03;
    const double cost =
        capital / annuity / (8760.0 * energy)
        + 0.1 / estimated_turbines;
    return -cost;
}

Surrogate build_surrogate(
    SearchContext& context,
    const fode::CounterRng& rng
) {
    Surrogate surrogate;
    surrogate.distances = {
        kMinimumSpacing + 8.0,
        1.5 * kMinimumSpacing,
        2.0 * kMinimumSpacing,
        2.5 * kMinimumSpacing,
        3.0 * kMinimumSpacing,
        3.5 * kMinimumSpacing
    };
    std::vector<std::vector<Point>> layouts;
    std::vector<std::pair<std::size_t, std::size_t>> owners;
    for (std::size_t radial = 0; radial < 6; ++radial) {
        const std::size_t angles = radial == 0 ? 360 : 180;
        for (std::size_t angle = 0; angle < angles; ++angle) {
            const double degrees =
                radial == 0 ? 0.5 * angle : static_cast<double>(angle);
            const double radians = degrees * kPi / 180.0;
            const double dx = surrogate.distances[radial] * std::cos(radians);
            const double dy = surrogate.distances[radial] * std::sin(radians);
            Point first{
                context.problem.width() * (
                    0.25 + 0.5 * rng.uniform(radial, 500, angle, 0)
                ),
                context.problem.height() * (
                    0.25 + 0.5 * rng.uniform(radial, 500, angle, 1)
                )
            };
            Point second{first.x + dx, first.y + dy};
            if (!context.problem.valid_point(first)
                || !context.problem.valid_point(second)) {
                first = {
                    context.problem.width() / 2.0 - 0.5 * dx,
                    context.problem.height() / 2.0 - 0.5 * dy
                };
                second = {first.x + dx, first.y + dy};
            }
            if (
                context.problem.valid_point(first)
                && context.problem.valid_point(second)
            ) {
                layouts.push_back({first, second});
                owners.emplace_back(radial, angle);
            }
        }
    }
    const auto values = context.evaluate(layouts);
    for (std::size_t index = 0; index < values.size(); ++index) {
        const auto [radial, angle] = owners[index];
        const double energy = values[index].energy_output_kw;
        const std::size_t primary =
            radial == 0 ? angle : 2U * angle;
        const std::size_t mirror = (primary + 360U) % 720U;
        surrogate.directional[radial][primary] = energy;
        surrogate.directional[radial][mirror] = energy;
        surrogate.single_energy = std::max(
            surrogate.single_energy,
            0.5 * energy
        );
    }
    for (std::size_t radial = 0; radial < 6; ++radial) {
        double last = 2.0 * surrogate.single_energy;
        for (std::size_t angle = 0; angle < 720; ++angle) {
            if (surrogate.directional[radial][angle] > 0.0) {
                last = surrogate.directional[radial][angle];
            } else {
                surrogate.directional[radial][angle] = last;
            }
        }
    }
    return surrogate;
}

std::array<double, 4> improve_rhomboid(
    const Problem& problem,
    std::array<double, 4> parameter,
    const Surrogate& surrogate,
    const fode::CounterRng& rng,
    std::uint64_t member,
    int iterations,
    fode::PersistentExecutor* executor = nullptr
) {
    const std::array<double, 4> lower{
        0.0, 30.0, kMinimumSpacing + 1.0, kMinimumSpacing + 1.0
    };
    const std::array<double, 4> upper{
        90.0, 150.0, 5.0 * kMinimumSpacing + 1.0,
        5.0 * kMinimumSpacing + 1.0
    };
    const double area = usable_area(problem);
    double best = rhomboid_surrogate_score(
        problem, parameter, surrogate, area
    );
    auto loop = [&](int iteration) {
        auto trial = parameter;
        bool changed = false;
        for (int coordinate = 0; coordinate < 4; ++coordinate) {
            if (
                rng.uniform(member, 520, iteration, coordinate) <= 0.25
            ) {
                const double initial_step = coordinate < 2 ? 45.0 : 250.0;
                const double step = initial_step * (
                    1.0 - static_cast<double>(iteration)
                        / static_cast<double>(iterations + 1)
                );
                trial[coordinate] = std::clamp(
                    parameter[coordinate]
                        + (
                            rng.uniform(
                                member,
                                521,
                                iteration,
                                coordinate
                            ) - 0.5
                        ) * step,
                    lower[coordinate],
                    upper[coordinate]
                );
                changed = true;
            }
        }
        if (!changed) {
            const int coordinate = rng.integer(
                0, 4, member, 522, iteration
            );
            trial[coordinate] = 0.5
                * (lower[coordinate] + upper[coordinate]);
        }
        const double score = rhomboid_surrogate_score(
            problem, trial, surrogate, area
        );
        return std::pair{trial, score};
    };
    // Each member owns its adaptive trajectory.  Parallelism is applied across
    // members by the caller; within one trajectory the paper's state order is
    // preserved.
    static_cast<void>(executor);
    for (int iteration = 0; iteration < iterations; ++iteration) {
        const auto [trial, score] = loop(iteration);
        if (score > best) {
            parameter = trial;
            best = score;
        }
    }
    return parameter;
}

Candidate run_3s_mde(
    SearchContext& context,
    const fode::CounterRng& rng
) {
    Surrogate surrogate = build_surrogate(context, rng);
    const double area = usable_area(context.problem);
    if (context.fes >= context.limit) {
        Candidate empty;
        empty.evaluation.energy_cost = kInvalid;
        return empty;
    }
    constexpr std::size_t initial_size = 200;
    std::vector<std::array<double, 4>> parameters(initial_size);
    context.executor.parallel_for(0, static_cast<int>(initial_size), [&](int raw) {
        const std::size_t member = static_cast<std::size_t>(raw);
        std::array<double, 4> value{
            90.0 * rng.uniform(0, 530, member, 0),
            70.0 + 40.0 * rng.uniform(0, 530, member, 1),
            kMinimumSpacing + 1.0
                + 2.0 * kMinimumSpacing
                    * rng.uniform(0, 530, member, 2),
            kMinimumSpacing + 1.0
                + 2.0 * kMinimumSpacing
                    * rng.uniform(0, 530, member, 3)
        };
        parameters[member] = improve_rhomboid(
            context.problem,
            value,
            surrogate,
            rng,
            member,
            15000
        );
    });
    std::vector<std::vector<Point>> layouts;
    for (std::size_t member = 0; member < initial_size; ++member) {
        layouts.push_back(rhomboid_layout(
            context.problem,
            parameters[member],
            rng,
            600 + member
        ));
    }
    const auto values = context.evaluate(layouts);
    struct Individual {
        std::array<double, 4> parameter{};
        std::vector<Point> layout;
        Evaluation evaluation;
        double surrogate_score = -kInvalid;
    };
    std::vector<Individual> population;
    Candidate global;
    global.evaluation.energy_cost = kInvalid;
    for (std::size_t member = 0; member < values.size(); ++member) {
        population.push_back({
            parameters[member],
            layouts[member],
            values[member],
            rhomboid_surrogate_score(
                context.problem, parameters[member], surrogate, area
            )
        });
        if (better(values[member], global.evaluation)) {
            global = {layouts[member], values[member]};
        }
    }
    std::sort(
        population.begin(),
        population.end(),
        [](const Individual& left, const Individual& right) {
            return better(left.evaluation, right.evaluation);
        }
    );
    if (population.size() > 10) {
        population.resize(10);
    }
    std::uint64_t generation = 1;
    const std::uint64_t third_stage =
        static_cast<std::uint64_t>(0.90 * static_cast<double>(context.limit));
    while (
        context.fes < third_stage
        && context.fes < context.limit
        && population.size() >= 4
    ) {
        std::vector<std::array<double, 4>> trials(population.size());
        context.executor.parallel_for(
            0,
            static_cast<int>(population.size()),
            [&](int raw) {
                const std::size_t member = static_cast<std::size_t>(raw);
                auto select = [&](std::uint64_t draw) {
                    std::size_t selected;
                    do {
                        selected = static_cast<std::size_t>(rng.integer(
                            0,
                            static_cast<int>(population.size()),
                            generation,
                            540 + draw,
                            member
                        ));
                    } while (selected == member);
                    return selected;
                };
                const std::size_t r1 = select(0);
                std::size_t r2 = select(1);
                while (r2 == r1) {
                    r2 = select(2 + r2);
                }
                std::size_t r3 = select(20);
                while (r3 == r1 || r3 == r2) {
                    r3 = select(21 + r3);
                }
                auto trial = population[member].parameter;
                const int mandatory = rng.integer(
                    0, 4, generation, 560, member
                );
                const double scale =
                    0.5 + 0.3 * rng.normal(generation, 561, member);
                const std::array<double, 4> lower{
                    0.0, 30.0, kMinimumSpacing + 1.0,
                    kMinimumSpacing + 1.0
                };
                const std::array<double, 4> upper{
                    90.0, 150.0, 5.0 * kMinimumSpacing + 1.0,
                    5.0 * kMinimumSpacing + 1.0
                };
                for (int coordinate = 0; coordinate < 4; ++coordinate) {
                    if (
                        coordinate == mandatory
                        || rng.uniform(
                            generation, 562, member, coordinate
                        ) < 0.9
                    ) {
                        trial[coordinate] =
                            population[r1].parameter[coordinate]
                            + scale * (
                                population[r2].parameter[coordinate]
                                - population[r3].parameter[coordinate]
                            );
                        if (
                            trial[coordinate] < lower[coordinate]
                            || trial[coordinate] > upper[coordinate]
                        ) {
                            trial[coordinate] = lower[coordinate]
                                + rng.uniform(
                                    generation, 563, member, coordinate
                                ) * (
                                    upper[coordinate] - lower[coordinate]
                                );
                        }
                    }
                }
                trials[member] = improve_rhomboid(
                    context.problem,
                    trial,
                    surrogate,
                    rng,
                    generation * 1000ULL + member,
                    5000
                );
            }
        );
        std::vector<std::vector<Point>> offspring;
        std::vector<std::size_t> owners;
        std::vector<std::vector<Point>> decoded_trials(trials.size());
        std::vector<double> approximate_scores(trials.size(), -kInvalid);
        std::vector<bool> admitted(trials.size(), false);
        for (std::size_t member = 0; member < trials.size(); ++member) {
            decoded_trials[member] = rhomboid_layout(
                context.problem,
                trials[member],
                rng,
                generation * 1000ULL + member
            );
            const double approximate = rhomboid_surrogate_score(
                context.problem, trials[member], surrogate, area
            );
            approximate_scores[member] = approximate;
            if (
                !std::isfinite(global.evaluation.energy_cost)
                // Source admits a trial when its surrogate cost is no more
                // than 1.20 times the best cost.  Scores here are negative
                // costs, hence the inequality uses 1.20 rather than 0.80.
                || approximate >= 1.2 * population[member].surrogate_score
            ) {
                admitted[member] = true;
                owners.push_back(member);
                offspring.push_back(decoded_trials[member]);
            }
        }
        // The released source assumes its interpolated surrogate remains
        // calibrated enough to admit a useful number of offspring.  Our
        // analytic HPC aggregation has the same pair data but a different
        // boundary approximation.  If calibration admits fewer than half the
        // population, fill the batch with the best surrogate-ranked trials.
        // This deterministic completion prevents zero-FES generations while
        // retaining the paper's filter and ranking semantics.
        std::vector<std::size_t> ranking(trials.size());
        std::iota(ranking.begin(), ranking.end(), 0U);
        std::sort(
            ranking.begin(),
            ranking.end(),
            [&](std::size_t left, std::size_t right) {
                return approximate_scores[left] > approximate_scores[right];
            }
        );
        const std::size_t minimum_admitted =
            std::min<std::size_t>(5, trials.size());
        for (std::size_t member : ranking) {
            if (offspring.size() >= minimum_admitted) {
                break;
            }
            if (!admitted[member]) {
                admitted[member] = true;
                owners.push_back(member);
                offspring.push_back(decoded_trials[member]);
            }
        }
        const auto offspring_values = context.evaluate(offspring);
        for (std::size_t index = 0; index < offspring_values.size(); ++index) {
            const std::size_t member = owners[index];
            if (better(offspring_values[index], population[member].evaluation)) {
                population[member] = {
                    trials[member],
                    offspring[index],
                    offspring_values[index],
                    rhomboid_surrogate_score(
                        context.problem, trials[member], surrogate, area
                    )
                };
            }
            if (better(offspring_values[index], global.evaluation)) {
                global = {offspring[index], offspring_values[index]};
            }
        }
        ++generation;
    }
    // Paper stage 3: use remaining physical evaluations for simultaneous
    // bounded turbine perturbations; candidate generation is independent and
    // therefore evaluated as all-core batches.
    std::uint64_t local_iteration = 0;
    while (
        context.fes < context.limit
        && std::isfinite(global.evaluation.energy_cost)
        && !global.layout.empty()
    ) {
        const std::size_t batch = static_cast<std::size_t>(
            std::min<std::uint64_t>(
                static_cast<std::uint64_t>(context.executor.thread_count()),
                context.limit - context.fes
            )
        );
        std::vector<std::vector<Point>> neighbors(batch, global.layout);
        for (std::size_t candidate = 0; candidate < batch; ++candidate) {
            for (
                std::size_t turbine = candidate;
                turbine < neighbors[candidate].size();
                turbine += std::max<std::size_t>(1, batch)
            ) {
                const double length =
                    6.25 * rng.uniform(local_iteration, 570, candidate, turbine);
                const double angle =
                    2.0 * kPi
                    * rng.uniform(
                        local_iteration, 571, candidate, turbine
                    );
                Point trial{
                    neighbors[candidate][turbine].x
                        + length * std::cos(angle),
                    neighbors[candidate][turbine].y
                        + length * std::sin(angle)
                };
                if (context.problem.valid_point(trial)) {
                    neighbors[candidate][turbine] = trial;
                }
            }
        }
        const auto neighbor_values = context.evaluate(neighbors);
        const Candidate selected = best_of(neighbors, neighbor_values);
        if (better(selected.evaluation, global.evaluation)) {
            global = selected;
        }
        ++local_iteration;
    }
    return global;
}

}  // namespace

Problem::Problem(int scenario_index)
    : scenario_index_(scenario_index),
      id_("t12_windflo_s" + std::to_string(scenario_index + 1)) {
    if (scenario_index < 0
        || scenario_index >= static_cast<int>(data::kScenarios)) {
        throw std::invalid_argument("T12 scenario must be 0..4");
    }
}

int Problem::scenario_index() const noexcept {
    return scenario_index_;
}

const std::string& Problem::id() const noexcept {
    return id_;
}

double Problem::width() const noexcept {
    return data::kWidth[static_cast<std::size_t>(scenario_index_)];
}

double Problem::height() const noexcept {
    return data::kHeight[static_cast<std::size_t>(scenario_index_)];
}

double Problem::radius() const noexcept {
    return kRadius;
}

double Problem::minimum_spacing() const noexcept {
    return kMinimumSpacing;
}

int Problem::nominal_turbines() const noexcept {
    return static_cast<int>(
        data::kNominalTurbines[static_cast<std::size_t>(scenario_index_)]
    );
}

std::vector<double> Problem::obstacle_coordinates() const {
    const auto [begin, end] = obstacle_range(scenario_index_);
    return {
        data::kObstacles.begin() + static_cast<std::ptrdiff_t>(4 * begin),
        data::kObstacles.begin() + static_cast<std::ptrdiff_t>(4 * end)
    };
}

bool Problem::valid_point(const Point& point) const noexcept {
    if (
        point.x < 0.0 || point.x > width()
        || point.y < 0.0 || point.y > height()
    ) {
        return false;
    }
    const auto [begin, end] = obstacle_range(scenario_index_);
    for (std::size_t obstacle = begin; obstacle < end; ++obstacle) {
        const std::size_t offset = 4 * obstacle;
        if (
            point.x > data::kObstacles[offset]
            && point.x < data::kObstacles[offset + 2]
            && point.y > data::kObstacles[offset + 1]
            && point.y < data::kObstacles[offset + 3]
        ) {
            return false;
        }
    }
    return true;
}

double Problem::constraint_violation(
    const std::vector<Point>& layout
) const {
    return violation_serial(*this, layout);
}

Evaluation Problem::evaluate(const std::vector<Point>& layout) const {
    return evaluate_serial(*this, layout);
}

Evaluation Problem::evaluate(
    const std::vector<Point>& layout,
    fode::PersistentExecutor& executor
) const {
    if (executor.thread_count() <= 1) {
        return evaluate_serial(*this, layout);
    }
    const double violation = violation_serial(*this, layout);
    if (violation > 1.0e-10) {
        Evaluation invalid;
        invalid.energy_cost = kInvalid;
        invalid.constraint_violation_m = violation;
        return invalid;
    }
    std::array<std::vector<double>, data::kDirections> outputs;
    executor.parallel_for(
        0,
        static_cast<int>(data::kDirections),
        [&](int direction) {
            outputs[static_cast<std::size_t>(direction)] =
                direction_outputs(
                    *this,
                    layout,
                    static_cast<std::size_t>(direction)
                );
        }
    );
    return finish_evaluation(*this, layout, outputs);
}

std::vector<Evaluation> Problem::evaluate_candidates(
    const std::vector<std::vector<Point>>& layouts,
    fode::PersistentExecutor& executor
) const {
    std::vector<Evaluation> values(layouts.size());
    if (layouts.size() <= 1U) {
        if (!layouts.empty()) {
            values[0] = evaluate(layouts[0], executor);
        }
        return values;
    }
    // A CMA-ES generation has only eight layouts, while Waffle exposes 20
    // logical CPUs.  Flattening layout x direction makes all cores useful and
    // also balances heterogeneous layout sizes without nested thread teams.
    std::vector<double> violations(layouts.size(), 0.0);
    std::vector<std::array<std::vector<double>, data::kDirections>> outputs(
        layouts.size()
    );
    for (std::size_t layout = 0; layout < layouts.size(); ++layout) {
        violations[layout] = violation_serial(*this, layouts[layout]);
    }
    const int tasks = static_cast<int>(
        layouts.size() * data::kDirections
    );
    executor.parallel_for(0, tasks, [&](int raw) {
        const std::size_t task = static_cast<std::size_t>(raw);
        const std::size_t layout = task / data::kDirections;
        const std::size_t direction = task % data::kDirections;
        if (violations[layout] <= 1.0e-10) {
            outputs[layout][direction] =
                direction_outputs(*this, layouts[layout], direction);
        }
    });
    for (std::size_t layout = 0; layout < layouts.size(); ++layout) {
        if (violations[layout] > 1.0e-10) {
            values[layout].energy_cost = kInvalid;
            values[layout].constraint_violation_m = violations[layout];
        } else {
            values[layout] = finish_evaluation(
                *this,
                layouts[layout],
                outputs[layout]
            );
        }
    }
    return values;
}

std::vector<std::string> algorithm_ids() {
    return {
        "t12_3s_mde",
        "t12_cmaes_geometric",
        "t12_sshh",
        "t12_goldman_lattice"
    };
}

RunResult run(
    const Problem& problem,
    const std::string& algorithm_id,
    std::uint64_t seed,
    std::uint64_t physical_fes_limit,
    int workers
) {
    const auto registered_algorithms = algorithm_ids();
    if (
        std::find(
            registered_algorithms.begin(),
            registered_algorithms.end(),
            algorithm_id
        ) == registered_algorithms.end()
    ) {
        throw std::invalid_argument("unknown T12 algorithm: " + algorithm_id);
    }
    const std::uint64_t limit =
        physical_fes_limit == 0 ? 2000 : physical_fes_limit;
    const auto end_to_end_start = Clock::now();
    SearchContext context(problem, limit, workers);
    const fode::CounterRng rng(seed);
    const auto algorithm_start = Clock::now();
    Candidate best;
    if (algorithm_id == "t12_3s_mde") {
        best = run_3s_mde(context, rng);
    } else if (algorithm_id == "t12_cmaes_geometric") {
        best = run_cmaes(context, rng);
    } else if (algorithm_id == "t12_sshh") {
        best = run_sshh(context, rng);
    } else {
        best = run_goldman(context);
    }
    const double algorithm_total = seconds_since(algorithm_start);
    RunResult result;
    result.algorithm_id = algorithm_id;
    result.problem_id = problem.id();
    result.best_layout = std::move(best.layout);
    result.best_evaluation = std::move(best.evaluation);
    result.seed = seed;
    result.physical_fes = context.fes;
    result.physical_fes_limit = limit;
    result.requested_workers = workers;
    const auto receipt = context.executor.work_receipt();
    result.observed_workers = receipt.distinct_participants;
    result.evaluator_seconds = context.evaluator_seconds;
    result.algorithm_seconds =
        std::max(0.0, algorithm_total - context.evaluator_seconds);
    result.end_to_end_seconds = seconds_since(end_to_end_start);
    result.scientific_hash = result_hash(
        algorithm_id,
        problem.scenario_index(),
        result.best_layout,
        result.best_evaluation
    );
    return result;
}

}  // namespace core99::t12
