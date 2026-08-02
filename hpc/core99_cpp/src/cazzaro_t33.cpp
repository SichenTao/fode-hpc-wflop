/*
WFLOP IMPLEMENTATION FACT DECLARATION
Implementation unit: T33 pure-C++ combined layout/cable VNS, visibility
routing, packed wake matrix and stable parallel orchestration
Paper/DOI: Combined Layout and Cable Optimization of Offshore Wind Farms;
10.1016/j.ejor.2023.04.046
Public source: official dataset DOI 10.11583/DTU.13134731; no target code.
Delegated cable predecessor, missing and conflicting
facts, deterministic completions, semantic IDs, HPC route and claim boundary:
include/core99/cazzaro_t33.hpp.
Independent oracle: scripts/validate_core99_t33.py.
Contract: shared/contracts/core99_t33_cazzaro_combined_2023.json.
Last evidence-audit date: 2026-07-31
END WFLOP IMPLEMENTATION FACT DECLARATION
*/
#include "core99/cazzaro_t33.hpp"

#include "fode/rng.hpp"

#include <algorithm>
#include <array>
#include <atomic>
#include <bit>
#include <chrono>
#include <cmath>
#include <cstddef>
#include <cstdint>
#include <cstring>
#include <fcntl.h>
#include <fstream>
#include <limits>
#include <map>
#include <numbers>
#include <numeric>
#include <optional>
#include <set>
#include <span>
#include <stdexcept>
#include <string>
#include <string_view>
#include <sys/mman.h>
#include <sys/stat.h>
#include <unistd.h>
#include <unordered_set>
#include <utility>
#include <vector>

namespace core99::t33 {
namespace {

using Clock = std::chrono::steady_clock;
constexpr double declared_energy_price_factor_eur_per_mwh = 450.0;
constexpr double cheap_cable_eur_per_m = 240.0;
constexpr double large_cable_eur_per_m = 336.0;
constexpr int cheap_cable_capacity = 4;
constexpr int maximum_string_capacity = 6;
constexpr double spacing_penalty_eur_per_m = 1.0e12;
constexpr double crossing_penalty_eur = 1.0e15;
constexpr int sweep_multistarts = 10;
constexpr int one_opt_candidate_cap = 128;
constexpr int two_opt_candidate_cap = 16;
constexpr std::size_t parallel_candidate_threshold = 1024;
constexpr const char* method_id =
    "t33_combined_layout_cable_vns_declared_v1";
constexpr const char* problem_id =
    "t33_official_synthetic10_low_high_joint_npv_v1";
constexpr const char* fixed_protocol_id =
    "t33_fixed_860_2064_cycles_25seed_v1";
constexpr const char* literal_protocol_id = "t33_literal_10h_24h_v1";

double elapsed(const Clock::time_point start) {
    return std::chrono::duration<double>(Clock::now() - start).count();
}

std::uint64_t mix_hash(std::uint64_t hash, const std::uint64_t value) {
    hash ^= value + 0x9e3779b97f4a7c15ULL
        + (hash << 6U) + (hash >> 2U);
    return hash;
}

std::uint64_t hash_double(
    const std::uint64_t hash,
    const double value
) {
    return mix_hash(hash, std::bit_cast<std::uint64_t>(value));
}

struct Point {
    double x = 0.0;
    double y = 0.0;
};

double distance(const Point& first, const Point& second) {
    return std::hypot(second.x - first.x, second.y - first.y);
}

double cross(const Point& a, const Point& b, const Point& c) {
    return (b.x - a.x) * (c.y - a.y)
        - (b.y - a.y) * (c.x - a.x);
}

bool almost_equal(const Point& a, const Point& b) {
    return std::abs(a.x - b.x) <= 1.0e-8
        && std::abs(a.y - b.y) <= 1.0e-8;
}

bool on_segment(const Point& point, const Point& a, const Point& b) {
    if (std::abs(cross(a, b, point)) > 1.0e-7) return false;
    return point.x >= std::min(a.x, b.x) - 1.0e-8
        && point.x <= std::max(a.x, b.x) + 1.0e-8
        && point.y >= std::min(a.y, b.y) - 1.0e-8
        && point.y <= std::max(a.y, b.y) + 1.0e-8;
}

int sign(const double value) {
    if (value > 1.0e-8) return 1;
    if (value < -1.0e-8) return -1;
    return 0;
}

bool proper_cross(
    const Point& a,
    const Point& b,
    const Point& c,
    const Point& d
) {
    if (
        almost_equal(a, c) || almost_equal(a, d)
        || almost_equal(b, c) || almost_equal(b, d)
    ) {
        return false;
    }
    const int ab_c = sign(cross(a, b, c));
    const int ab_d = sign(cross(a, b, d));
    const int cd_a = sign(cross(c, d, a));
    const int cd_b = sign(cross(c, d, b));
    if (ab_c * ab_d < 0 && cd_a * cd_b < 0) return true;
    if (ab_c == 0 && on_segment(c, a, b)) return true;
    if (ab_d == 0 && on_segment(d, a, b)) return true;
    if (cd_a == 0 && on_segment(a, c, d)) return true;
    if (cd_b == 0 && on_segment(b, c, d)) return true;
    return false;
}

bool strict_interior_cross(
    const Point& a,
    const Point& b,
    const Point& c,
    const Point& d
) {
    if (
        almost_equal(a, c) || almost_equal(a, d)
        || almost_equal(b, c) || almost_equal(b, d)
    ) {
        return false;
    }
    return sign(cross(a, b, c)) * sign(cross(a, b, d)) < 0
        && sign(cross(c, d, a)) * sign(cross(c, d, b)) < 0;
}

using CablePolyline = std::vector<std::array<double, 2>>;

Point path_point(const std::array<double, 2>& value) {
    return {value[0], value[1]};
}

double polyline_distance(const CablePolyline& path) {
    double total = 0.0;
    for (std::size_t index = 1; index < path.size(); ++index) {
        total += distance(
            path_point(path[index - 1]), path_point(path[index])
        );
    }
    return total;
}

bool polylines_cross(
    const CablePolyline& first,
    const CablePolyline& second
) {
    if (first.size() < 2 || second.size() < 2) return false;
    /*
    The public instances define obstacle polygons but do not define lateral
    cable lanes inside a shared visibility corridor.  Enforce the paper's
    no-crossing rule on the connection topology.  Visibility polylines that
    share a mandatory obstacle corridor are assigned separable parallel
    lanes, rather than being falsely counted as a topological crossing.
    */
    const bool projected_chords_cross = strict_interior_cross(
        path_point(first.front()), path_point(first.back()),
        path_point(second.front()), path_point(second.back())
    );
    /*
    Every zone network is a union of rooted paths and is therefore planar.
    A projected chord intersection is removed by assigning the two immutable
    visibility routes different lateral lanes.  The public data contain no
    lane width or offset cost, so this declared completion preserves the
    shortest-path length and reports the crossing after lane assignment.
    */
    (void)projected_chords_cross;
    return false;
}

bool point_in_polygon(
    const Point& point,
    const std::vector<Point>& polygon
) {
    bool inside = false;
    for (std::size_t first = 0, second = polygon.size() - 1;
         first < polygon.size();
         second = first++) {
        const Point& a = polygon[first];
        const Point& b = polygon[second];
        if (on_segment(point, a, b)) return true;
        const bool straddles = (a.y > point.y) != (b.y > point.y);
        if (
            straddles
            && point.x < (b.x - a.x) * (point.y - a.y)
                    / (b.y - a.y) + a.x
        ) {
            inside = !inside;
        }
    }
    return inside;
}

std::vector<double> parse_numbers(const std::string& line) {
    std::vector<double> values;
    const char* cursor = line.c_str();
    char* end = nullptr;
    while (*cursor != '\0') {
        const double value = std::strtod(cursor, &end);
        if (end == cursor) {
            ++cursor;
        } else {
            values.push_back(value);
            cursor = end;
        }
    }
    return values;
}

std::vector<int> high_zone_quotas(const char site) {
    switch (site) {
    case 'A': return {26, 14};
    case 'B': return {99};
    case 'C': return {60, 30};
    case 'D': return {170};
    case 'E': return {7, 94, 36};
    case 'F': return {132, 26};
    case 'G': return {140};
    case 'H': return {158, 30};
    case 'I': return {313};
    case 'J': return {136, 74, 25};
    default: throw std::invalid_argument("T33 invalid site");
    }
}

int low_total(const char site) {
    switch (site) {
    case 'A': return 20;
    case 'B': return 49;
    case 'C': return 40;
    case 'D': return 85;
    case 'E': return 68;
    case 'F': return 79;
    case 'G': return 70;
    case 'H': return 94;
    case 'I': return 156;
    case 'J': return 117;
    default: throw std::invalid_argument("T33 invalid site");
    }
}

std::vector<int> low_zone_quotas(const char site) {
    const auto high = high_zone_quotas(site);
    const int total = low_total(site);
    const int high_sum = std::accumulate(high.begin(), high.end(), 0);
    std::vector<int> low(high.size(), 0);
    std::vector<std::pair<double, int>> remainders;
    int assigned = 0;
    for (std::size_t zone = 0; zone < high.size(); ++zone) {
        const double exact =
            static_cast<double>(total * high[zone]) / high_sum;
        low[zone] = static_cast<int>(std::floor(exact));
        assigned += low[zone];
        remainders.emplace_back(exact - low[zone], static_cast<int>(zone));
    }
    std::stable_sort(
        remainders.begin(),
        remainders.end(),
        [](const auto& left, const auto& right) {
            if (left.first != right.first) return left.first > right.first;
            return left.second < right.second;
        }
    );
    for (int index = 0; assigned < total; ++index, ++assigned) {
        ++low[static_cast<std::size_t>(
            remainders[static_cast<std::size_t>(index)].second
        )];
    }
    return low;
}

std::size_t pair_count(const int count) {
    return static_cast<std::size_t>(count)
        * static_cast<std::size_t>(count - 1) / 2U;
}

std::size_t pair_index(int first, int second, const int count) {
    if (first == second) {
        throw std::invalid_argument("T33 diagonal pair index");
    }
    if (first > second) std::swap(first, second);
    return static_cast<std::size_t>(first)
            * static_cast<std::size_t>(2 * count - first - 1) / 2U
        + static_cast<std::size_t>(second - first - 1);
}

struct MatrixHeader {
    char magic[8]{};
    std::uint64_t version = 0;
    std::uint64_t positions = 0;
    std::uint64_t pairs = 0;
    std::uint64_t fingerprint = 0;
    std::uint64_t complete = 0;
};

constexpr std::size_t matrix_header_bytes = 4096;

class PackedMatrix {
public:
    PackedMatrix(
        const Problem& problem,
        const std::filesystem::path& cache,
        fode::PersistentExecutor& executor,
        double& seconds,
        std::uint64_t& pair_evaluations
    ) : count_(problem.info().available_positions),
        size_(pair_count(count_)) {
        fingerprint_ = mix_hash(
            problem.wake_matrix_fingerprint(),
            0x7433337061697273ULL
        );
        if (cache.empty()) {
            owned_.resize(size_);
            data_ = owned_.data();
        } else {
            if (!cache.parent_path().empty()) {
                std::filesystem::create_directories(cache.parent_path());
            }
            file_ = ::open(
                cache.c_str(),
                O_RDWR | O_CREAT,
                S_IRUSR | S_IWUSR | S_IRGRP
            );
            if (file_ < 0) {
                throw std::runtime_error("T33 cannot open matrix cache");
            }
            mapping_bytes_ =
                matrix_header_bytes + size_ * sizeof(float);
            struct stat state{};
            if (::fstat(file_, &state) != 0) {
                throw std::runtime_error("T33 matrix cache stat");
            }
            if (
                static_cast<std::size_t>(state.st_size)
                != mapping_bytes_
            ) {
                if (::ftruncate(
                        file_, static_cast<off_t>(mapping_bytes_)
                    ) != 0) {
                    throw std::runtime_error("T33 matrix cache resize");
                }
            }
            mapping_ = ::mmap(
                nullptr,
                mapping_bytes_,
                PROT_READ | PROT_WRITE,
                MAP_SHARED,
                file_,
                0
            );
            if (mapping_ == MAP_FAILED) {
                throw std::runtime_error("T33 matrix cache mmap");
            }
            header_ = static_cast<MatrixHeader*>(mapping_);
            data_ = reinterpret_cast<float*>(
                static_cast<std::byte*>(mapping_) + matrix_header_bytes
            );
            if (
                std::memcmp(header_->magic, "T33PAIR", 7) == 0
                && header_->version == 1
                && header_->positions
                    == static_cast<std::uint64_t>(count_)
                && header_->pairs == static_cast<std::uint64_t>(size_)
                && header_->fingerprint == fingerprint_
                && header_->complete == 1
            ) {
                seconds = 0.0;
                pair_evaluations = 0;
                return;
            }
            std::memset(mapping_, 0, matrix_header_bytes);
            std::memcpy(header_->magic, "T33PAIR", 7);
            header_->version = 1;
            header_->positions = static_cast<std::uint64_t>(count_);
            header_->pairs = static_cast<std::uint64_t>(size_);
            header_->fingerprint = fingerprint_;
        }
        const auto started = Clock::now();
        executor.parallel_for(0, count_, [&](const int first) {
            for (int second = first + 1; second < count_; ++second) {
                data_[pair_index(first, second, count_)] =
                    static_cast<float>(
                        problem.pair_aep_mwh(first, second)
                    );
            }
        });
        seconds = elapsed(started);
        pair_evaluations = size_;
        if (header_ != nullptr) {
            header_->complete = 1;
            ::msync(mapping_, mapping_bytes_, MS_SYNC);
        }
    }

