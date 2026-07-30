/*
WFLOP IMPLEMENTATION FACT DECLARATION
Implementation unit: T22 DEBO, Gaussian AEP evaluator, and CPU-HPC execution
Paper DOI: 10.5194/wes-8-865-2023
Public source: paper-linked archive DOI 10.5281/zenodo.7125349
Missing information and reconstruction decisions:
include/core99/debo_t22.hpp
Method/problem semantic IDs: t22_debo_paper_reconstruction_v1;
t22_iea37_cs4_gaussian_aep_v1
Controlling contract: shared/contracts/core99_t22_iea37_cs4.json
Independent oracle: scripts/validate_core99_t22.py
HPC design: structure-of-arrays direction kernels for fixed layouts;
paper-DEBO candidate layouts distributed over a persistent all-core executor;
all selection and state commits occur in paper order after parallel evaluation
Claim boundary: academic declared reproduction, not author DEBO source
Last evidence-audit date: 2026-07-31
END WFLOP IMPLEMENTATION FACT DECLARATION
*/
#include "core99/debo_t22.hpp"

#include "core99/t22_iea37_data.hpp"
#include "fode/rng.hpp"

#include <algorithm>
#include <array>
#include <bit>
#include <chrono>
#include <cmath>
#include <cstddef>
#include <limits>
#include <numbers>
#include <numeric>
#include <stdexcept>
#include <utility>
#include <vector>

