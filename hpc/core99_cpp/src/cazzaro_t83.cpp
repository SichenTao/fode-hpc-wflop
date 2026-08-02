/*
WFLOP IMPLEMENTATION FACT DECLARATION
Implementation unit: T83 pure-C++ three-scale WFASP and CPU-HPC implementation
Paper DOI: 10.1016/j.apenergy.2022.118830
Public source, missing information, declared completions, semantic IDs, HPC
design, controlling contract and claim boundary:
include/core99/cazzaro_t83.hpp
Claim boundary: academic flexible reconstruction, not author numerical replay.
Last evidence-audit date: 2026-07-31
END WFLOP IMPLEMENTATION FACT DECLARATION
*/
#include "core99/cazzaro_t83.hpp"

#include "core99/cazzaro_t31.hpp"
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
#include <unordered_map>
#include <utility>
#include <vector>

namespace core99::t83 {
namespace {

using Clock = std::chrono::steady_clock;
constexpr int kTurbines = 100;
constexpr double kInstalledCapacityMw = 1500.0;
constexpr double kRotorDiameterM = 240.0;
constexpr double kMinimumSpacingM = 5.0 * kRotorDiameterM;
constexpr double kMesoRadiusM = 15000.0;
constexpr double kMaximumAreaKm2 = 500.0;
constexpr double kMaximumPta = 5.0;
constexpr double kMinimumDensityMwKm2 = 3.0;
constexpr double kEnergyPriceEurMwh = 39.59;
constexpr double kDiscountRate = 0.06;
constexpr int kLifetimeYears = 25;
constexpr double kExportCableEurM = 1500.0;
constexpr double kInterarrayMeanEurM = 288.0;
constexpr double kCandidateCellM = 800.0;
constexpr double kConstraintPenaltyMeur = 1.0e5;
constexpr std::array<double, 5> kAspectRatios{0.5, 0.75, 1.0, 1.5, 2.0};
constexpr std::array<double, 6> kOrientationsDeg{0.0, 30.0, 60.0, 90.0,
                                                 120.0, 150.0};
constexpr std::array<double, 3> kVnsRadiiM{
    3.0 * kRotorDiameterM,
    5.0 * kRotorDiameterM,
    8.0 * kRotorDiameterM,
};

double elapsed(const Clock::time_point& start) {
    return std::chrono::duration<double>(Clock::now() - start).count();
}

double annuity_factor() {
    double result = 0.0;
    for (int year = 0; year <= kLifetimeYears; ++year) {
        result += 1.0 / std::pow(1.0 + kDiscountRate, year);
    }
    return result;
}

double distance(const Point& left, const Point& right) {
    return std::hypot(left.x_m - right.x_m, left.y_m - right.y_m);
}

double cross(const Point& origin, const Point& first, const Point& second) {
    return (first.x_m - origin.x_m) * (second.y_m - origin.y_m)
        - (first.y_m - origin.y_m) * (second.x_m - origin.x_m);
}

std::vector<Point> convex_hull(std::vector<Point> points) {
    std::sort(points.begin(), points.end(), [](const Point& left,
                                                const Point& right) {
        if (left.x_m != right.x_m) return left.x_m < right.x_m;
        return left.y_m < right.y_m;
    });
    points.erase(std::unique(points.begin(), points.end(), [](const Point& a,
                                                              const Point& b) {
        return a.x_m == b.x_m && a.y_m == b.y_m;
    }), points.end());
    if (points.size() <= 2U) return points;
    std::vector<Point> result(2U * points.size());
    std::size_t size = 0;
    for (const auto& point : points) {
        while (size >= 2U
               && cross(result[size - 2U], result[size - 1U], point) <= 0.0) {
            --size;
        }
        result[size++] = point;
    }
    const std::size_t lower = size + 1U;
    for (auto iterator = points.rbegin(); iterator != points.rend(); ++iterator) {
        while (size >= lower
               && cross(result[size - 2U], result[size - 1U], *iterator) <= 0.0) {
            --size;
        }
        result[size++] = *iterator;
    }
    result.resize(size - 1U);
    return result;
}

std::pair<double, double> hull_area_perimeter(const std::vector<Point>& hull) {
    if (hull.size() < 3U) return {0.0, 0.0};
    double doubled_area = 0.0;
    double perimeter = 0.0;
    for (std::size_t index = 0; index < hull.size(); ++index) {
        const auto& current = hull[index];
        const auto& next = hull[(index + 1U) % hull.size()];
        doubled_area += current.x_m * next.y_m - current.y_m * next.x_m;
        perimeter += distance(current, next);
    }
    return {0.5 * std::abs(doubled_area) / 1.0e6, perimeter / 1000.0};
}

bool point_in_convex_polygon(const Point& point, const std::vector<Point>& hull) {
    if (hull.size() < 3U) return false;
    double sign = 0.0;
    for (std::size_t index = 0; index < hull.size(); ++index) {
        const double value = cross(hull[index], hull[(index + 1U) % hull.size()],
                                   point);
        if (std::abs(value) <= 1.0e-8) continue;
        if (sign == 0.0) sign = value;
        else if (sign * value < 0.0) return false;
    }
    return true;
}

double minimum_spanning_tree_m(const std::vector<Point>& points) {
    if (points.size() < 2U) return 0.0;
    std::vector<double> best(points.size(), std::numeric_limits<double>::infinity());
    std::vector<bool> used(points.size(), false);
    best[0] = 0.0;
    double total = 0.0;
    for (std::size_t step = 0; step < points.size(); ++step) {
        std::size_t current = points.size();
        for (std::size_t index = 0; index < points.size(); ++index) {
            if (!used[index]
                && (current == points.size() || best[index] < best[current])) {
                current = index;
            }
        }
        if (current == points.size()) break;
        used[current] = true;
        total += best[current];
        for (std::size_t next = 0; next < points.size(); ++next) {
            if (!used[next]) {
                best[next] = std::min(best[next], distance(points[current], points[next]));
            }
        }
    }
    return total;
}

double score(const Evaluation& evaluation) {
    return evaluation.npv_meur - kConstraintPenaltyMeur * (
        evaluation.spacing_violation_m / kMinimumSpacingM
        + evaluation.area_violation_km2 / kMaximumAreaKm2
        + evaluation.pta_violation
        + evaluation.density_violation_mw_km2
    );
}

std::uint64_t mix_hash(std::uint64_t hash, const std::uint64_t value) {
    hash ^= value;
    hash *= 1099511628211ULL;
    return hash;
}

std::uint64_t result_hash(
    const std::vector<int>& shape,
    const std::vector<int>& rectangle,
    const Evaluation& shape_evaluation,
    const Evaluation& rectangle_evaluation,
    const std::uint64_t shape_cycles,
    const std::uint64_t rectangle_cycles
) {
    std::uint64_t hash = 1469598103934665603ULL;
    for (const int position : shape) hash = mix_hash(hash, position);
    for (const int position : rectangle) hash = mix_hash(hash, position);
    hash = mix_hash(hash, std::bit_cast<std::uint64_t>(shape_evaluation.npv_meur));
    hash = mix_hash(hash, std::bit_cast<std::uint64_t>(rectangle_evaluation.npv_meur));
    hash = mix_hash(hash, shape_cycles);
    return mix_hash(hash, rectangle_cycles);
}

}  // namespace

class Problem::Impl {
public:
    Impl(std::filesystem::path root, const char seed_role, const int workers)
        : definition(case_for(seed_role)),
          wake_problem(
              std::move(root),
              std::string("t31_official_")
                  + static_cast<char>(definition.proxy_site - 'A' + 'a'),
              t31::FoundationMode::none,
              workers
          ) {
        const auto candidate_start = Clock::now();
        source_count = wake_problem.info().available_positions;
        source_preprocessing = wake_problem.preprocessing_seconds();
        source_fingerprint = wake_problem.matrix_fingerprint();
        const auto& source = wake_problem.positions();
        double minimum_x = std::numeric_limits<double>::infinity();
        double minimum_y = std::numeric_limits<double>::infinity();
        for (const auto& position : source) {
            minimum_x = std::min(minimum_x, position.x_m);
            minimum_y = std::min(minimum_y, position.y_m);
        }
        struct BinChoice {
            int source_index = -1;
            double squared_distance = std::numeric_limits<double>::infinity();
        };
        std::unordered_map<std::uint64_t, BinChoice> bins;
        bins.reserve(source.size());
        for (int index = 0; index < static_cast<int>(source.size()); ++index) {
            const auto& point = source[static_cast<std::size_t>(index)];
            const auto column = static_cast<std::uint32_t>(
                std::floor((point.x_m - minimum_x) / kCandidateCellM)
            );
            const auto row = static_cast<std::uint32_t>(
                std::floor((point.y_m - minimum_y) / kCandidateCellM)
            );
            const std::uint64_t key = (static_cast<std::uint64_t>(row) << 32U)
                | static_cast<std::uint64_t>(column);
            const double center_x = minimum_x
                + (static_cast<double>(column) + 0.5) * kCandidateCellM;
            const double center_y = minimum_y
                + (static_cast<double>(row) + 0.5) * kCandidateCellM;
            const double squared = std::pow(point.x_m - center_x, 2.0)
                + std::pow(point.y_m - center_y, 2.0);
            auto& choice = bins[key];
            if (squared < choice.squared_distance
                || (squared == choice.squared_distance
                    && index < choice.source_index)) {
                choice = {index, squared};
            }
        }
        source_indices.reserve(bins.size());
        for (const auto& [key, choice] : bins) {
            (void)key;
            source_indices.push_back(choice.source_index);
        }
        std::sort(source_indices.begin(), source_indices.end());
        points.reserve(source_indices.size());
        diagonal_aep.reserve(source_indices.size());
        foundation_meur.reserve(source_indices.size());
        for (const int index : source_indices) {
            const auto& position = source[static_cast<std::size_t>(index)];
            points.push_back({position.x_m, position.y_m});
            diagonal_aep.push_back(wake_problem.diagonal_value(index));
            foundation_meur.push_back(position.foundation_eur / 1.0e6);
        }
        legal_area_km2 = hull_area_perimeter(convex_hull(points)).first;
        candidate_preprocessing = elapsed(candidate_start);
        const auto matrix_start = Clock::now();
        const int count = static_cast<int>(points.size());
        pair_matrix.assign(static_cast<std::size_t>(count) * count, 0.0F);
        fode::PersistentExecutor executor(workers);
        executor.parallel_for(0, count, [&](const int first) {
            for (int second = first + 1; second < count; ++second) {
                const double value = wake_problem.symmetric_pair_value(
                    source_indices[static_cast<std::size_t>(first)],
                    source_indices[static_cast<std::size_t>(second)]
                );
                pair_matrix[static_cast<std::size_t>(first) * count
                    + static_cast<std::size_t>(second)] = static_cast<float>(value);
                pair_matrix[static_cast<std::size_t>(second) * count
                    + static_cast<std::size_t>(first)] = static_cast<float>(value);
            }
        });
        pair_matrix_seconds = elapsed(matrix_start);
        if (points.size() < static_cast<std::size_t>(kTurbines)) {
            throw std::runtime_error("T83 proxy field has fewer than 100 candidates");
        }
    }