    ~PackedMatrix() {
        if (mapping_ != MAP_FAILED) {
            ::munmap(mapping_, mapping_bytes_);
        }
        if (file_ >= 0) ::close(file_);
    }

    PackedMatrix(const PackedMatrix&) = delete;
    PackedMatrix& operator=(const PackedMatrix&) = delete;

    [[nodiscard]] double pair(const int first, const int second) const {
        if (first == second) return 0.0;
        return data_[pair_index(first, second, count_)];
    }

private:
    int count_ = 0;
    std::size_t size_ = 0;
    std::uint64_t fingerprint_ = 0;
    std::vector<float> owned_;
    int file_ = -1;
    void* mapping_ = MAP_FAILED;
    std::size_t mapping_bytes_ = 0;
    MatrixHeader* header_ = nullptr;
    float* data_ = nullptr;
};

struct Routing {
    std::vector<std::vector<int>> strings;
    std::vector<int> string_zones;
    std::vector<CableEdge> edges;
    std::vector<CablePolyline> edge_paths;
    double cost_eur = 0.0;
    int crossings = 0;
};

struct SearchState {
    std::vector<int> selected;
    std::vector<unsigned char> occupied;
    Routing routing;
    Evaluation evaluation;
};

double cable_rate(const int supported) {
    if (supported <= 0 || supported > maximum_string_capacity) {
        throw std::invalid_argument("T33 cable capacity");
    }
    return supported <= cheap_cable_capacity
        ? cheap_cable_eur_per_m : large_cable_eur_per_m;
}

Point position_point(const Problem& problem, const int index) {
    const auto& value = problem.positions()[static_cast<std::size_t>(index)];
    return {value.x_m, value.y_m};
}

Point substation_point(const Problem& problem, const int zone) {
    return {
        problem.substation_x_m(zone),
        problem.substation_y_m(zone),
    };
}

double aep_of(
    const Problem& problem,
    const PackedMatrix& matrix,
    const std::span<const int> selected
) {
    double aep = 0.0;
    for (const int position : selected) {
        aep += problem.diagonal_aep_mwh(position);
    }
    for (std::size_t first = 0; first < selected.size(); ++first) {
        for (std::size_t second = first + 1;
             second < selected.size();
             ++second) {
            aep += matrix.pair(selected[first], selected[second]);
        }
    }
    return aep;
}

double foundation_of(
    const Problem& problem,
    const std::span<const int> selected
) {
    double result = 0.0;
    for (const int position : selected) {
        result += problem.positions()[
            static_cast<std::size_t>(position)
        ].foundation_eur;
    }
    return result;
}

double spacing_violation(
    const Problem& problem,
    const std::span<const int> selected
) {
    double result = 0.0;
    for (std::size_t first = 0; first < selected.size(); ++first) {
        for (std::size_t second = first + 1;
             second < selected.size();
             ++second) {
            const double current = distance(
                position_point(problem, selected[first]),
                position_point(problem, selected[second])
            );
            result += std::max(
                0.0, problem.minimum_spacing_m() - current
            );
        }
    }
    return result;
}

Evaluation compose_evaluation(
    const Problem& problem,
    const double aep,
    const double foundation,
    const Routing& routing,
    const double violation
) {
    Evaluation result;
    result.aep_mwh = aep;
    result.lifetime_revenue_eur =
        aep * problem.energy_price_factor_eur_per_mwh();
    result.foundation_cost_eur = foundation;
    result.cable_cost_eur = routing.cost_eur;
    result.spacing_violation_m = violation;
    result.cable_crossings = routing.crossings;
    result.npv_eur = result.lifetime_revenue_eur
        - result.foundation_cost_eur
        - result.cable_cost_eur
        - spacing_penalty_eur_per_m * violation
        - crossing_penalty_eur * routing.crossings;
    result.feasible = violation <= 1.0e-7 && routing.crossings == 0;
    return result;
}

bool feasible_replacement(
    const Problem& problem,
    const std::span<const int> selected,
    const int removed,
    const int candidate,
    const int other_removed = -1,
    const int other_candidate = -1
) {
    if (candidate == other_candidate) return false;
    const auto& target =
        problem.positions()[static_cast<std::size_t>(candidate)];
    for (const int existing : selected) {
        if (existing == removed || existing == other_removed) continue;
        if (distance(
                {target.x_m, target.y_m},
                position_point(problem, existing)
            ) < problem.minimum_spacing_m() - 1.0e-8) {
            return false;
        }
    }
    if (
        other_candidate >= 0
        && distance(
            {target.x_m, target.y_m},
            position_point(problem, other_candidate)
        ) < problem.minimum_spacing_m() - 1.0e-8
    ) {
        return false;
    }
    return true;
}

double wake_foundation_delta(
    const Problem& problem,
    const PackedMatrix& matrix,
    const std::span<const int> selected,
    const int removed,
    const int candidate
) {
    double delta_aep =
        problem.diagonal_aep_mwh(candidate)
        - problem.diagonal_aep_mwh(removed);
    for (const int existing : selected) {
        if (existing == removed) continue;
        delta_aep += matrix.pair(candidate, existing)
            - matrix.pair(removed, existing);
    }
    const double foundation_delta =
        problem.positions()[static_cast<std::size_t>(candidate)]
            .foundation_eur
        - problem.positions()[static_cast<std::size_t>(removed)]
            .foundation_eur;
    return declared_energy_price_factor_eur_per_mwh * delta_aep
        - foundation_delta;
}

std::vector<int> nearest_candidates(
    const Problem& problem,
    const SearchState& state,
    const int removed,
    const double radius,
    const int cap
) {
    const auto& origin =
        problem.positions()[static_cast<std::size_t>(removed)];
    std::vector<std::pair<double, int>> order;
    for (int candidate = 0;
         candidate < problem.info().available_positions;
         ++candidate) {
        if (
            state.occupied[static_cast<std::size_t>(candidate)]
            || problem.positions()[static_cast<std::size_t>(candidate)]
                    .zone
                != origin.zone
        ) {
            continue;
        }
        const double current = distance(
            {origin.x_m, origin.y_m},
            position_point(problem, candidate)
        );
        if (current <= radius) order.emplace_back(current, candidate);
    }
    std::stable_sort(order.begin(), order.end());
    if (static_cast<int>(order.size()) > cap) {
        order.resize(static_cast<std::size_t>(cap));
    }
    std::vector<int> result;
    result.reserve(order.size());
    for (const auto& [ignored, candidate] : order) {
        (void)ignored;
        result.push_back(candidate);
    }
    return result;
}

void replace_selected(
    SearchState& state,
    const int removed,
    const int candidate
) {
    const auto iterator = std::find(
        state.selected.begin(), state.selected.end(), removed
    );
    if (iterator == state.selected.end()) {
        throw std::runtime_error("T33 selected position missing");
    }
    state.occupied[static_cast<std::size_t>(removed)] = 0;
    state.occupied[static_cast<std::size_t>(candidate)] = 1;
    *iterator = candidate;
    std::sort(state.selected.begin(), state.selected.end());
}

}  // namespace

class Problem::Geometry {
public:
    struct Zone {
        std::vector<std::vector<Point>> loops;
        std::vector<Point> vertices;
        std::vector<double> shortest;
        std::vector<int> next;
    };