namespace core99::t22 {
namespace {

using Clock = std::chrono::steady_clock;

constexpr double kHoursPerYear = 365.0 * 24.0;
constexpr double kDistanceTolerance = 1.0e-9;
constexpr double kGridStep = 100.0;
constexpr double kMaximumLinkDistance = 990.0;
constexpr double kInitialNeighborhood = 1000.0;
constexpr double kMinimumNeighborhood = 5.0;
constexpr int kNeighborhoodHalfDivisions = 6;
constexpr double kNeighborhoodReduction = 0.75;

struct GridPoint {
    int ix = 0;
    int iy = 0;
    Point point;
};

double squared_distance(const Point& left, const Point& right) noexcept {
    const double dx = left.x - right.x;
    const double dy = left.y - right.y;
    return dx * dx + dy * dy;
}

double distance_to_segment(
    const Point& point,
    const Point& first,
    const Point& second
) noexcept {
    const double dx = second.x - first.x;
    const double dy = second.y - first.y;
    const double denominator = dx * dx + dy * dy;
    if (denominator <= 0.0) {
        return std::sqrt(squared_distance(point, first));
    }
    const double projection = std::clamp(
        (
            (point.x - first.x) * dx
            + (point.y - first.y) * dy
        ) / denominator,
        0.0,
        1.0
    );
    const Point closest{
        first.x + projection * dx,
        first.y + projection * dy
    };
    return std::sqrt(squared_distance(point, closest));
}

bool point_on_segment(
    const Point& point,
    const Point& first,
    const Point& second
) noexcept {
    return distance_to_segment(point, first, second) <= 1.0e-8;
}

std::size_t polygon_count() noexcept {
    return data::kBoundaryOffsets.size() - 1;
}

Point polygon_vertex(std::size_t vertex) noexcept {
    return {
        data::kBoundaryCoordinates[2 * vertex],
        data::kBoundaryCoordinates[2 * vertex + 1]
    };
}

bool inside_polygon(const Point& point, std::size_t polygon) noexcept {
    const std::size_t begin = data::kBoundaryOffsets[polygon];
    const std::size_t end = data::kBoundaryOffsets[polygon + 1];
    bool inside = false;
    for (
        std::size_t current = begin, previous = end - 1;
        current < end;
        previous = current++
    ) {
        const Point first = polygon_vertex(previous);
        const Point second = polygon_vertex(current);
        if (point_on_segment(point, first, second)) {
            return true;
        }
        const bool crosses =
            (first.y > point.y) != (second.y > point.y);
        if (crosses) {
            const double crossing_x =
                (second.x - first.x)
                * (point.y - first.y)
                / (second.y - first.y)
                + first.x;
            if (point.x < crossing_x) {
                inside = !inside;
            }
        }
    }
    return inside;
}

double distance_to_site(const Point& point) noexcept {
    double best = std::numeric_limits<double>::infinity();
    for (std::size_t polygon = 0; polygon < polygon_count(); ++polygon) {
        if (inside_polygon(point, polygon)) {
            return 0.0;
        }
        const std::size_t begin = data::kBoundaryOffsets[polygon];
        const std::size_t end = data::kBoundaryOffsets[polygon + 1];
        for (
            std::size_t current = begin, previous = end - 1;
            current < end;
            previous = current++
        ) {
            best = std::min(
                best,
                distance_to_segment(
                    point,
                    polygon_vertex(previous),
                    polygon_vertex(current)
                )
            );
        }
    }
    return best;
}

double power_w(double effective_speed) noexcept {
    if (
        effective_speed >= data::kCutIn
        && effective_speed < data::kRatedSpeed
    ) {
        const double fraction =
            (effective_speed - data::kCutIn)
            / (data::kRatedSpeed - data::kCutIn);
        return data::kRatedPower * fraction * fraction * fraction;
    }
    if (
        effective_speed >= data::kRatedSpeed
        && effective_speed < data::kCutOut
    ) {
        return data::kRatedPower;
    }
    return 0.0;
}

double direction_expected_power(
    const std::vector<Point>& layout,
    std::size_t direction
) {
    const double meteorological =
        270.0 - data::kDirectionDegrees[direction];
    const double radians =
        -meteorological * std::numbers::pi / 180.0;
    const double cosine = std::cos(radians);
    const double sine = std::sin(radians);
    const std::size_t turbines = layout.size();
    std::vector<double> downwind(turbines);
    std::vector<double> crosswind(turbines);
    for (std::size_t index = 0; index < turbines; ++index) {
        downwind[index] =
            layout[index].x * cosine - layout[index].y * sine;
        crosswind[index] =
            layout[index].x * sine + layout[index].y * cosine;
    }
    std::vector<double> loss(turbines, 0.0);
    for (std::size_t downstream = 0; downstream < turbines; ++downstream) {
        double squared_loss = 0.0;
        for (std::size_t upstream = 0; upstream < turbines; ++upstream) {
            const double dx =
                downwind[downstream] - downwind[upstream];
            if (!(dx > 0.0)) {
                continue;
            }
            const double dy =
                crosswind[downstream] - crosswind[upstream];
            const double sigma =
                data::kWakeExpansion * dx
                + data::kDiameter / std::sqrt(8.0);
            const double denominator =
                8.0 * sigma * sigma
                / (data::kDiameter * data::kDiameter);
            const double radical = std::max(
                0.0,
                1.0 - data::kThrustCoefficient / denominator
            );
            const double deficit =
                (1.0 - std::sqrt(radical))
                * std::exp(-0.5 * (dy / sigma) * (dy / sigma));
            squared_loss += deficit * deficit;
        }
        loss[downstream] = std::sqrt(squared_loss);
    }
    double expected_power = 0.0;
    const std::size_t speed_offset = direction * data::kSpeeds;
    for (std::size_t speed = 0; speed < data::kSpeeds; ++speed) {
        double farm_power = 0.0;
        for (std::size_t turbine = 0; turbine < turbines; ++turbine) {
            farm_power += power_w(
                data::kSpeedBins[speed] * (1.0 - loss[turbine])
            );
        }
        expected_power += farm_power
            * data::kConditionalSpeedFrequency[speed_offset + speed];
    }
    return expected_power * data::kDirectionFrequency[direction];
}

double ideal_expected_power() {
    double total = 0.0;
    for (
        std::size_t direction = 0;
        direction < data::kDirections;
        ++direction
    ) {
        double expected = 0.0;
        const std::size_t offset = direction * data::kSpeeds;
        for (std::size_t speed = 0; speed < data::kSpeeds; ++speed) {
            expected += power_w(data::kSpeedBins[speed])
                * data::kConditionalSpeedFrequency[offset + speed];
        }
        total += expected
            * data::kDirectionFrequency[direction]
            * static_cast<double>(data::kTurbines);
    }
    return total;
}

Evaluation evaluate_serial(
    const Problem& problem,
    const std::vector<Point>& layout
) {
    double expected_power = 0.0;
    for (
        std::size_t direction = 0;
        direction < data::kDirections;
        ++direction
    ) {
        expected_power += direction_expected_power(layout, direction);
    }
    const double aep = expected_power * kHoursPerYear / 1.0e6;
    const double ideal =
        ideal_expected_power() * kHoursPerYear / 1.0e6;
    return {
        aep,
        ideal > 0.0 ? 1.0 - aep / ideal : 0.0,
        problem.constraint_violation(layout)
    };
}

std::vector<Point> decode_positions(
    const std::array<double, 2 * data::kTurbines>& packed
) {
    std::vector<Point> result;
    result.reserve(data::kTurbines);
    for (std::size_t index = 0; index < data::kTurbines; ++index) {
        result.push_back({
            packed[2 * index],
            packed[2 * index + 1]
        });
    }
    return result;
}

std::vector<GridPoint> admissible_grid(const Problem& problem) {
    double minimum_x = std::numeric_limits<double>::infinity();
    double minimum_y = std::numeric_limits<double>::infinity();
    double maximum_x = -std::numeric_limits<double>::infinity();
    double maximum_y = -std::numeric_limits<double>::infinity();
    for (
        std::size_t vertex = 0;
        vertex < data::kBoundaryCoordinates.size() / 2;
        ++vertex
    ) {
        const Point point = polygon_vertex(vertex);
        minimum_x = std::min(minimum_x, point.x);
        minimum_y = std::min(minimum_y, point.y);
        maximum_x = std::max(maximum_x, point.x);
        maximum_y = std::max(maximum_y, point.y);
    }
    const int nx = static_cast<int>(
        std::floor((maximum_x - minimum_x) / kGridStep)
    );
    const int ny = static_cast<int>(
        std::floor((maximum_y - minimum_y) / kGridStep)
    );
    std::vector<GridPoint> grid;
    for (int ix = 0; ix <= nx; ++ix) {
        for (int iy = 0; iy <= ny; ++iy) {
            const Point point{
                minimum_x + static_cast<double>(ix) * kGridStep,
                minimum_y + static_cast<double>(iy) * kGridStep
            };
            if (problem.inside(point)) {
                grid.push_back({ix, iy, point});
            }
        }
    }
    return grid;
}

std::vector<bool> border_mask(const std::vector<GridPoint>& grid) {
    int maximum_x = 0;
    int maximum_y = 0;
    for (const GridPoint& point : grid) {
        maximum_x = std::max(maximum_x, point.ix);
        maximum_y = std::max(maximum_y, point.iy);
    }
    const int stride = maximum_y + 3;
    std::vector<bool> occupied(
        static_cast<std::size_t>((maximum_x + 3) * stride),
        false
    );
    auto key = [stride](int ix, int iy) {
        return static_cast<std::size_t>((ix + 1) * stride + iy + 1);
    };
    for (const GridPoint& point : grid) {
        occupied[key(point.ix, point.iy)] = true;
    }
    std::vector<bool> border(grid.size(), false);
    for (std::size_t index = 0; index < grid.size(); ++index) {
        for (int dx = -1; dx <= 1 && !border[index]; ++dx) {
            for (int dy = -1; dy <= 1; ++dy) {
                if (
                    !occupied[
                        key(grid[index].ix + dx, grid[index].iy + dy)
                    ]
                ) {
                    border[index] = true;
                    break;
                }
            }
        }
    }
    return border;
}

bool spacing_feasible(
    const std::vector<Point>& layout,
    const Point& candidate,
    std::size_t ignored = std::numeric_limits<std::size_t>::max()
) noexcept {
    const double required =
        data::kMinimumSpacing * data::kMinimumSpacing
        - kDistanceTolerance;
    for (std::size_t index = 0; index < layout.size(); ++index) {
        if (index != ignored
            && squared_distance(layout[index], candidate) < required) {
            return false;
        }
    }
    return true;
}

bool better_candidate(
    const Evaluation& left,
    const Point& left_point,
    const Evaluation& right,
    const Point& right_point
) noexcept {
    if (left.constraint_violation_m != right.constraint_violation_m) {
        return left.constraint_violation_m < right.constraint_violation_m;
    }
    if (left.aep_mwh != right.aep_mwh) {
        return left.aep_mwh > right.aep_mwh;
    }
    if (left_point.x != right_point.x) {
        return left_point.x < right_point.x;
    }
    return left_point.y < right_point.y;
}

std::uint64_t result_hash(
    const std::vector<Point>& layout,
    const Evaluation& evaluation
) noexcept {
    std::uint64_t hash = 1469598103934665603ULL;
    for (const Point& point : layout) {
        hash ^= std::bit_cast<std::uint64_t>(point.x);
        hash *= 1099511628211ULL;
        hash ^= std::bit_cast<std::uint64_t>(point.y);
        hash *= 1099511628211ULL;
    }
    hash ^= std::bit_cast<std::uint64_t>(evaluation.aep_mwh);
    hash *= 1099511628211ULL;
    return hash;
}

}  // namespace

Problem::Problem() = default;

const std::string& Problem::id() const noexcept {
    return id_;
}

int Problem::turbine_count() const noexcept {
    return static_cast<int>(data::kTurbines);
}

bool Problem::inside(const Point& point) const noexcept {
    for (std::size_t polygon = 0; polygon < polygon_count(); ++polygon) {
        if (inside_polygon(point, polygon)) {
            return true;
        }
    }
    return false;
}

double Problem::constraint_violation(
    const std::vector<Point>& layout
) const {
    double violation = 0.0;
    for (const Point& point : layout) {
        violation += distance_to_site(point);
    }
    for (std::size_t left = 0; left < layout.size(); ++left) {
        for (std::size_t right = left + 1; right < layout.size(); ++right) {
            const double distance = std::sqrt(
                squared_distance(layout[left], layout[right])
            );
            violation += std::max(
                0.0,
                data::kMinimumSpacing - distance
            );
        }
    }
    if (layout.size() != data::kTurbines) {
        violation += data::kMinimumSpacing
            * std::abs(
                static_cast<double>(layout.size())
                - static_cast<double>(data::kTurbines)
            );
    }
    return violation;
}

Evaluation Problem::evaluate(const std::vector<Point>& layout) const {
    return evaluate_serial(*this, layout);
}

Evaluation Problem::evaluate(
    const std::vector<Point>& layout,
    fode::PersistentExecutor& executor
) const {
    std::array<double, data::kDirections> expected{};
    executor.parallel_for(
        0,
        static_cast<int>(data::kDirections),
        [&](int direction) {
            expected[static_cast<std::size_t>(direction)] =
                direction_expected_power(
                    layout,
                    static_cast<std::size_t>(direction)
                );
        }
    );
    const double total =
        std::accumulate(expected.begin(), expected.end(), 0.0);
    const double aep = total * kHoursPerYear / 1.0e6;
    const double ideal =
        ideal_expected_power() * kHoursPerYear / 1.0e6;
    return {
        aep,
        ideal > 0.0 ? 1.0 - aep / ideal : 0.0,
        constraint_violation(layout)
    };
}

std::vector<Evaluation> Problem::evaluate_candidates(
    const std::vector<std::vector<Point>>& layouts,
    fode::PersistentExecutor& executor
) const {
    std::vector<Evaluation> result(layouts.size());
    executor.parallel_for(
        0,
        static_cast<int>(layouts.size()),
        [&](int index) {
            result[static_cast<std::size_t>(index)] =
                evaluate_serial(
                    *this,
                    layouts[static_cast<std::size_t>(index)]
                );
        }
    );
    return result;
}

std::vector<Point> Problem::author_base_layout() const {
    return decode_positions(data::kBasePositions);
}

std::vector<Point> Problem::author_debo_layout() const {
    return decode_positions(data::kDeboPositions);
}

double Problem::ideal_aep_mwh() const {
    return ideal_expected_power() * kHoursPerYear / 1.0e6;
}

RunResult run(
    const Problem& problem,
    std::uint64_t seed,
    std::uint64_t physical_fes_limit,
    int workers
) {
    if (workers <= 0) {
        throw std::invalid_argument("T22 workers must be positive");
    }
    const auto start = Clock::now();
    fode::PersistentExecutor executor(workers);
    executor.reset_work_receipt();
    const fode::CounterRng rng(seed);
    std::uint64_t physical_fes = 0;
    double evaluator_seconds = 0.0;
    bool limit_reached = false;

    auto evaluate_batch = [&](
        const std::vector<std::vector<Point>>& candidates
    ) -> std::vector<Evaluation> {
        if (
            physical_fes_limit != 0
            && physical_fes + candidates.size() > physical_fes_limit
        ) {
            limit_reached = true;
            return {};
        }
        const auto begin = Clock::now();
        auto values = problem.evaluate_candidates(candidates, executor);
        evaluator_seconds += std::chrono::duration<double>(
            Clock::now() - begin
        ).count();
        physical_fes += candidates.size();
        return values;
    };

    const std::vector<GridPoint> grid = admissible_grid(problem);
    if (grid.empty()) {
        throw std::runtime_error("T22 admissible grid is empty");
    }
    const std::vector<bool> border = border_mask(grid);
    std::vector<Point> layout;
    bool border_initialization = true;
    for (int restart = 0; restart < 2 && layout.empty(); ++restart) {
        std::vector<bool> available(grid.size(), true);
        std::vector<bool> potential(grid.size(), false);
        if (border_initialization) {
            potential = border;
        }
        std::size_t first = 0;
        for (std::size_t index = 1; index < grid.size(); ++index) {
            const int candidate_sum = grid[index].ix + grid[index].iy;
            const int incumbent_sum = grid[first].ix + grid[first].iy;
            if (
                candidate_sum > incumbent_sum
                || (
                    candidate_sum == incumbent_sum
                    && std::pair(grid[index].ix, grid[index].iy)
                        > std::pair(grid[first].ix, grid[first].iy)
                )
            ) {
                first = index;
            }
        }
        layout = {grid[first].point};
        while (layout.size() < data::kTurbines) {
            const Point last = layout.back();
            for (std::size_t index = 0; index < grid.size(); ++index) {
                if (
                    available[index]
                    && squared_distance(grid[index].point, last)
                        < data::kMinimumSpacing
                            * data::kMinimumSpacing
                            - kDistanceTolerance
                ) {
                    available[index] = false;
                }
            }
            for (std::size_t index = 0; index < grid.size(); ++index) {
                if (!available[index]) {
                    potential[index] = false;
                    continue;
                }
                const double distance =
                    squared_distance(grid[index].point, last);
                if (
                    distance >= data::kMinimumSpacing
                        * data::kMinimumSpacing
                        - kDistanceTolerance
                    && distance <= kMaximumLinkDistance
                        * kMaximumLinkDistance
                        + kDistanceTolerance
                ) {
                    potential[index] = true;
                }
            }
            std::vector<std::size_t> indices;
            std::vector<std::vector<Point>> candidates;
            for (std::size_t index = 0; index < grid.size(); ++index) {
                if (potential[index]) {
                    indices.push_back(index);
                    candidates.push_back(layout);
                    candidates.back().push_back(grid[index].point);
                }
            }
            if (candidates.empty()) {
                layout.clear();
                break;
            }
            const auto values = evaluate_batch(candidates);
            if (limit_reached) {
                break;
            }
            std::size_t best = 0;
            for (std::size_t index = 1; index < candidates.size(); ++index) {
                if (
                    better_candidate(
                        values[index],
                        grid[indices[index]].point,
                        values[best],
                        grid[indices[best]].point
                    )
                ) {
                    best = index;
                }
            }
            layout.push_back(grid[indices[best]].point);
        }
        if (layout.size() != data::kTurbines) {
            if (limit_reached) {
                break;
            }
            layout.clear();
            border_initialization = false;
        }
    }
    if (layout.empty()) {
        throw std::runtime_error("T22 DEBO greedy placement failed");
    }

    Evaluation current{};
    if (layout.size() == data::kTurbines && !limit_reached) {
        const auto values = evaluate_batch({layout});
        if (!values.empty()) {
            current = values.front();
        }
    }
    std::uint64_t pass = 0;
    for (
        double length = kInitialNeighborhood;
        length > kMinimumNeighborhood && !limit_reached;
        length *= kNeighborhoodReduction
    ) {
        bool changed = true;
        while (changed && !limit_reached) {
            changed = false;
            std::vector<std::size_t> order(data::kTurbines);
            std::iota(order.begin(), order.end(), 0);
            for (std::size_t index = order.size(); index > 1; --index) {
                const int selected = rng.integer(
                    0,
                    static_cast<int>(index),
                    pass,
                    2201,
                    index
                );
                std::swap(
                    order[index - 1],
                    order[static_cast<std::size_t>(selected)]
                );
            }
            ++pass;
            for (const std::size_t turbine : order) {
                std::vector<Point> candidate_points;
                std::vector<std::vector<Point>> candidates;
                const Point origin = layout[turbine];
                for (
                    int horizontal = 0;
                    horizontal <= 2 * kNeighborhoodHalfDivisions;
                    ++horizontal
                ) {
                    for (
                        int vertical = 0;
                        vertical <= 2 * kNeighborhoodHalfDivisions;
                        ++vertical
                    ) {
                        const Point point{
                            origin.x
                                + (
                                    static_cast<double>(horizontal)
                                    / kNeighborhoodHalfDivisions
                                    - 1.0
                                ) * length,
                            origin.y
                                + (
                                    static_cast<double>(vertical)
                                    / kNeighborhoodHalfDivisions
                                    - 1.0
                                ) * length
                        };
                        if (
                            squared_distance(point, origin)
                                <= kDistanceTolerance
                            || !problem.inside(point)
                            || !spacing_feasible(layout, point, turbine)
                        ) {
                            continue;
                        }
                        candidate_points.push_back(point);
                        candidates.push_back(layout);
                        candidates.back()[turbine] = point;
                    }
                }
                if (candidates.empty()) {
                    continue;
                }
                const auto values = evaluate_batch(candidates);
                if (limit_reached) {
                    break;
                }
                std::size_t best = 0;
                for (std::size_t index = 1; index < candidates.size(); ++index) {
                    if (
                        better_candidate(
                            values[index],
                            candidate_points[index],
                            values[best],
                            candidate_points[best]
                        )
                    ) {
                        best = index;
                    }
                }
                if (
                    values[best].constraint_violation_m <= 1.0e-8
                    && values[best].aep_mwh > current.aep_mwh
                ) {
                    layout[turbine] = candidate_points[best];
                    current = values[best];
                    changed = true;
                }
            }
        }
    }

    if (current.aep_mwh == 0.0) {
        const auto begin = Clock::now();
        current = problem.evaluate(layout, executor);
        evaluator_seconds += std::chrono::duration<double>(
            Clock::now() - begin
        ).count();
        ++physical_fes;
    }
    const double end_to_end = std::chrono::duration<double>(
        Clock::now() - start
    ).count();
    const auto receipt = executor.work_receipt();
    const std::uint64_t scientific_hash =
        result_hash(layout, current);
    return {
        problem.id(),
        std::move(layout),
        current,
        seed,
        physical_fes,
        !limit_reached,
        workers,
        receipt.distinct_participants,
        evaluator_seconds,
        std::max(0.0, end_to_end - evaluator_seconds),
        end_to_end,
        scientific_hash
    };
}

}  // namespace core99::t22