    static CaseDefinition case_for(const char role) {
        for (const auto& item : paper_cases()) {
            if (item.seed_role == role) return item;
        }
        throw std::invalid_argument("T83 seed role must be A through H");
    }

    double pair(const int first, const int second) const {
        const std::size_t count = points.size();
        return pair_matrix[static_cast<std::size_t>(first) * count
            + static_cast<std::size_t>(second)];
    }

    std::vector<Point> selected_points(const std::vector<int>& selected) const {
        std::vector<Point> result;
        result.reserve(selected.size());
        for (const int index : selected) {
            if (index < 0 || index >= static_cast<int>(points.size())) {
                throw std::invalid_argument("T83 candidate index out of range");
            }
            result.push_back(points[static_cast<std::size_t>(index)]);
        }
        return result;
    }

    Evaluation evaluate(const std::vector<int>& selected) const {
        Evaluation result;
        if (selected.empty()) return result;
        const auto selected_geometry = selected_points(selected);
        result.minimum_spacing_m = std::numeric_limits<double>::infinity();
        for (std::size_t first = 0; first < selected.size(); ++first) {
            const int first_index = selected[first];
            result.aep_mwh += diagonal_aep[static_cast<std::size_t>(first_index)];
            result.foundation_cost_meur += foundation_meur[
                static_cast<std::size_t>(first_index)
            ];
            for (std::size_t second = first + 1U; second < selected.size(); ++second) {
                const double spacing = distance(
                    selected_geometry[first], selected_geometry[second]
                );
                result.minimum_spacing_m = std::min(result.minimum_spacing_m,
                                                     spacing);
                result.spacing_violation_m += std::max(0.0,
                    kMinimumSpacingM - spacing);
                if (spacing >= kMinimumSpacingM - 1.0e-9) {
                    result.aep_mwh += pair(first_index, selected[second]);
                }
            }
        }
        if (selected.size() == 1U) {
            result.minimum_spacing_m = std::numeric_limits<double>::infinity();
        }
        const auto hull = convex_hull(selected_geometry);
        const auto [area, perimeter] = hull_area_perimeter(hull);
        result.area_km2 = area;
        result.perimeter_km = perimeter;
        result.perimeter_to_sqrt_area = area > 0.0
            ? perimeter / std::sqrt(area)
            : std::numeric_limits<double>::infinity();
        const double capacity = 15.0 * static_cast<double>(selected.size());
        result.density_mw_km2 = area > 0.0
            ? capacity / area
            : std::numeric_limits<double>::infinity();
        result.area_violation_km2 = std::max(0.0, area - kMaximumAreaKm2);
        result.pta_violation = selected.size() >= 3U
            ? std::max(0.0, result.perimeter_to_sqrt_area - kMaximumPta) : 0.0;
        result.density_violation_mw_km2 = area > 0.0
            ? std::max(0.0, kMinimumDensityMwKm2 - result.density_mw_km2)
            : 0.0;
        result.interarray_cable_cost_meur = minimum_spanning_tree_m(
            selected_geometry
        ) * kInterarrayMeanEurM / 1.0e6;
        Point centroid{};
        for (const auto& point : selected_geometry) {
            centroid.x_m += point.x_m;
            centroid.y_m += point.y_m;
        }
        centroid.x_m /= static_cast<double>(selected_geometry.size());
        centroid.y_m /= static_cast<double>(selected_geometry.size());
        // Target shore and export-route arrays are absent. The latitude role
        // controls a fixed, layout-responsive proxy distance; its constant
        // portion is absorbed by the single declared Table-1 calibration.
        const double nominal_shore_km = 45.0
            + 8.0 * static_cast<double>(definition.seed_role - 'A');
        const double centroid_displacement_km = distance(centroid, field_centroid())
            / 1000.0;
        result.export_cable_cost_meur = (nominal_shore_km
            + 0.05 * centroid_displacement_km) * 1000.0
            * kExportCableEurM / 1.0e6;
        result.fixed_cost_offset_meur = fixed_cost_offset_meur;
        const double revenue_meur = result.aep_mwh * kEnergyPriceEurMwh
            * annuity_factor() / 1.0e6;
        result.npv_meur = revenue_meur - result.foundation_cost_meur
            - result.interarray_cable_cost_meur
            - result.export_cable_cost_meur - fixed_cost_offset_meur;
        result.feasible = selected.size() == static_cast<std::size_t>(kTurbines)
            && result.spacing_violation_m <= 1.0e-6
            && result.area_violation_km2 <= 1.0e-9
            && result.pta_violation <= 1.0e-9
            && result.density_violation_mw_km2 <= 1.0e-9;
        return result;
    }