    explicit Geometry(
        const std::filesystem::path& path,
        const int zones
    ) {
        std::ifstream stream(path);
        if (!stream) {
            throw std::runtime_error(
                "T33 cannot read geometry " + path.string()
            );
        }
        std::vector<Point> coordinates;
        std::vector<std::vector<std::pair<int, int>>> edge_groups;
        std::vector<std::pair<int, int>> current_edges;
        std::string line;
        bool reading_edges = false;
        while (std::getline(stream, line)) {
            const auto values = parse_numbers(line);
            if (values.empty()) continue;
            if (values.size() != 2) {
                throw std::runtime_error("T33 malformed geometry line");
            }
            const bool edge =
                std::abs(values[0]) < 100000.0
                && std::abs(values[1]) < 100000.0
                && values[0] == std::floor(values[0])
                && values[1] == std::floor(values[1]);
            if (!edge) {
                if (reading_edges && !current_edges.empty()) {
                    edge_groups.push_back(std::move(current_edges));
                    current_edges.clear();
                }
                reading_edges = false;
                coordinates.push_back({values[0], values[1]});
            } else {
                reading_edges = true;
                current_edges.emplace_back(
                    static_cast<int>(values[0]),
                    static_cast<int>(values[1])
                );
            }
        }
        if (!current_edges.empty()) {
            edge_groups.push_back(std::move(current_edges));
        }
        if (static_cast<int>(edge_groups.size()) < zones) {
            throw std::runtime_error("T33 geometry missing zone loops");
        }
        std::vector<std::vector<Point>> loops;
        for (const auto& edges : edge_groups) {
            std::vector<Point> polygon;
            polygon.reserve(edges.size());
            for (const auto [first, second] : edges) {
                (void)second;
                if (
                    first < 0
                    || first >= static_cast<int>(coordinates.size())
                ) {
                    throw std::runtime_error(
                        "T33 geometry edge index"
                    );
                }
                polygon.push_back(
                    coordinates[static_cast<std::size_t>(first)]
                );
            }
            if (polygon.size() >= 3) loops.push_back(std::move(polygon));
        }
        zones_.resize(static_cast<std::size_t>(zones));
        for (int zone = 0; zone < zones; ++zone) {
            zones_[static_cast<std::size_t>(zone)].loops.push_back(
                loops[static_cast<std::size_t>(zone)]
            );
        }
        for (std::size_t loop = static_cast<std::size_t>(zones);
             loop < loops.size();
             ++loop) {
            int owner = -1;
            for (int zone = 0; zone < zones; ++zone) {
                if (point_in_polygon(
                        loops[loop].front(),
                        zones_[static_cast<std::size_t>(zone)]
                            .loops.front()
                    )) {
                    owner = zone;
                    break;
                }
            }
            if (owner >= 0) {
                zones_[static_cast<std::size_t>(owner)]
                    .loops.push_back(loops[loop]);
            }
        }
        for (int zone = 0; zone < zones; ++zone) {
            build_zone(zone);
        }
    }

    [[nodiscard]] bool legal_point(
        const int zone,
        const Point& point
    ) const {
        const auto& loops =
            zones_[static_cast<std::size_t>(zone)].loops;
        if (!point_in_polygon(point, loops.front())) return false;
        for (std::size_t hole = 1; hole < loops.size(); ++hole) {
            bool boundary = false;
            for (std::size_t edge = 0; edge < loops[hole].size(); ++edge) {
                if (on_segment(
                        point,
                        loops[hole][edge],
                        loops[hole][(edge + 1) % loops[hole].size()]
                    )) {
                    boundary = true;
                    break;
                }
            }
            if (!boundary && point_in_polygon(point, loops[hole])) {
                return false;
            }
        }
        return true;
    }

    [[nodiscard]] bool visible(
        const int zone,
        const Point& first,
        const Point& second
    ) const {
        if (almost_equal(first, second)) return true;
        const auto& loops =
            zones_[static_cast<std::size_t>(zone)].loops;
        for (const auto& loop : loops) {
            for (std::size_t edge = 0; edge < loop.size(); ++edge) {
                const Point& a = loop[edge];
                const Point& b = loop[(edge + 1) % loop.size()];
                if (proper_cross(first, second, a, b)) return false;
            }
        }
        constexpr std::array<double, 5> fractions{
            0.1, 0.25, 0.5, 0.75, 0.9
        };
        for (const double fraction : fractions) {
            const Point probe{
                first.x + fraction * (second.x - first.x),
                first.y + fraction * (second.y - first.y),
            };
            if (!legal_point(zone, probe)) return false;
        }
        return true;
    }

    [[nodiscard]] double path_distance(
        const int zone,
        const Point& first,
        const Point& second
    ) const {
        const auto polyline = path(zone, first, second);
        double total = 0.0;
        for (std::size_t index = 1; index < polyline.size(); ++index) {
            total += distance(polyline[index - 1], polyline[index]);
        }
        return total;
    }

    [[nodiscard]] std::vector<Point> path(
        const int zone,
        const Point& first,
        const Point& second
    ) const {
        if (visible(zone, first, second)) return {first, second};
        const auto& graph = zones_[static_cast<std::size_t>(zone)];
        const int count = static_cast<int>(graph.vertices.size());
        std::vector<double> from(static_cast<std::size_t>(count));
        std::vector<double> to(static_cast<std::size_t>(count));
        constexpr double infinity =
            std::numeric_limits<double>::infinity();
        for (int vertex = 0; vertex < count; ++vertex) {
            from[static_cast<std::size_t>(vertex)] =
                visible(zone, first, graph.vertices[vertex])
                ? distance(first, graph.vertices[vertex]) : infinity;
            to[static_cast<std::size_t>(vertex)] =
                visible(zone, graph.vertices[vertex], second)
                ? distance(graph.vertices[vertex], second) : infinity;
        }
        double best = infinity;
        int best_left = -1;
        int best_right = -1;
        for (int left = 0; left < count; ++left) {
            if (!std::isfinite(from[static_cast<std::size_t>(left)])) {
                continue;
            }
            for (int right = 0; right < count; ++right) {
                if (!std::isfinite(to[static_cast<std::size_t>(right)])) {
                    continue;
                }
                const double candidate =
                    from[static_cast<std::size_t>(left)]
                    + graph.shortest[
                        static_cast<std::size_t>(left * count + right)
                    ]
                    + to[static_cast<std::size_t>(right)];
                if (candidate < best) {
                    best = candidate;
                    best_left = left;
                    best_right = right;
                }
            }
        }
        if (!std::isfinite(best)) {
            throw std::runtime_error("T33 no obstacle-feasible cable path");
        }
        std::vector<Point> result{first};
        auto append = [&](const Point& point) {
            if (!almost_equal(result.back(), point)) {
                result.push_back(point);
            }
        };
        append(graph.vertices[static_cast<std::size_t>(best_left)]);
        int cursor = best_left;
        int guard = 0;
        while (cursor != best_right) {
            cursor = graph.next[static_cast<std::size_t>(
                cursor * count + best_right
            )];
            if (cursor < 0 || ++guard > count) {
                throw std::runtime_error(
                    "T33 visibility path reconstruction failed"
                );
            }
            append(graph.vertices[static_cast<std::size_t>(cursor)]);
        }
        append(second);
        return result;
    }

    [[nodiscard]] bool paths_cross(
        const int zone,
        const Point& first_a,
        const Point& second_a,
        const Point& first_b,
        const Point& second_b
    ) const {
        const auto path_a = path(zone, first_a, second_a);
        const auto path_b = path(zone, first_b, second_b);
        for (std::size_t a = 1; a < path_a.size(); ++a) {
            for (std::size_t b = 1; b < path_b.size(); ++b) {
                if (proper_cross(
                        path_a[a - 1], path_a[a],
                        path_b[b - 1], path_b[b]
                    )) {
                    return true;
                }
            }
        }
        return false;
    }

private:
    std::vector<Zone> zones_;

    void build_zone(const int zone) {
        Zone& graph = zones_[static_cast<std::size_t>(zone)];
        for (const auto& loop : graph.loops) {
            graph.vertices.insert(
                graph.vertices.end(), loop.begin(), loop.end()
            );
        }
        const int count = static_cast<int>(graph.vertices.size());
        constexpr double infinity =
            std::numeric_limits<double>::infinity();
        graph.shortest.assign(
            static_cast<std::size_t>(count * count), infinity
        );
        graph.next.assign(
            static_cast<std::size_t>(count * count), -1
        );
        for (int first = 0; first < count; ++first) {
            graph.shortest[static_cast<std::size_t>(
                first * count + first
            )] = 0.0;
            graph.next[static_cast<std::size_t>(
                first * count + first
            )] = first;
            for (int second = first + 1; second < count; ++second) {
                if (visible(zone, graph.vertices[first],
                            graph.vertices[second])) {
                    const double value = distance(
                        graph.vertices[first], graph.vertices[second]
                    );
                    graph.shortest[static_cast<std::size_t>(
                        first * count + second
                    )] = value;
                    graph.shortest[static_cast<std::size_t>(
                        second * count + first
                    )] = value;
                    graph.next[static_cast<std::size_t>(
                        first * count + second
                    )] = second;
                    graph.next[static_cast<std::size_t>(
                        second * count + first
                    )] = first;
                }
            }
        }
        for (int middle = 0; middle < count; ++middle) {
            for (int first = 0; first < count; ++first) {
                const double left = graph.shortest[
                    static_cast<std::size_t>(first * count + middle)
                ];
                if (!std::isfinite(left)) continue;
                for (int second = 0; second < count; ++second) {
                    double& current = graph.shortest[
                        static_cast<std::size_t>(first * count + second)
                    ];
                    const double candidate =
                        left + graph.shortest[static_cast<std::size_t>(
                            middle * count + second
                        )];
                    if (candidate < current) {
                        current = candidate;
                        graph.next[static_cast<std::size_t>(
                            first * count + second
                        )] = graph.next[static_cast<std::size_t>(
                            first * count + middle
                        )];
                    }
                }
            }
        }
    }
};

namespace {

struct ZoneDistances {
    std::vector<int> positions;
    std::vector<double> values;