    Point field_centroid() const {
        Point result{};
        for (const auto& point : points) {
            result.x_m += point.x_m;
            result.y_m += point.y_m;
        }
        result.x_m /= static_cast<double>(points.size());
        result.y_m /= static_cast<double>(points.size());
        return result;
    }

    Point seed_center() const {
        Point result = field_centroid();
        const int role = definition.seed_role - 'A';
        result.x_m += static_cast<double>(role % 3 - 1) * 2000.0;
        result.y_m += static_cast<double>(role / 3 - 1) * 2000.0;
        return result;
    }

    CaseDefinition definition;
    t31::Problem wake_problem;
    int source_count = 0;
    std::vector<int> source_indices;
    std::vector<Point> points;
    std::vector<double> diagonal_aep;
    std::vector<double> foundation_meur;
    std::vector<float> pair_matrix;
    double source_preprocessing = 0.0;
    double candidate_preprocessing = 0.0;
    double pair_matrix_seconds = 0.0;
    double fixed_cost_offset_meur = 0.0;
    double legal_area_km2 = 0.0;
    std::uint64_t source_fingerprint = 0;
};

namespace {

std::vector<int> rectangle_layout(
    const Problem::Impl& problem,
    const Point center,
    const double orientation_deg,
    const double aspect_ratio
) {
    constexpr int rows = 10;
    constexpr int columns = 10;
    const double target_area_m2 = std::min(
        480.0,
        std::max(120.0, 0.85 * problem.legal_area_km2)
    ) * 1.0e6;
    const double width = std::sqrt(target_area_m2 * aspect_ratio);
    const double height = target_area_m2 / width;
    const double angle = orientation_deg * std::numbers::pi / 180.0;
    const double cosine = std::cos(angle);
    const double sine = std::sin(angle);
    struct Ranked {
        int candidate = 0;
        double lattice_distance = 0.0;
    };
    std::vector<Ranked> ranked;
    ranked.reserve(problem.points.size());
    for (int candidate = 0;
         candidate < static_cast<int>(problem.points.size()); ++candidate) {
        const auto& point = problem.points[static_cast<std::size_t>(candidate)];
        const double dx = point.x_m - center.x_m;
        const double dy = point.y_m - center.y_m;
        const double local_x = dx * cosine + dy * sine;
        const double local_y = -dx * sine + dy * cosine;
        const double column_real = (local_x / width + 0.5) * (columns - 1);
        const double row_real = (local_y / height + 0.5) * (rows - 1);
        const double column = std::clamp(std::round(column_real), 0.0,
                                         static_cast<double>(columns - 1));
        const double row = std::clamp(std::round(row_real), 0.0,
                                      static_cast<double>(rows - 1));
        const double ideal_x = width * (column / (columns - 1) - 0.5);
        const double ideal_y = height * (row / (rows - 1) - 0.5);
        const double outside = std::max(0.0, std::abs(local_x) - 0.5 * width)
            + std::max(0.0, std::abs(local_y) - 0.5 * height);
        ranked.push_back({
            candidate,
            std::hypot(local_x - ideal_x, local_y - ideal_y) + 10.0 * outside,
        });
    }
    std::sort(ranked.begin(), ranked.end(), [](const Ranked& left,
                                                const Ranked& right) {
        if (left.lattice_distance != right.lattice_distance) {
            return left.lattice_distance < right.lattice_distance;
        }
        return left.candidate < right.candidate;
    });
    std::vector<int> selected;
    selected.reserve(kTurbines);
    for (const auto& item : ranked) {
        bool spacing_ok = true;
        for (const int existing : selected) {
            if (distance(problem.points[static_cast<std::size_t>(item.candidate)],
                         problem.points[static_cast<std::size_t>(existing)])
                < kMinimumSpacingM - 1.0e-9) {
                spacing_ok = false;
                break;
            }
        }
        if (!spacing_ok) continue;
        selected.push_back(item.candidate);
        if (selected.size() == static_cast<std::size_t>(kTurbines)) break;
    }
    if (selected.size() != static_cast<std::size_t>(kTurbines)) return {};
    return selected;
}

struct MacroResult {
    std::vector<int> positions;
    Evaluation evaluation;
    int attempted = 0;
};

MacroResult macro_screen(
    const Problem::Impl& problem,
    fode::PersistentExecutor& executor,
    const int cell_axis
) {
    const Point center = problem.seed_center();
    struct Specification {
        Point center;
        double orientation = 0.0;
        double aspect = 1.0;
    };
    std::vector<Specification> specifications;
    constexpr double macro_cell_m = 4000.0;
    for (int row = 0; row < cell_axis; ++row) {
        for (int column = 0; column < cell_axis; ++column) {
            const double offset_x = (column - 0.5 * (cell_axis - 1)) * macro_cell_m;
            const double offset_y = (row - 0.5 * (cell_axis - 1)) * macro_cell_m;
            for (const double orientation : kOrientationsDeg) {
                for (const double aspect : kAspectRatios) {
                    specifications.push_back({
                        {center.x_m + offset_x, center.y_m + offset_y},
                        orientation,
                        aspect,
                    });
                }
            }
        }
    }
    struct Candidate {
        std::vector<int> positions;
        Evaluation evaluation;
        double score = -std::numeric_limits<double>::infinity();
    };
    std::vector<Candidate> candidates(specifications.size());
    executor.parallel_for(0, static_cast<int>(specifications.size()),
        [&](const int index) {
            const auto& specification = specifications[static_cast<std::size_t>(index)];
            auto positions = rectangle_layout(
                problem, specification.center, specification.orientation,
                specification.aspect
            );
            if (positions.size() != static_cast<std::size_t>(kTurbines)) return;
            const auto evaluation = problem.evaluate(positions);
            candidates[static_cast<std::size_t>(index)] = {
                std::move(positions), evaluation, score(evaluation)
            };
        });
    int best = -1;
    for (int index = 0; index < static_cast<int>(candidates.size()); ++index) {
        if (candidates[static_cast<std::size_t>(index)].positions.empty()) continue;
        if (best < 0 || candidates[static_cast<std::size_t>(index)].score
                > candidates[static_cast<std::size_t>(best)].score) {
            best = index;
        }
    }
    if (best < 0) throw std::runtime_error("T83 macro screen found no layout");
    return {
        std::move(candidates[static_cast<std::size_t>(best)].positions),
        candidates[static_cast<std::size_t>(best)].evaluation,
        static_cast<int>(specifications.size()),
    };
}

struct MesoResult {
    std::vector<int> positions;
    Evaluation evaluation;
    std::uint64_t candidate_evaluations = 0;
};

MesoResult meso_construct(
    const Problem::Impl& problem,
    fode::PersistentExecutor& executor,
    const std::vector<int>& feasible_reserve
) {
    if (feasible_reserve.size() != static_cast<std::size_t>(kTurbines)) {
        throw std::invalid_argument("T83 meso reserve must contain 100 positions");
    }
    const Point center = problem.seed_center();
    int first = feasible_reserve.front();
    for (const int candidate : feasible_reserve) {
        if (distance(problem.points[static_cast<std::size_t>(candidate)], center)
            < distance(problem.points[static_cast<std::size_t>(first)], center)) {
            first = candidate;
        }
    }
    std::vector<int> selected{first};
    std::vector<bool> used(problem.points.size(), false);
    std::vector<bool> reserve_available(problem.points.size(), false);
    for (const int candidate : feasible_reserve) {
        reserve_available[static_cast<std::size_t>(candidate)] = true;
    }
    used[static_cast<std::size_t>(first)] = true;
    for (const int candidate : feasible_reserve) {
        if (candidate == first
            || distance(problem.points[static_cast<std::size_t>(candidate)],
                        problem.points[static_cast<std::size_t>(first)])
                < kMinimumSpacingM - 1.0e-9) {
            reserve_available[static_cast<std::size_t>(candidate)] = false;
        }
    }
    std::vector<double> marginal_aep = problem.diagonal_aep;
    for (int candidate = 0; candidate < static_cast<int>(problem.points.size());
         ++candidate) {
        marginal_aep[static_cast<std::size_t>(candidate)] += problem.pair(candidate, first);
    }
    double current_aep = problem.diagonal_aep[static_cast<std::size_t>(first)];
    double current_foundation = problem.foundation_meur[static_cast<std::size_t>(first)];
    std::uint64_t evaluated = 0;
    while (selected.size() < static_cast<std::size_t>(kTurbines)) {
        std::vector<double> values(problem.points.size(),
            -std::numeric_limits<double>::infinity());
        executor.parallel_for(0, static_cast<int>(problem.points.size()),
            [&](const int candidate) {
                if (used[static_cast<std::size_t>(candidate)]) return;
                bool frontier = false;
                for (const int existing : selected) {
                    const double spacing = distance(
                        problem.points[static_cast<std::size_t>(candidate)],
                        problem.points[static_cast<std::size_t>(existing)]
                    );
                    if (spacing < kMinimumSpacingM - 1.0e-9) return;
                    frontier = frontier || spacing <= kMesoRadiusM;
                }
                if (!frontier) return;
                const int positions_needed = kTurbines
                    - static_cast<int>(selected.size()) - 1;
                int compatible_reserve = 0;
                for (const int reserve : feasible_reserve) {
                    if (!reserve_available[static_cast<std::size_t>(reserve)]
                        || reserve == candidate) continue;
                    if (distance(
                            problem.points[static_cast<std::size_t>(candidate)],
                            problem.points[static_cast<std::size_t>(reserve)]
                        ) >= kMinimumSpacingM - 1.0e-9) {
                        ++compatible_reserve;
                    }
                }
                if (compatible_reserve < positions_needed) return;
                std::vector<Point> geometry;
                geometry.reserve(selected.size() + 1U);
                for (const int index : selected) {
                    geometry.push_back(problem.points[static_cast<std::size_t>(index)]);
                }
                geometry.push_back(problem.points[static_cast<std::size_t>(candidate)]);
                const auto hull = convex_hull(std::move(geometry));
                const auto [area, perimeter] = hull_area_perimeter(hull);
                const double pta = area > 0.0 ? perimeter / std::sqrt(area) : 0.0;
                const double capacity = 15.0 * static_cast<double>(selected.size() + 1U);
                const double density = area > 0.0 ? capacity / area
                                                  : std::numeric_limits<double>::infinity();
                const double revenue = (current_aep
                    + marginal_aep[static_cast<std::size_t>(candidate)])
                    * kEnergyPriceEurMwh * annuity_factor() / 1.0e6;
                const double foundation = current_foundation
                    + problem.foundation_meur[static_cast<std::size_t>(candidate)];
                const double violation = std::max(0.0, area - kMaximumAreaKm2)
                        / kMaximumAreaKm2
                    + (selected.size() + 1U >= 3U
                        ? std::max(0.0, pta - kMaximumPta) : 0.0)
                    + (area > 0.0
                        ? std::max(0.0, kMinimumDensityMwKm2 - density) : 0.0);
                values[static_cast<std::size_t>(candidate)] = revenue - foundation
                    - kConstraintPenaltyMeur * violation;
            });
        int best = -1;
        for (int candidate = 0; candidate < static_cast<int>(values.size());
             ++candidate) {
            if (std::isfinite(values[static_cast<std::size_t>(candidate)])) {
                ++evaluated;
                if (best < 0 || values[static_cast<std::size_t>(candidate)]
                        > values[static_cast<std::size_t>(best)]) {
                    best = candidate;
                }
            }
        }
        if (best < 0) throw std::runtime_error("T83 meso construction stalled");
        current_aep += marginal_aep[static_cast<std::size_t>(best)];
        current_foundation += problem.foundation_meur[static_cast<std::size_t>(best)];
        used[static_cast<std::size_t>(best)] = true;
        for (const int reserve : feasible_reserve) {
            if (reserve == best
                || distance(problem.points[static_cast<std::size_t>(best)],
                            problem.points[static_cast<std::size_t>(reserve)])
                    < kMinimumSpacingM - 1.0e-9) {
                reserve_available[static_cast<std::size_t>(reserve)] = false;
            }
        }
        selected.push_back(best);
        executor.parallel_for(0, static_cast<int>(problem.points.size()),
            [&](const int candidate) {
                marginal_aep[static_cast<std::size_t>(candidate)]
                    += problem.pair(candidate, best);
            });
    }
    return {selected, problem.evaluate(selected), evaluated};
}

struct MicroResult {
    std::vector<int> positions;
    Evaluation evaluation;
    std::uint64_t cycles = 0;
    std::uint64_t candidate_evaluations = 0;
    double seconds = 0.0;
};

MicroResult micro_optimize(
    const Problem::Impl& problem,
    std::vector<int> initial,
    fode::PersistentExecutor& executor,
    const fode::CounterRng& rng,
    const std::uint64_t stream,
    const double time_limit,
    const std::uint64_t fixed_cycles
) {
    const auto started = Clock::now();
    const auto boundary = convex_hull(problem.selected_points(initial));
    std::vector<int> current = initial;
    Evaluation current_evaluation = problem.evaluate(current);
    std::vector<int> best = current;
    Evaluation best_evaluation = current_evaluation;
    std::uint64_t cycles = 0;
    std::uint64_t candidate_evaluations = 0;
    int neighborhood = 0;
    auto continue_search = [&] {
        if (fixed_cycles > 0) return cycles < fixed_cycles;
        return elapsed(started) < time_limit;
    };
    while (continue_search()) {
        std::vector<bool> occupied(problem.points.size(), false);
        for (const int position : current) {
            occupied[static_cast<std::size_t>(position)] = true;
        }
        struct Move {
            int slot = 0;
            int candidate = 0;
            double key = 0.0;
        };
        std::vector<Move> alternatives;
        alternatives.reserve(1024);
        std::vector<std::vector<Move>> slot_moves(kTurbines);
        executor.parallel_for(0, kTurbines, [&](const int slot) {
            const int old_position = current[static_cast<std::size_t>(slot)];
            auto& local = slot_moves[static_cast<std::size_t>(slot)];
            local.reserve(16);
            for (int candidate = 0;
                 candidate < static_cast<int>(problem.points.size()); ++candidate) {
                if (candidate == old_position
                    || occupied[static_cast<std::size_t>(candidate)]) continue;
                const auto& point = problem.points[static_cast<std::size_t>(candidate)];
                if (!point_in_convex_polygon(point, boundary)) continue;
                if (distance(
                        point,
                        problem.points[static_cast<std::size_t>(old_position)]
                    ) > kVnsRadiiM[static_cast<std::size_t>(neighborhood)]) {
                    continue;
                }
                bool feasible = true;
                for (int other = 0; other < kTurbines; ++other) {
                    if (other == slot) continue;
                    if (distance(point, problem.points[static_cast<std::size_t>(
                            current[static_cast<std::size_t>(other)])])
                        < kMinimumSpacingM - 1.0e-9) {
                        feasible = false;
                        break;
                    }
                }
                if (feasible) local.push_back({
                    slot,
                    candidate,
                    rng.uniform(cycles, stream, neighborhood, slot, candidate),
                });
            }
        });
        for (auto& local : slot_moves) {
            alternatives.insert(
                alternatives.end(), local.begin(), local.end()
            );
        }
        std::sort(alternatives.begin(), alternatives.end(), [](const Move& left,
                                                                const Move& right) {
            if (left.key != right.key) return left.key < right.key;
            if (left.slot != right.slot) return left.slot < right.slot;
            return left.candidate < right.candidate;
        });
        if (alternatives.size() > 128U) alternatives.resize(128U);
        std::vector<double> deltas(alternatives.size(),
            -std::numeric_limits<double>::infinity());
        auto evaluate_move = [&](const int item) {
                const auto& move = alternatives[static_cast<std::size_t>(item)];
                const int old_position = current[static_cast<std::size_t>(move.slot)];
                double aep_delta = problem.diagonal_aep[
                    static_cast<std::size_t>(move.candidate)
                ] - problem.diagonal_aep[static_cast<std::size_t>(old_position)];
                for (int other = 0; other < kTurbines; ++other) {
                    if (other == move.slot) continue;
                    const int neighbor = current[static_cast<std::size_t>(other)];
                    aep_delta += problem.pair(move.candidate, neighbor)
                        - problem.pair(old_position, neighbor);
                }
                const double foundation_delta = problem.foundation_meur[
                    static_cast<std::size_t>(move.candidate)
                ] - problem.foundation_meur[static_cast<std::size_t>(old_position)];
                deltas[static_cast<std::size_t>(item)] = aep_delta
                    * kEnergyPriceEurMwh * annuity_factor() / 1.0e6
                    - foundation_delta;
            };
        if (alternatives.size() >= 64U && executor.thread_count() > 1) {
            executor.parallel_for(0, static_cast<int>(alternatives.size()),
                                  evaluate_move);
        } else {
            for (int item = 0; item < static_cast<int>(alternatives.size()); ++item) {
                evaluate_move(item);
            }
        }
        candidate_evaluations += alternatives.size();
        int best_alternative = -1;
        for (int item = 0; item < static_cast<int>(deltas.size()); ++item) {
            if (best_alternative < 0
                || deltas[static_cast<std::size_t>(item)]
                    > deltas[static_cast<std::size_t>(best_alternative)]) {
                best_alternative = item;
            }
        }
        bool improved = false;
        if (best_alternative >= 0
            && deltas[static_cast<std::size_t>(best_alternative)] > 0.0) {
            const auto& move = alternatives[
                static_cast<std::size_t>(best_alternative)
            ];
            auto trial = current;
            trial[static_cast<std::size_t>(move.slot)] = move.candidate;
            const auto trial_evaluation = problem.evaluate(trial);
            if (trial_evaluation.feasible
                && trial_evaluation.npv_meur > current_evaluation.npv_meur) {
                current = std::move(trial);
                current_evaluation = trial_evaluation;
                improved = true;
                neighborhood = 0;
                if (current_evaluation.npv_meur > best_evaluation.npv_meur) {
                    best = current;
                    best_evaluation = current_evaluation;
                }
            }
        }
        if (!improved) {
            ++neighborhood;
            if (neighborhood == static_cast<int>(kVnsRadiiM.size())) {
                neighborhood = 0;
                // Random-conic shake completion from the cited T31 method:
                // consume a counter-keyed feasible candidate, while retaining
                // the incumbent separately. This is an ordered state change.
                if (!alternatives.empty()) {
                    const std::size_t choice = static_cast<std::size_t>(
                        rng.integer(
                            0, static_cast<int>(alternatives.size()),
                            cycles, stream, 99
                        )
                    );
                    const auto& move = alternatives[choice];
                    auto shaken = current;
                    shaken[static_cast<std::size_t>(move.slot)] = move.candidate;
                    const auto shaken_evaluation = problem.evaluate(shaken);
                    if (shaken_evaluation.feasible) {
                        current = std::move(shaken);
                        current_evaluation = shaken_evaluation;
                    }
                }
            }
        }
        ++cycles;
    }
    return {best, best_evaluation, cycles, candidate_evaluations,
            elapsed(started)};
}

}  // namespace

std::vector<CaseDefinition> paper_cases() {
    return {
        {'A', 54.60,  1.55, "Dogger Bank",    'D', 4734.0, 4.98, 3.11, 4705.9},
        {'B', 54.40,  2.10, "Dogger Bank",    'E', 4702.3, 4.97, 3.11, 4675.2},
        {'C', 53.51,  1.40, "Eastern",        'F', 4504.9, 4.98, 3.10, 4501.4},
        {'D', 52.20,  2.00, "Eastern",        'G', 4021.6, 4.36, 3.13, 3955.7},
        {'E', 50.75,  0.85, "South East",     'H', 4045.0, 4.73, 3.08, 4019.6},
        {'F', 50.65,  0.40, "South East",     'I', 3987.8, 4.77, 3.06, 3929.7},
        {'G', 53.65, -3.28, "Northern Wales", 'J', 4044.3, 4.71, 3.14, 3991.9},
        {'H', 54.50, -3.75, "Northern Wales", 'I', 4040.6, 4.60, 3.14, 3932.0},
    };
}

Problem::Problem(
    std::filesystem::path t31_dataset_root,
    const char seed_role,
    const int workers
) : impl_(std::make_unique<Impl>(std::move(t31_dataset_root), seed_role, workers)) {
    if (workers < 1) throw std::invalid_argument("T83 workers must be positive");
}

Problem::~Problem() = default;
Problem::Problem(Problem&&) noexcept = default;
Problem& Problem::operator=(Problem&&) noexcept = default;
const CaseDefinition& Problem::paper_case() const noexcept {
    return impl_->definition;
}
int Problem::source_candidate_positions() const noexcept { return impl_->source_count; }
int Problem::candidate_positions() const noexcept {
    return static_cast<int>(impl_->points.size());
}
double Problem::source_preprocessing_seconds() const noexcept {
    return impl_->source_preprocessing;
}
double Problem::candidate_preprocessing_seconds() const noexcept {
    return impl_->candidate_preprocessing;
}
double Problem::pair_matrix_seconds() const noexcept {
    return impl_->pair_matrix_seconds;
}
std::uint64_t Problem::source_matrix_fingerprint() const noexcept {
    return impl_->source_fingerprint;
}
const std::vector<Point>& Problem::candidate_points() const noexcept {
    return impl_->points;
}
Evaluation Problem::evaluate(const std::vector<int>& selected) const {
    return impl_->evaluate(selected);
}

RunResult run(const Problem& problem, const RunConfig& config) {
    if (config.workers < 1 || config.micro_time_seconds < 0.0) {
        throw std::invalid_argument("T83 invalid run configuration");
    }
    const auto started = Clock::now();
    fode::PersistentExecutor executor(config.workers);
    executor.reset_work_receipt();
    const fode::CounterRng rng(config.seed);
    const int cell_axis = config.macro_cell_axis_override > 0
        ? config.macro_cell_axis_override : 3;

    const auto macro_started = Clock::now();
    // Re-running the same immutable Problem (for H6 one/all-worker checks)
    // must recalibrate from the same unshifted economic model.
    problem.impl_->fixed_cost_offset_meur = 0.0;
    auto macro = macro_screen(*problem.impl_, executor, cell_axis);
    const double macro_seconds = elapsed(macro_started);
    // One unavailable layout-independent CAPEX/OPEX term is fixed from the
    // paper's rectangular anchor. It never changes method comparisons.
    problem.impl_->fixed_cost_offset_meur = macro.evaluation.npv_meur
        - problem.impl_->definition.paper_rectangle_npv_meur;
    macro.evaluation = problem.impl_->evaluate(macro.positions);

    const auto meso_started = Clock::now();
    auto meso = meso_construct(*problem.impl_, executor, macro.positions);
    const double meso_seconds = elapsed(meso_started);
    if (!meso.evaluation.feasible) {
        throw std::runtime_error("T83 meso final layout violates tender constraints");
    }

    const auto shape_micro = micro_optimize(
        *problem.impl_, meso.positions, executor, rng, 8301,
        config.micro_time_seconds, config.fixed_micro_cycles
    );
    const auto rectangle_micro = micro_optimize(
        *problem.impl_, macro.positions, executor, rng, 8302,
        config.micro_time_seconds, config.fixed_micro_cycles
    );
    const auto receipt = executor.work_receipt();
    return {
        .case_id = std::string("t83_seed_") + problem.paper_case().seed_role,
        .paper_case = problem.paper_case(),
        .seed = config.seed,
        .requested_workers = config.workers,
        .observed_workers = receipt.distinct_participants,
        .source_candidate_positions = problem.source_candidate_positions(),
        .hpc_candidate_positions = problem.candidate_positions(),
        .turbines = kTurbines,
        .macro_rectangles_evaluated = macro.attempted,
        .pair_matrix_evaluations = static_cast<std::uint64_t>(
            problem.candidate_positions()
        ) * static_cast<std::uint64_t>(problem.candidate_positions() - 1) / 2ULL,
        .meso_candidate_evaluations = meso.candidate_evaluations,
        .shape_micro_candidate_evaluations = shape_micro.candidate_evaluations,
        .rectangle_micro_candidate_evaluations =
            rectangle_micro.candidate_evaluations,
        .shape_micro_cycles = shape_micro.cycles,
        .rectangle_micro_cycles = rectangle_micro.cycles,
        .macro_rectangle = macro.evaluation,
        .meso_shape = meso.evaluation,
        .optimized_shape = shape_micro.evaluation,
        .optimized_rectangle = rectangle_micro.evaluation,
        .meso_positions = meso.positions,
        .optimized_shape_positions = shape_micro.positions,
        .optimized_rectangle_positions = rectangle_micro.positions,
        .source_preprocessing_seconds = problem.source_preprocessing_seconds(),
        .candidate_preprocessing_seconds = problem.candidate_preprocessing_seconds(),
        .pair_matrix_seconds = problem.pair_matrix_seconds(),
        .macro_seconds = macro_seconds,
        .meso_seconds = meso_seconds,
        .shape_micro_seconds = shape_micro.seconds,
        .rectangle_micro_seconds = rectangle_micro.seconds,
        .end_to_end_seconds = elapsed(started)
            + problem.source_preprocessing_seconds()
            + problem.candidate_preprocessing_seconds()
            + problem.pair_matrix_seconds(),
        .source_matrix_fingerprint = problem.source_matrix_fingerprint(),
        .scientific_hash = result_hash(
            shape_micro.positions, rectangle_micro.positions,
            shape_micro.evaluation, rectangle_micro.evaluation,
            shape_micro.cycles, rectangle_micro.cycles
        ),
    };
}

}  // namespace core99::t83