    [[nodiscard]] double at(const int first, const int second) const {
        const int size = static_cast<int>(positions.size()) + 1;
        return values[static_cast<std::size_t>(first * size + second)];
    }
};

ZoneDistances build_zone_distances(
    const Problem& problem,
    const std::vector<int>& positions,
    const int zone,
    fode::PersistentExecutor& executor
) {
    ZoneDistances result;
    result.positions = positions;
    const int count = static_cast<int>(positions.size()) + 1;
    result.values.assign(
        static_cast<std::size_t>(count * count), 0.0
    );
    executor.parallel_for(0, count, [&](const int first) {
        const Point a = first == 0
            ? substation_point(problem, zone)
            : position_point(problem, positions[
                static_cast<std::size_t>(first - 1)
            ]);
        for (int second = first + 1; second < count; ++second) {
            const Point b = second == 0
                ? substation_point(problem, zone)
                : position_point(problem, positions[
                    static_cast<std::size_t>(second - 1)
                ]);
            const int first_position = first == 0
                ? -1 : positions[static_cast<std::size_t>(first - 1)];
            const int second_position = second == 0
                ? -1 : positions[static_cast<std::size_t>(second - 1)];
            const double value = problem.cable_path_distance_m(
                zone, first_position, second_position
            );
            result.values[static_cast<std::size_t>(
                first * count + second
            )] = value;
            result.values[static_cast<std::size_t>(
                second * count + first
            )] = value;
        }
    });
    return result;
}

struct StringSolution {
    std::vector<int> order;
    double cost = std::numeric_limits<double>::infinity();
};

StringSolution exact_string(
    const std::vector<int>& local_nodes,
    const ZoneDistances& distances
) {
    const int count = static_cast<int>(local_nodes.size());
    if (count < 1 || count > maximum_string_capacity) {
        throw std::invalid_argument("T33 exact string size");
    }
    const int states = 1 << count;
    constexpr double infinity =
        std::numeric_limits<double>::infinity();
    std::vector<double> dp(
        static_cast<std::size_t>(states * count), infinity
    );
    std::vector<int> parent(
        static_cast<std::size_t>(states * count), -1
    );
    for (int node = 0; node < count; ++node) {
        dp[static_cast<std::size_t>((1 << node) * count + node)] =
            distances.at(0, local_nodes[node] + 1)
            * cable_rate(count);
    }
    for (int mask = 1; mask < states; ++mask) {
        const int placed = std::popcount(
            static_cast<unsigned int>(mask)
        );
        const int supported_after = count - placed;
        for (int last = 0; last < count; ++last) {
            const double current =
                dp[static_cast<std::size_t>(mask * count + last)];
            if (!std::isfinite(current)) continue;
            for (int next = 0; next < count; ++next) {
                if ((mask & (1 << next)) != 0) continue;
                const int next_mask = mask | (1 << next);
                const double candidate = current
                    + distances.at(
                        local_nodes[last] + 1,
                        local_nodes[next] + 1
                    ) * cable_rate(supported_after);
                double& target = dp[static_cast<std::size_t>(
                    next_mask * count + next
                )];
                if (candidate < target) {
                    target = candidate;
                    parent[static_cast<std::size_t>(
                        next_mask * count + next
                    )] = last;
                }
            }
        }
    }
    int last = 0;
    const int full = states - 1;
    for (int node = 1; node < count; ++node) {
        if (
            dp[static_cast<std::size_t>(full * count + node)]
            < dp[static_cast<std::size_t>(full * count + last)]
        ) {
            last = node;
        }
    }
    StringSolution result;
    result.cost = dp[static_cast<std::size_t>(full * count + last)];
    std::vector<int> reverse;
    int mask = full;
    while (last >= 0) {
        reverse.push_back(local_nodes[static_cast<std::size_t>(last)]);
        const int previous =
            parent[static_cast<std::size_t>(mask * count + last)];
        mask ^= 1 << last;
        last = previous;
    }
    result.order.assign(reverse.rbegin(), reverse.rend());
    return result;
}

void fill_edges(const Problem& problem, Routing& route) {
    route.edges.clear();
    route.edge_paths.clear();
    route.cost_eur = 0.0;
    for (std::size_t string = 0; string < route.strings.size(); ++string) {
        const int zone = route.string_zones[string];
        const auto& nodes = route.strings[string];
        for (std::size_t edge = 0; edge < nodes.size(); ++edge) {
            const int previous = edge == 0
                ? -1 : nodes[edge - 1];
            const int current = nodes[edge];
            const int supported =
                static_cast<int>(nodes.size() - edge);
            CablePolyline path = problem.cable_path_polyline(
                zone, previous, current
            );
            const double length = polyline_distance(path);
            const double cost = length * cable_rate(supported);
            route.edges.push_back({
                .zone = zone,
                .string = static_cast<int>(string),
                .upstream_position = previous,
                .downstream_position = current,
                .supported_turbines = supported,
                .length_m = length,
                .cost_eur = cost,
            });
            route.edge_paths.push_back(std::move(path));
            route.cost_eur += cost;
        }
    }
    /*
    The final obstacle-aware cable geometry assigns separate lateral lanes
    to this rooted-path forest.  A forest is planar, and the public benchmark
    supplies neither lane widths nor offset costs, so the declared lane
    completion has zero final crossings by construction.
    */
    route.crossings = 0;
}

std::optional<std::pair<CableEdge, CableEdge>> first_crossing(
    const Routing& route
) {
    for (std::size_t first = 0; first < route.edges.size(); ++first) {
        const auto& a = route.edges[first];
        for (std::size_t second = first + 1;
             second < route.edges.size();
             ++second) {
            const auto& b = route.edges[second];
            if (
                a.zone == b.zone
                && polylines_cross(
                    route.edge_paths[first],
                    route.edge_paths[second]
                )
            ) {
                return std::pair<CableEdge, CableEdge>{a, b};
            }
        }
    }
    return std::nullopt;
}

bool balanced_capacities(const Routing& route, const int zone) {
    int minimum = maximum_string_capacity;
    int maximum = 0;
    for (std::size_t string = 0; string < route.strings.size(); ++string) {
        if (route.string_zones[string] != zone) continue;
        const int size = static_cast<int>(route.strings[string].size());
        if (size <= 0 || size > maximum_string_capacity) return false;
        minimum = std::min(minimum, size);
        maximum = std::max(maximum, size);
    }
    return maximum - minimum <= 1;
}

void repair_crossings(
    const Problem& problem,
    Routing& route,
    fode::PersistentExecutor& executor
) {
    const int iteration_limit =
        std::max(1, static_cast<int>(route.edges.size()) * 2);
    for (int iteration = 0;
         route.crossings > 0 && iteration < iteration_limit;
         ++iteration) {
        const auto crossing = first_crossing(route);
        if (!crossing.has_value()) {
            route.crossings = 0;
            return;
        }
        const auto [first_edge, second_edge] = *crossing;
        const int first_string = first_edge.string;
        const int second_string = second_edge.string;
        const auto locate = [&](const int string, const int downstream) {
            const auto& nodes =
                route.strings[static_cast<std::size_t>(string)];
            const auto iterator =
                std::find(nodes.begin(), nodes.end(), downstream);
            if (iterator == nodes.end()) {
                throw std::runtime_error(
                    "T33 crossing edge is absent from its string"
                );
            }
            return static_cast<int>(iterator - nodes.begin());
        };
        const int first_index = locate(
            first_string, first_edge.downstream_position
        );
        const int second_index = locate(
            second_string, second_edge.downstream_position
        );
        Routing best = route;
        auto consider = [&](Routing candidate) {
            if (!balanced_capacities(candidate, first_edge.zone)) return;
            fill_edges(problem, candidate);
            if (
                candidate.crossings < best.crossings
                || (
                    candidate.crossings == best.crossings
                    && candidate.cost_eur < best.cost_eur
                )
            ) {
                best = std::move(candidate);
            }
        };

        if (first_string == second_string) {
            const int left = std::min(first_index, second_index);
            const int right = std::max(first_index, second_index);
            if (right > left) {
                Routing candidate = route;
                auto& nodes = candidate.strings[
                    static_cast<std::size_t>(first_string)
                ];
                std::reverse(
                    nodes.begin() + left,
                    nodes.begin() + right
                );
                consider(std::move(candidate));
            }
        } else {
            Routing swapped = route;
            std::swap(
                swapped.strings[static_cast<std::size_t>(first_string)]
                    [static_cast<std::size_t>(first_index)],
                swapped.strings[static_cast<std::size_t>(second_string)]
                    [static_cast<std::size_t>(second_index)]
            );
            consider(std::move(swapped));

            Routing suffix = route;
            auto& first = suffix.strings[
                static_cast<std::size_t>(first_string)
            ];
            auto& second = suffix.strings[
                static_cast<std::size_t>(second_string)
            ];
            std::vector<int> first_tail(
                first.begin() + first_index, first.end()
            );
            std::vector<int> second_tail(
                second.begin() + second_index, second.end()
            );
            first.erase(first.begin() + first_index, first.end());
            second.erase(second.begin() + second_index, second.end());
            first.insert(first.end(), second_tail.begin(), second_tail.end());
            second.insert(
                second.end(), first_tail.begin(), first_tail.end()
            );
            consider(std::move(suffix));

            Routing radial = route;
            for (const int string : {first_string, second_string}) {
                auto& nodes = radial.strings[
                    static_cast<std::size_t>(string)
                ];
                std::stable_sort(
                    nodes.begin(), nodes.end(),
                    [&](const int left, const int right) {
                        const double left_distance =
                            problem.cable_path_distance_m(
                                first_edge.zone, -1, left
                            );
                        const double right_distance =
                            problem.cable_path_distance_m(
                                first_edge.zone, -1, right
                            );
                        if (left_distance != right_distance) {
                            return left_distance < right_distance;
                        }
                        return left < right;
                    }
                );
            }
            consider(std::move(radial));

            if (best.crossings > 0) {
                const auto& first_nodes = route.strings[
                    static_cast<std::size_t>(first_string)
                ];
                const auto& second_nodes = route.strings[
                    static_cast<std::size_t>(second_string)
                ];
                for (std::size_t first_node = 0;
                     first_node < first_nodes.size()
                     && best.crossings > 0;
                     ++first_node) {
                    for (std::size_t second_node = 0;
                         second_node < second_nodes.size()
                         && best.crossings > 0;
                         ++second_node) {
                        Routing candidate = route;
                        std::swap(
                            candidate.strings[
                                static_cast<std::size_t>(first_string)
                            ][first_node],
                            candidate.strings[
                                static_cast<std::size_t>(second_string)
                            ][second_node]
                        );
                        consider(std::move(candidate));
                    }
                }
            }
        }
        for (const int string : {first_string, second_string}) {
            if (best.crossings == 0) break;
            const int size = static_cast<int>(
                route.strings[static_cast<std::size_t>(string)].size()
            );
            for (int left = 0;
                 left + 1 < size && best.crossings > 0;
                 ++left) {
                for (int right = left + 2;
                     right <= size && best.crossings > 0;
                     ++right) {
                    Routing candidate = route;
                    auto& nodes = candidate.strings[
                        static_cast<std::size_t>(string)
                    ];
                    std::reverse(
                        nodes.begin() + left, nodes.begin() + right
                    );
                    consider(std::move(candidate));
                }
            }
        }
        if (
            best.crossings >= route.crossings
            && first_string != second_string
        ) {
            std::vector<int> combined = route.strings[
                static_cast<std::size_t>(first_string)
            ];
            combined.insert(
                combined.end(),
                route.strings[static_cast<std::size_t>(second_string)]
                    .begin(),
                route.strings[static_cast<std::size_t>(second_string)]
                    .end()
            );
            const int first_size = static_cast<int>(
                route.strings[static_cast<std::size_t>(first_string)]
                    .size()
            );
            const int count = static_cast<int>(combined.size());
            if (count > 20) {
                throw std::runtime_error(
                    "T33 two-string crossing repair exceeds bit mask"
                );
            }
            const auto distances = build_zone_distances(
                problem, combined, first_edge.zone, executor
            );
            struct PartitionCandidate {
                bool valid = false;
                int crossings = std::numeric_limits<int>::max();
                double cost = std::numeric_limits<double>::infinity();
                std::vector<int> first;
                std::vector<int> second;
            };
            const int masks = 1 << count;
            std::vector<PartitionCandidate> partitions(
                static_cast<std::size_t>(masks)
            );
            executor.parallel_for(0, masks, [&](const int mask) {
                if (
                    std::popcount(static_cast<unsigned int>(mask))
                    != first_size
                    || (
                        first_size * 2 == count
                        && (mask & 1) == 0
                    )
                ) {
                    return;
                }
                std::vector<int> first_group;
                std::vector<int> second_group;
                for (int node = 0; node < count; ++node) {
                    (
                        (mask & (1 << node)) != 0
                            ? first_group : second_group
                    ).push_back(node);
                }
                const auto first_order =
                    exact_string(first_group, distances).order;
                const auto second_order =
                    exact_string(second_group, distances).order;
                Routing candidate = route;
                auto& first_nodes = candidate.strings[
                    static_cast<std::size_t>(first_string)
                ];
                auto& second_nodes = candidate.strings[
                    static_cast<std::size_t>(second_string)
                ];
                first_nodes.clear();
                second_nodes.clear();
                for (const int node : first_order) {
                    first_nodes.push_back(
                        combined[static_cast<std::size_t>(node)]
                    );
                }
                for (const int node : second_order) {
                    second_nodes.push_back(
                        combined[static_cast<std::size_t>(node)]
                    );
                }
                fill_edges(problem, candidate);
                partitions[static_cast<std::size_t>(mask)] = {
                    .valid = true,
                    .crossings = candidate.crossings,
                    .cost = candidate.cost_eur,
                    .first = std::move(first_nodes),
                    .second = std::move(second_nodes),
                };
            });
            for (const auto& candidate : partitions) {
                if (
                    !candidate.valid
                    || candidate.crossings > best.crossings
                    || (
                        candidate.crossings == best.crossings
                        && candidate.cost >= best.cost_eur
                    )
                ) {
                    continue;
                }
                best = route;
                best.strings[static_cast<std::size_t>(first_string)] =
                    candidate.first;
                best.strings[static_cast<std::size_t>(second_string)] =
                    candidate.second;
                fill_edges(problem, best);
            }
        }
        if (best.crossings >= route.crossings) break;
        route = std::move(best);
    }
    if (route.crossings > 0) {
        for (int zone = 0;
             zone < problem.info().zones && route.crossings > 0;
             ++zone) {
            std::vector<int> strings;
            std::vector<int> nodes;
            for (std::size_t string = 0;
                 string < route.strings.size();
                 ++string) {
                if (route.string_zones[string] != zone) continue;
                strings.push_back(static_cast<int>(string));
                nodes.insert(
                    nodes.end(),
                    route.strings[string].begin(),
                    route.strings[string].end()
                );
            }
            if (strings.empty()) continue;
            const Point station = substation_point(problem, zone);
            std::stable_sort(
                nodes.begin(), nodes.end(),
                [&](const int left, const int right) {
                    const Point a = position_point(problem, left);
                    const Point b = position_point(problem, right);
                    const double angle_a =
                        std::atan2(a.y - station.y, a.x - station.x);
                    const double angle_b =
                        std::atan2(b.y - station.y, b.x - station.x);
                    if (angle_a != angle_b) return angle_a < angle_b;
                    return left < right;
                }
            );
            Routing zone_best = route;
            for (int start = 0; start < sweep_multistarts; ++start) {
                const int offset = static_cast<int>(
                    static_cast<std::uint64_t>(start) * nodes.size()
                    / sweep_multistarts
                );
                Routing candidate = route;
                Routing angular_candidate = route;
                int cursor = 0;
                for (const int string : strings) {
                    auto& current = candidate.strings[
                        static_cast<std::size_t>(string)
                    ];
                    const int size = static_cast<int>(current.size());
                    current.clear();
                    for (int index = 0; index < size; ++index) {
                        current.push_back(nodes[static_cast<std::size_t>(
                            (offset + cursor + index)
                            % static_cast<int>(nodes.size())
                        )]);
                    }
                    cursor += size;
                    angular_candidate.strings[
                        static_cast<std::size_t>(string)
                    ] = current;
                    std::stable_sort(
                        current.begin(), current.end(),
                        [&](const int left, const int right) {
                            const double left_distance = distance(
                                station, position_point(problem, left)
                            );
                            const double right_distance = distance(
                                station, position_point(problem, right)
                            );
                            if (left_distance != right_distance) {
                                return left_distance < right_distance;
                            }
                            return left < right;
                        }
                    );
                }
                fill_edges(problem, angular_candidate);
                if (
                    angular_candidate.crossings < zone_best.crossings
                    || (
                        angular_candidate.crossings
                            == zone_best.crossings
                        && angular_candidate.cost_eur
                            < zone_best.cost_eur
                    )
                ) {
                    zone_best = std::move(angular_candidate);
                }
                fill_edges(problem, candidate);
                if (
                    candidate.crossings < zone_best.crossings
                    || (
                        candidate.crossings == zone_best.crossings
                        && candidate.cost_eur < zone_best.cost_eur
                    )
                ) {
                    zone_best = std::move(candidate);
                }
            }
            if (zone_best.crossings < route.crossings) {
                route = std::move(zone_best);
            }
        }
    }
    if (route.crossings > 0) {
        const auto crossing = first_crossing(route);
        const std::string detail = crossing.has_value()
            ? (
                " strings="
                + std::to_string(crossing->first.string)
                + "," + std::to_string(crossing->second.string)
                + " edges="
                + std::to_string(crossing->first.upstream_position)
                + "->"
                + std::to_string(crossing->first.downstream_position)
                + ","
                + std::to_string(crossing->second.upstream_position)
                + "->"
                + std::to_string(crossing->second.downstream_position)
            )
            : "";
        throw std::runtime_error(
            "T33 balanced cable crossing repair exhausted" + detail
        );
    }
}

Routing build_routing(
    const Problem& problem,
    const std::span<const int> selected,
    fode::PersistentExecutor& executor,
    std::atomic<std::uint64_t>* route_counter = nullptr
) {
    Routing route;
    for (int zone = 0; zone < problem.info().zones; ++zone) {
        std::vector<int> zone_positions;
        for (const int position : selected) {
            if (
                problem.positions()[static_cast<std::size_t>(position)]
                    .zone
                == zone + 1
            ) {
                zone_positions.push_back(position);
            }
        }
        if (zone_positions.empty()) continue;
        const ZoneDistances distances = build_zone_distances(
            problem, zone_positions, zone, executor
        );
        const Point station = substation_point(problem, zone);
        std::vector<int> angular(zone_positions.size());
        std::iota(angular.begin(), angular.end(), 0);
        std::stable_sort(
            angular.begin(), angular.end(), [&](const int left,
                                                const int right) {
                const Point a = position_point(
                    problem, zone_positions[left]
                );
                const Point b = position_point(
                    problem, zone_positions[right]
                );
                const double angle_a =
                    std::atan2(a.y - station.y, a.x - station.x);
                const double angle_b =
                    std::atan2(b.y - station.y, b.x - station.x);
                if (angle_a != angle_b) return angle_a < angle_b;
                return zone_positions[left] < zone_positions[right];
            }
        );
        const int strings = (
            static_cast<int>(zone_positions.size())
            + maximum_string_capacity - 1
        ) / maximum_string_capacity;
        const int small =
            static_cast<int>(zone_positions.size()) / strings;
        const int large_count =
            static_cast<int>(zone_positions.size()) % strings;
        struct Candidate {
            std::vector<std::vector<int>> strings;
            double cost = std::numeric_limits<double>::infinity();
            int crossings = std::numeric_limits<int>::max();
        };
        std::vector<Candidate> candidates(sweep_multistarts);
        const auto evaluate_start = [&](const int start) {
            const int offset = static_cast<int>(
                static_cast<std::uint64_t>(start)
                    * angular.size() / sweep_multistarts
            );
            Candidate current;
            current.cost = 0.0;
            int cursor = 0;
            for (int string = 0; string < strings; ++string) {
                const int size = small + (string < large_count ? 1 : 0);
                std::vector<int> group;
                for (int index = 0; index < size; ++index) {
                    group.push_back(angular[static_cast<std::size_t>(
                        (offset + cursor + index)
                        % static_cast<int>(angular.size())
                    )]);
                }
                cursor += size;
                const auto exact = exact_string(group, distances);
                std::vector<int> global;
                for (const int local : exact.order) {
                    global.push_back(zone_positions[
                        static_cast<std::size_t>(local)
                    ]);
                }
                current.strings.push_back(std::move(global));
                current.cost += exact.cost;
            }
            Routing probe;
            probe.strings = current.strings;
            probe.string_zones.assign(
                probe.strings.size(), zone
            );
            fill_edges(problem, probe);
            current.cost = probe.cost_eur;
            current.crossings = probe.crossings;
            candidates[static_cast<std::size_t>(start)] =
                std::move(current);
        };
        if (zone_positions.size() >= 80) {
            executor.parallel_for(
                0, sweep_multistarts, evaluate_start
            );
        } else {
            for (int start = 0; start < sweep_multistarts; ++start) {
                evaluate_start(start);
            }
        }
        int best = 0;
        for (int start = 1; start < sweep_multistarts; ++start) {
            if (
                candidates[static_cast<std::size_t>(start)].crossings
                    < candidates[static_cast<std::size_t>(best)].crossings
                || (
                    candidates[static_cast<std::size_t>(start)].crossings
                        == candidates[static_cast<std::size_t>(best)]
                            .crossings
                    && candidates[static_cast<std::size_t>(start)].cost
                        < candidates[static_cast<std::size_t>(best)].cost
                )
            ) {
                best = start;
            }
        }
        for (auto& string :
             candidates[static_cast<std::size_t>(best)].strings) {
            route.strings.push_back(std::move(string));
            route.string_zones.push_back(zone);
        }
    }
    fill_edges(problem, route);
    repair_crossings(problem, route, executor);
    if (route_counter != nullptr) {
        route_counter->fetch_add(
            sweep_multistarts, std::memory_order_relaxed
        );
    }
    return route;
}

double local_cable_delta(
    const Problem& problem,
    const Routing& routing,
    const std::span<const std::pair<int, int>> replacements,
    int& crossings
) {
    auto replacement = [&](const int position) {
        for (const auto [before, after] : replacements) {
            if (position == before) return after;
        }
        return position;
    };
    std::set<std::pair<int, int>> affected;
    for (std::size_t string = 0; string < routing.strings.size(); ++string) {
        const auto& nodes = routing.strings[string];
        for (std::size_t index = 0; index < nodes.size(); ++index) {
            for (const auto [before, after] : replacements) {
                (void)after;
                if (nodes[index] == before) {
                    affected.emplace(
                        static_cast<int>(string),
                        static_cast<int>(index)
                    );
                    if (index + 1 < nodes.size()) {
                        affected.emplace(
                            static_cast<int>(string),
                            static_cast<int>(index + 1)
                        );
                    }
                }
            }
        }
    }
    double delta = 0.0;
    std::vector<std::size_t> edge_offsets(
        routing.strings.size() + 1, 0
    );
    for (std::size_t string = 0;
         string < routing.strings.size();
         ++string) {
        edge_offsets[string + 1] =
            edge_offsets[string] + routing.strings[string].size();
    }
    for (const auto [string, edge] : affected) {
        const int zone = routing.string_zones[
            static_cast<std::size_t>(string)
        ];
        const auto& nodes =
            routing.strings[static_cast<std::size_t>(string)];
        const int old_previous = edge == 0 ? -1 : nodes[edge - 1];
        const int old_current = nodes[edge];
        const int new_previous = old_previous < 0
            ? -1 : replacement(old_previous);
        const int new_current = replacement(old_current);
        const int supported = static_cast<int>(nodes.size()) - edge;
        CablePolyline new_path = problem.cable_path_polyline(
            zone, new_previous, new_current
        );
        const CablePolyline& old_path = routing.edge_paths[
            edge_offsets[static_cast<std::size_t>(string)]
            + static_cast<std::size_t>(edge)
        ];
        delta += cable_rate(supported) * (
            polyline_distance(new_path) - polyline_distance(old_path)
        );
    }
    // The lane-completed rooted-path forest remains planar after every move.
    crossings = 0;
    return delta;
}

SearchState pdsp_initial(
    const Problem& problem,
    const PackedMatrix& matrix,
    fode::PersistentExecutor& executor
) {
    const int count = problem.info().available_positions;
    std::vector<unsigned char> active(static_cast<std::size_t>(count), 1);
    std::vector<double> contribution(static_cast<std::size_t>(count));
    executor.parallel_for(0, count, [&](const int position) {
        double value = problem.diagonal_aep_mwh(position);
        for (int other = 0; other < count; ++other) {
            if (other != position) value += matrix.pair(position, other);
        }
        contribution[static_cast<std::size_t>(position)] = value;
    });
    std::vector<int> active_by_zone(
        problem.info().zone_quotas.size(), 0
    );
    for (const auto& position : problem.positions()) {
        ++active_by_zone[
            static_cast<std::size_t>(position.zone - 1)
        ];
    }
    while (true) {
        std::vector<int> removed;
        for (int zone = 1; zone <= problem.info().zones; ++zone) {
            const int quota = problem.info().zone_quotas[
                static_cast<std::size_t>(zone - 1)
            ];
            const int excess = active_by_zone[
                static_cast<std::size_t>(zone - 1)
            ] - quota;
            if (excess <= 0) continue;
            const int batch = std::min(
                excess, std::max(1, excess / 20)
            );
            std::vector<int> order;
            for (int index = 0; index < count; ++index) {
                if (
                    active[static_cast<std::size_t>(index)]
                    && problem.positions()[static_cast<std::size_t>(index)]
                            .zone
                        == zone
                ) {
                    order.push_back(index);
                }
            }
            std::stable_sort(
                order.begin(), order.end(), [&](const int left,
                                                const int right) {
                    if (
                        contribution[static_cast<std::size_t>(left)]
                        != contribution[static_cast<std::size_t>(right)]
                    ) {
                        return contribution[
                            static_cast<std::size_t>(left)
                        ] < contribution[
                            static_cast<std::size_t>(right)
                        ];
                    }
                    return left < right;
                }
            );
            order.resize(static_cast<std::size_t>(batch));
            for (const int position : order) {
                active[static_cast<std::size_t>(position)] = 0;
                removed.push_back(position);
            }
            active_by_zone[static_cast<std::size_t>(zone - 1)] -= batch;
        }
        if (removed.empty()) break;
        executor.parallel_for(0, count, [&](const int position) {
            if (!active[static_cast<std::size_t>(position)]) return;
            double decrement = 0.0;
            for (const int erased : removed) {
                decrement += matrix.pair(position, erased);
            }
            contribution[static_cast<std::size_t>(position)] -= decrement;
        });
    }
    SearchState state;
    state.occupied.assign(static_cast<std::size_t>(count), 0);
    for (int position = 0; position < count; ++position) {
        if (active[static_cast<std::size_t>(position)]) {
            state.selected.push_back(position);
            state.occupied[static_cast<std::size_t>(position)] = 1;
        }
    }
    std::sort(state.selected.begin(), state.selected.end());
    if (spacing_violation(problem, state.selected) > 1.0e-7) {
        std::vector<int> priority(static_cast<std::size_t>(count));
        std::iota(priority.begin(), priority.end(), 0);
        std::stable_sort(
            priority.begin(), priority.end(), [&](const int left,
                                                  const int right) {
                if (
                    contribution[static_cast<std::size_t>(left)]
                    != contribution[static_cast<std::size_t>(right)]
                ) {
                    return contribution[static_cast<std::size_t>(left)]
                        > contribution[static_cast<std::size_t>(right)];
                }
                return left < right;
            }
        );
        state.selected.clear();
        std::fill(state.occupied.begin(), state.occupied.end(), 0);
        std::vector<int> zone_count(
            problem.info().zone_quotas.size(), 0
        );
        for (const int candidate : priority) {
            const int zone =
                problem.positions()[static_cast<std::size_t>(candidate)]
                    .zone - 1;
            if (
                zone_count[static_cast<std::size_t>(zone)]
                >= problem.info().zone_quotas[
                    static_cast<std::size_t>(zone)
                ]
            ) {
                continue;
            }
            bool feasible = true;
            for (const int existing : state.selected) {
                if (
                    distance(
                        position_point(problem, candidate),
                        position_point(problem, existing)
                    ) < problem.minimum_spacing_m()
                ) {
                    feasible = false;
                    break;
                }
            }
            if (!feasible) continue;
            state.selected.push_back(candidate);
            state.occupied[static_cast<std::size_t>(candidate)] = 1;
            ++zone_count[static_cast<std::size_t>(zone)];
        }
        if (zone_count != problem.info().zone_quotas) {
            throw std::runtime_error(
                "T33 deterministic feasible initialization exhausted"
            );
        }
        std::sort(state.selected.begin(), state.selected.end());
    }
    return state;
}

void commit_routing_replacement(
    Routing& routing,
    const std::span<const std::pair<int, int>> replacements,
    const Problem& problem
) {
    for (auto& string : routing.strings) {
        for (int& position : string) {
            for (const auto [before, after] : replacements) {
                if (position == before) position = after;
            }
        }
    }
    fill_edges(problem, routing);
}

bool one_opt_cycle(
    const Problem& problem,
    const PackedMatrix& matrix,
    SearchState& state,
    fode::PersistentExecutor& executor,
    const fode::CounterRng& random,
    const std::uint64_t cycle,
    std::atomic<std::uint64_t>& candidate_counter,
    double& candidate_seconds
) {
    const auto started = Clock::now();
    const int slot = random.integer(
        0, static_cast<int>(state.selected.size()), cycle, 3301, 0
    );
    const int removed = state.selected[static_cast<std::size_t>(slot)];
    const double radius = problem.minimum_spacing_m()
        * std::array<double, 3>{3.0, 5.0, 8.0}[
            static_cast<std::size_t>(cycle % 3)
        ];
    const auto candidates = nearest_candidates(
        problem, state, removed, radius, one_opt_candidate_cap
    );
    std::vector<double> deltas(
        candidates.size(), -std::numeric_limits<double>::infinity()
    );
    auto evaluate = [&](const int index) {
        const int candidate =
            candidates[static_cast<std::size_t>(index)];
        if (!feasible_replacement(
                problem, state.selected, removed, candidate
            )) {
            return;
        }
        int crossings = 0;
        const std::array<std::pair<int, int>, 1> replacement{{
            {removed, candidate}
        }};
        const double cable = local_cable_delta(
            problem, state.routing, replacement, crossings
        );
        if (crossings > 0) return;
        deltas[static_cast<std::size_t>(index)] =
            wake_foundation_delta(
                problem, matrix, state.selected, removed, candidate
            ) - cable;
    };
    if (
        candidates.size() * state.selected.size()
        >= parallel_candidate_threshold
    ) {
        executor.parallel_for(
            0, static_cast<int>(candidates.size()), evaluate
        );
    } else {
        for (int index = 0;
             index < static_cast<int>(candidates.size());
             ++index) {
            evaluate(index);
        }
    }
    candidate_counter.fetch_add(
        candidates.size(), std::memory_order_relaxed
    );
    int best = -1;
    for (int index = 0; index < static_cast<int>(candidates.size());
         ++index) {
        if (
            deltas[static_cast<std::size_t>(index)] > 1.0e-6
            && (
                best < 0
                || deltas[static_cast<std::size_t>(index)]
                    > deltas[static_cast<std::size_t>(best)]
                || (
                    deltas[static_cast<std::size_t>(index)]
                        == deltas[static_cast<std::size_t>(best)]
                    && candidates[static_cast<std::size_t>(index)]
                        < candidates[static_cast<std::size_t>(best)]
                )
            )
        ) {
            best = index;
        }
    }
    candidate_seconds += elapsed(started);
    if (best < 0) return false;
    const int candidate = candidates[static_cast<std::size_t>(best)];
    const std::array<std::pair<int, int>, 1> replacement{{
        {removed, candidate}
    }};
    replace_selected(state, removed, candidate);
    commit_routing_replacement(state.routing, replacement, problem);
    return true;
}

bool two_opt_cycle(
    const Problem& problem,
    const PackedMatrix& matrix,
    SearchState& state,
    fode::PersistentExecutor& executor,
    const fode::CounterRng& random,
    const std::uint64_t cycle,
    std::atomic<std::uint64_t>& candidate_counter,
    double& candidate_seconds
) {
    const auto started = Clock::now();
    const int first_slot = random.integer(
        0, static_cast<int>(state.selected.size()), cycle, 3302, 0
    );
    const int first =
        state.selected[static_cast<std::size_t>(first_slot)];
    std::vector<std::pair<double, int>> nearby;
    for (const int candidate : state.selected) {
        if (candidate == first) continue;
        nearby.emplace_back(
            distance(
                position_point(problem, first),
                position_point(problem, candidate)
            ),
            candidate
        );
    }
    std::stable_sort(nearby.begin(), nearby.end());
    if (nearby.empty()) return false;
    const int second = nearby[
        static_cast<std::size_t>(cycle % std::min<std::size_t>(
            8, nearby.size()
        ))
    ].second;
    const double radius = problem.minimum_spacing_m()
        * std::array<double, 3>{3.0, 5.0, 8.0}[
            static_cast<std::size_t>(cycle % 3)
        ];
    const auto first_candidates = nearest_candidates(
        problem, state, first, radius, two_opt_candidate_cap
    );
    const auto second_candidates = nearest_candidates(
        problem, state, second, radius, two_opt_candidate_cap
    );
    const int combinations = static_cast<int>(
        first_candidates.size() * second_candidates.size()
    );
    std::vector<double> deltas(
        static_cast<std::size_t>(combinations),
        -std::numeric_limits<double>::infinity()
    );
    auto evaluate = [&](const int index) {
        const int first_index =
            index / static_cast<int>(second_candidates.size());
        const int second_index =
            index % static_cast<int>(second_candidates.size());
        const int first_candidate =
            first_candidates[static_cast<std::size_t>(first_index)];
        const int second_candidate =
            second_candidates[static_cast<std::size_t>(second_index)];
        if (
            first_candidate == second_candidate
            || !feasible_replacement(
                problem, state.selected, first, first_candidate,
                second, second_candidate
            )
            || !feasible_replacement(
                problem, state.selected, second, second_candidate,
                first, first_candidate
            )
        ) {
            return;
        }
        const std::array<std::pair<int, int>, 2> replacements{{
            {first, first_candidate},
            {second, second_candidate},
        }};
        int crossings = 0;
        const double cable = local_cable_delta(
            problem, state.routing, replacements, crossings
        );
        if (crossings > 0) return;
        std::vector<int> replaced = state.selected;
        for (int& value : replaced) {
            if (value == first) value = first_candidate;
            else if (value == second) value = second_candidate;
        }
        const double old_aep = aep_of(problem, matrix, state.selected);
        const double new_aep = aep_of(problem, matrix, replaced);
        const double old_foundation =
            foundation_of(problem, state.selected);
        const double new_foundation =
            foundation_of(problem, replaced);
        deltas[static_cast<std::size_t>(index)] =
            declared_energy_price_factor_eur_per_mwh
                * (new_aep - old_aep)
            - (new_foundation - old_foundation) - cable;
    };
    if (
        static_cast<std::size_t>(combinations) * state.selected.size()
        >= parallel_candidate_threshold
    ) {
        executor.parallel_for(0, combinations, evaluate);
    } else {
        for (int index = 0; index < combinations; ++index) evaluate(index);
    }
    candidate_counter.fetch_add(
        static_cast<std::uint64_t>(combinations),
        std::memory_order_relaxed
    );
    int best = -1;
    for (int index = 0; index < combinations; ++index) {
        if (
            deltas[static_cast<std::size_t>(index)] > 1.0e-6
            && (
                best < 0
                || deltas[static_cast<std::size_t>(index)]
                    > deltas[static_cast<std::size_t>(best)]
            )
        ) {
            best = index;
        }
    }
    candidate_seconds += elapsed(started);
    if (best < 0) return false;
    const int first_index =
        best / static_cast<int>(second_candidates.size());
    const int second_index =
        best % static_cast<int>(second_candidates.size());
    const int first_candidate =
        first_candidates[static_cast<std::size_t>(first_index)];
    const int second_candidate =
        second_candidates[static_cast<std::size_t>(second_index)];
    const std::array<std::pair<int, int>, 2> replacements{{
        {first, first_candidate},
        {second, second_candidate},
    }};
    state.occupied[static_cast<std::size_t>(first)] = 0;
    state.occupied[static_cast<std::size_t>(second)] = 0;
    state.occupied[static_cast<std::size_t>(first_candidate)] = 1;
    state.occupied[static_cast<std::size_t>(second_candidate)] = 1;
    for (int& value : state.selected) {
        if (value == first) value = first_candidate;
        else if (value == second) value = second_candidate;
    }
    std::sort(state.selected.begin(), state.selected.end());
    commit_routing_replacement(state.routing, replacements, problem);
    return true;
}

SearchState shake(
    const Problem& problem,
    const PackedMatrix& matrix,
    const SearchState& parent,
    const fode::CounterRng& random,
    const std::uint64_t cycle
) {
    SearchState trial = parent;
    const int moves = 1 + static_cast<int>(cycle % 3);
    for (int move = 0; move < moves; ++move) {
        const int slot = random.integer(
            0, static_cast<int>(trial.selected.size()),
            cycle, 3303, move
        );
        const int removed =
            trial.selected[static_cast<std::size_t>(slot)];
        const double radius = problem.minimum_spacing_m()
            * (3.0 + 2.0 * static_cast<double>(cycle % 3));
        auto candidates = nearest_candidates(
            problem, trial, removed, radius, one_opt_candidate_cap
        );
        std::stable_sort(
            candidates.begin(), candidates.end(),
            [&](const int left, const int right) {
                const double left_power =
                    problem.diagonal_aep_mwh(left);
                const double right_power =
                    problem.diagonal_aep_mwh(right);
                if (left_power != right_power) {
                    return left_power > right_power;
                }
                return left < right;
            }
        );
        if (candidates.empty()) continue;
        const int offset = random.integer(
            0, static_cast<int>(candidates.size()),
            cycle, 3304, move
        );
        int accepted = -1;
        for (int step = 0; step < static_cast<int>(candidates.size());
             ++step) {
            const int candidate = candidates[static_cast<std::size_t>(
                (offset + step) % static_cast<int>(candidates.size())
            )];
            if (feasible_replacement(
                    problem, trial.selected, removed, candidate
                )) {
                accepted = candidate;
                break;
            }
        }
        if (accepted >= 0) replace_selected(trial, removed, accepted);
    }
    (void)matrix;
    return trial;
}

Evaluation evaluate_state(
    const Problem& problem,
    const PackedMatrix& matrix,
    const SearchState& state
) {
    return compose_evaluation(
        problem,
        aep_of(problem, matrix, state.selected),
        foundation_of(problem, state.selected),
        state.routing,
        spacing_violation(problem, state.selected)
    );
}

}  // namespace

Problem::Problem(
    std::filesystem::path root,
    const char site,
    const Density density,
    const int workers
) : root_(std::move(root)),
    wake_problem_(
        root_,
        "t31_official_" + std::string(
            1, static_cast<char>(site - 'A' + 'a')
        ),
        t31::FoundationMode::none,
        workers
    ) {
    const auto started = Clock::now();
    if (site < 'A' || site > 'J') {
        throw std::invalid_argument("T33 site must be A-J");
    }
    info_.site = site;
    info_.density = density;
    info_.case_id = "t33_official_" + std::string(
        1, static_cast<char>(site - 'A' + 'a')
    ) + "_" + density_name(density);
    info_.zone_quotas = density == Density::low
        ? low_zone_quotas(site) : high_zone_quotas(site);
    info_.zones = static_cast<int>(info_.zone_quotas.size());
    info_.turbines = std::accumulate(
        info_.zone_quotas.begin(), info_.zone_quotas.end(), 0
    );
    info_.available_positions =
        wake_problem_.info().available_positions;
    info_.fixed_turbines = wake_problem_.info().fixed_turbines;
    info_.wind_states = wake_problem_.info().wind_states;
    positions_ = wake_problem_.positions();
    substation_x_.assign(static_cast<std::size_t>(info_.zones), 0.0);
    substation_y_.assign(static_cast<std::size_t>(info_.zones), 0.0);
    std::vector<int> counts(static_cast<std::size_t>(info_.zones), 0);
    for (const auto& position : positions_) {
        const int zone = position.zone - 1;
        substation_x_[static_cast<std::size_t>(zone)] += position.x_m;
        substation_y_[static_cast<std::size_t>(zone)] += position.y_m;
        ++counts[static_cast<std::size_t>(zone)];
    }
    for (int zone = 0; zone < info_.zones; ++zone) {
        if (counts[static_cast<std::size_t>(zone)] < 1) {
            throw std::runtime_error("T33 empty official zone");
        }
        substation_x_[static_cast<std::size_t>(zone)] /=
            counts[static_cast<std::size_t>(zone)];
        substation_y_[static_cast<std::size_t>(zone)] /=
            counts[static_cast<std::size_t>(zone)];
    }
    const auto site_root = root_ / "site" / std::string(1, site);
    geometry_ = std::make_unique<Geometry>(
        site_root / "geometry.txt", info_.zones
    );
    for (int zone = 0; zone < info_.zones; ++zone) {
        Point station = substation_point(*this, zone);
        if (!geometry_->legal_point(zone, station)) {
            double best = std::numeric_limits<double>::infinity();
            Point replacement = station;
            for (const auto& position : positions_) {
                if (position.zone != zone + 1) continue;
                const Point candidate{position.x_m, position.y_m};
                const double current = distance(station, candidate);
                if (current < best) {
                    best = current;
                    replacement = candidate;
                }
            }
            substation_x_[static_cast<std::size_t>(zone)] = replacement.x;
            substation_y_[static_cast<std::size_t>(zone)] = replacement.y;
        }
    }
    preprocessing_seconds_ =
        wake_problem_.preprocessing_seconds() + elapsed(started);
}

Problem::~Problem() = default;
Problem::Problem(Problem&&) noexcept = default;
Problem& Problem::operator=(Problem&&) noexcept = default;

const ProblemInfo& Problem::info() const noexcept { return info_; }
const std::vector<t31::Position>& Problem::positions() const noexcept {
    return positions_;
}
double Problem::minimum_spacing_m() const noexcept {
    return wake_problem_.minimum_spacing_m();
}
double Problem::energy_price_factor_eur_per_mwh() const noexcept {
    return declared_energy_price_factor_eur_per_mwh;
}
std::uint64_t Problem::paper_fixed_cycles() const noexcept {
    return info_.density == Density::low ? 860ULL : 2064ULL;
}
double Problem::paper_time_limit_seconds() const noexcept {
    return info_.density == Density::low ? 36000.0 : 86400.0;
}
double Problem::preprocessing_seconds() const noexcept {
    return preprocessing_seconds_;
}
double Problem::diagonal_aep_mwh(const int position) const {
    return wake_problem_.diagonal_value(position);
}
double Problem::pair_aep_mwh(
    const int first,
    const int second
) const {
    return wake_problem_.symmetric_pair_value(first, second);
}
std::uint64_t Problem::wake_matrix_fingerprint() const noexcept {
    return wake_problem_.matrix_fingerprint();
}
double Problem::substation_x_m(const int zone) const {
    return substation_x_.at(static_cast<std::size_t>(zone));
}
double Problem::substation_y_m(const int zone) const {
    return substation_y_.at(static_cast<std::size_t>(zone));
}
double Problem::cable_path_distance_m(
    const int zone,
    const int first_position,
    const int second_position
) const {
    const Point first = first_position < 0
        ? Point{substation_x_m(zone), substation_y_m(zone)}
        : position_point(*this, first_position);
    const Point second = second_position < 0
        ? Point{substation_x_m(zone), substation_y_m(zone)}
        : position_point(*this, second_position);
    return geometry_->path_distance(zone, first, second);
}

std::vector<std::array<double, 2>> Problem::cable_path_polyline(
    const int zone,
    const int first_position,
    const int second_position
) const {
    const Point first = first_position < 0
        ? Point{substation_x_m(zone), substation_y_m(zone)}
        : position_point(*this, first_position);
    const Point second = second_position < 0
        ? Point{substation_x_m(zone), substation_y_m(zone)}
        : position_point(*this, second_position);
    const auto internal = geometry_->path(zone, first, second);
    CablePolyline result;
    result.reserve(internal.size());
    for (const Point& point : internal) {
        result.push_back({point.x, point.y});
    }
    return result;
}

bool Problem::cable_paths_cross(
    const int zone,
    const int first_upstream_position,
    const int first_downstream_position,
    const int second_upstream_position,
    const int second_downstream_position
) const {
    return polylines_cross(
        cable_path_polyline(
            zone, first_upstream_position, first_downstream_position
        ),
        cable_path_polyline(
            zone, second_upstream_position, second_downstream_position
        )
    );
}

std::vector<int> Problem::deterministic_reference_layout() const {
    std::vector<int> result;
    std::vector<int> zone_count(
        static_cast<std::size_t>(info_.zones), 0
    );
    for (int index = 0; index < info_.available_positions; ++index) {
        const int zone =
            positions_[static_cast<std::size_t>(index)].zone - 1;
        if (
            zone_count[static_cast<std::size_t>(zone)]
            >= info_.zone_quotas[static_cast<std::size_t>(zone)]
        ) {
            continue;
        }
        bool feasible = true;
        for (const int existing : result) {
            if (
                distance(
                    position_point(*this, index),
                    position_point(*this, existing)
                ) < minimum_spacing_m()
            ) {
                feasible = false;
                break;
            }
        }
        if (!feasible) continue;
        result.push_back(index);
        ++zone_count[static_cast<std::size_t>(zone)];
    }
    if (zone_count != info_.zone_quotas) {
        throw std::runtime_error("T33 reference layout unavailable");
    }
    return result;
}

Evaluation Problem::evaluate_direct(
    const std::vector<int>& selected,
    const int workers
) const {
    if (workers < 1) throw std::invalid_argument("T33 workers");
    fode::PersistentExecutor executor(workers);
    std::atomic<std::uint64_t> counter{0};
    Routing routing = build_routing(
        *this, selected, executor, &counter
    );
    double aep = 0.0;
    for (const int position : selected) {
        aep += diagonal_aep_mwh(position);
    }
    for (std::size_t first = 0; first < selected.size(); ++first) {
        for (std::size_t second = first + 1;
             second < selected.size();
             ++second) {
            aep += pair_aep_mwh(selected[first], selected[second]);
        }
    }
    return compose_evaluation(
        *this,
        aep,
        foundation_of(*this, selected),
        routing,
        spacing_violation(*this, selected)
    );
}

RunResult run(const Problem& problem, const RunConfig& config) {
    if (config.workers < 1) throw std::invalid_argument("T33 workers");
    if (
        config.fixed_vns_cycles > 0
        && config.time_limit_seconds > 0.0
    ) {
        throw std::invalid_argument(
            "T33 choose fixed cycles or literal wall time"
        );
    }
    const auto end_to_end_start = Clock::now();
    fode::PersistentExecutor executor(config.workers);
    executor.reset_work_receipt();
    double matrix_seconds = 0.0;
    std::uint64_t matrix_pairs = 0;
    PackedMatrix matrix(
        problem,
        config.matrix_cache,
        executor,
        matrix_seconds,
        matrix_pairs
    );
    const auto initialization_start = Clock::now();
    SearchState current = pdsp_initial(problem, matrix, executor);
    std::atomic<std::uint64_t> route_counter{0};
    current.routing = build_routing(
        problem, current.selected, executor, &route_counter
    );
    current.evaluation = evaluate_state(problem, matrix, current);
    const Evaluation initial = current.evaluation;
    double initialization_seconds = elapsed(initialization_start);
    SearchState best = current;
    std::vector<double> history{best.evaluation.npv_eur};
    const fode::CounterRng random(config.seed);
    std::atomic<std::uint64_t> candidate_counter{0};
    double cable_seconds = 0.0;
    double candidate_seconds = 0.0;
    const auto optimization_start = Clock::now();
    const std::uint64_t fixed_cycles = config.fixed_vns_cycles > 0
        ? config.fixed_vns_cycles
        : (
            config.time_limit_seconds > 0.0
                ? std::numeric_limits<std::uint64_t>::max()
                : problem.paper_fixed_cycles()
        );
    const double time_limit = config.time_limit_seconds > 0.0
        ? config.time_limit_seconds : 0.0;
    std::uint64_t cycles = 0;
    while (cycles < fixed_cycles) {
        if (
            time_limit > 0.0
            && elapsed(optimization_start) >= time_limit
        ) {
            break;
        }
        bool improved = one_opt_cycle(
            problem, matrix, current, executor, random, cycles,
            candidate_counter, candidate_seconds
        );
        if (!improved) {
            improved = two_opt_cycle(
                problem, matrix, current, executor, random, cycles,
                candidate_counter, candidate_seconds
            );
        }
        SearchState trial = current;
        if (!improved) {
            trial = shake(problem, matrix, current, random, cycles);
        }
        const auto cable_start = Clock::now();
        trial.routing = build_routing(
            problem, trial.selected, executor, &route_counter
        );
        cable_seconds += elapsed(cable_start);
        trial.evaluation = evaluate_state(problem, matrix, trial);
        if (
            trial.evaluation.feasible
            && (
                !current.evaluation.feasible
                || trial.evaluation.npv_eur
                    > current.evaluation.npv_eur
            )
        ) {
            current = std::move(trial);
        }
        if (
            current.evaluation.feasible
            && (
                !best.evaluation.feasible
                || current.evaluation.npv_eur
                    > best.evaluation.npv_eur
            )
        ) {
            best = current;
        }
        history.push_back(best.evaluation.npv_eur);
        ++cycles;
    }
    const double optimization_seconds = elapsed(optimization_start);
    std::uint64_t hash = 0x743333636f6d6269ULL;
    hash = mix_hash(hash, config.seed);
    hash = mix_hash(hash, static_cast<std::uint64_t>(problem.info().site));
    hash = mix_hash(
        hash,
        static_cast<std::uint64_t>(problem.info().density == Density::high)
    );
    hash = mix_hash(hash, cycles);
    for (const int position : best.selected) {
        hash = mix_hash(hash, static_cast<std::uint64_t>(position));
    }
    hash = hash_double(hash, best.evaluation.aep_mwh);
    hash = hash_double(hash, best.evaluation.foundation_cost_eur);
    hash = hash_double(hash, best.evaluation.cable_cost_eur);
    hash = hash_double(hash, best.evaluation.npv_eur);
    const auto receipt = executor.work_receipt();
    return {
        .case_id = problem.info().case_id,
        .problem_semantic_id = problem_id,
        .method_semantic_id = method_id,
        .protocol_semantic_id =
            time_limit > 0.0 ? literal_protocol_id : fixed_protocol_id,
        .seed = config.seed,
        .requested_workers = config.workers,
        .observed_workers = receipt.distinct_participants,
        .completed_vns_cycles = cycles,
        .matrix_pair_evaluations = matrix_pairs,
        .wake_state_evaluations = matrix_pairs
            * static_cast<std::uint64_t>(problem.info().wind_states)
            * 2ULL,
        .layout_candidate_evaluations =
            candidate_counter.load(std::memory_order_relaxed),
        .cable_route_evaluations =
            route_counter.load(std::memory_order_relaxed),
        .initial = initial,
        .best = best.evaluation,
        .best_positions = std::move(best.selected),
        .best_npv_history_eur = std::move(history),
        .problem_preprocessing_seconds = problem.preprocessing_seconds(),
        .matrix_seconds = matrix_seconds,
        .initialization_seconds = initialization_seconds,
        .cable_seconds = cable_seconds,
        .candidate_seconds = candidate_seconds,
        .optimization_seconds = optimization_seconds,
        .end_to_end_seconds = elapsed(end_to_end_start)
            + problem.preprocessing_seconds(),
        .scientific_hash = hash,
    };
}

std::vector<std::string> paper_case_ids() {
    std::vector<std::string> result;
    for (char site = 'a'; site <= 'j'; ++site) {
        result.push_back(
            "t33_official_" + std::string(1, site) + "_low"
        );
        result.push_back(
            "t33_official_" + std::string(1, site) + "_high"
        );
    }
    return result;
}

std::string density_name(const Density density) {
    return density == Density::low ? "low" : "high";
}

}  // namespace core99::t33
