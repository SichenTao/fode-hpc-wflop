/*
WFLOP IMPLEMENTATION FACT DECLARATION
Implementation unit: T31 pure-C++ official-data VNS and packed power matrix
Paper title: Variable Neighborhood Search for Large Offshore Wind Farm Layout
Optimization
Paper DOI: 10.1016/j.cor.2021.105588
Dataset DOI/source/missing/completions/HPC route/claim boundary:
include/core99/cazzaro_t31.hpp.
Independent oracle: scripts/validate_core99_t31.py.
Contract: shared/contracts/core99_t31_cazzaro_vns_2022.json.
Last evidence-audit date: 2026-07-31
END WFLOP IMPLEMENTATION FACT DECLARATION
*/
#include "core99/cazzaro_t31.hpp"

#include "fode/rng.hpp"

#include <algorithm>
#include <array>
#include <atomic>
#include <bit>
#include <chrono>
#include <cmath>
#include <cstddef>
#include <cstdint>
#include <cstdlib>
#include <cstring>
#include <deque>
#include <fcntl.h>
#include <fstream>
#include <limits>
#include <map>
#include <numbers>
#include <numeric>
#include <optional>
#include <span>
#include <stdexcept>
#include <string>
#include <string_view>
#include <sys/mman.h>
#include <sys/stat.h>
#include <unistd.h>
#include <utility>
#include <vector>

namespace core99::t31 {
namespace {

using Clock = std::chrono::steady_clock;
constexpr double hours_per_year = 8760.0;
constexpr double low_cost_energy_price_eur_per_mwh = 200.0;
constexpr double high_cost_energy_price_eur_per_mwh = 40.0;
constexpr double spacing_penalty_mwh_per_m = 1.0e8;
constexpr double new_rotor_diameter_m = 240.0;
constexpr double fixed_rotor_diameter_m = 179.0;
constexpr double declared_wake_expansion = 0.04;
constexpr double declared_spacing_diameters = 5.0;
constexpr int one_opt_candidate_cap = 128;
constexpr int two_opt_pair_cap = 32;
constexpr int two_opt_candidate_cap = 16;
constexpr std::size_t parallel_delta_arithmetic_threshold = 20000;
constexpr double conic_half_angle_degrees = 30.0;
constexpr const char* method_id =
    "t31_pdsp_random_conic_vns_declared_v1";
constexpr const char* protocol_id =
    "t31_3x30s_10x3x1h_10x10h_v1";

double elapsed(const Clock::time_point start) {
    return std::chrono::duration<double>(Clock::now() - start).count();
}

std::uint64_t mix_hash(std::uint64_t hash, const std::uint64_t value) {
    hash ^= value + 0x9e3779b97f4a7c15ULL
        + (hash << 6U) + (hash >> 2U);
    return hash;
}

std::uint64_t string_hash(const std::string_view value) {
    std::uint64_t hash = 1469598103934665603ULL;
    for (const unsigned char byte : value) {
        hash ^= static_cast<std::uint64_t>(byte);
        hash *= 1099511628211ULL;
    }
    return hash;
}

double circle_overlap(
    const double distance,
    const double first_radius,
    const double second_radius
) {
    if (distance >= first_radius + second_radius) return 0.0;
    if (distance <= std::abs(first_radius - second_radius)) {
        const double radius = std::min(first_radius, second_radius);
        return std::numbers::pi * radius * radius;
    }
    const double first_angle = std::acos(std::clamp(
        (
            distance * distance + first_radius * first_radius
            - second_radius * second_radius
        ) / (2.0 * distance * first_radius),
        -1.0,
        1.0
    ));
    const double second_angle = std::acos(std::clamp(
        (
            distance * distance + second_radius * second_radius
            - first_radius * first_radius
        ) / (2.0 * distance * second_radius),
        -1.0,
        1.0
    ));
    const double radicand = std::max(
        0.0,
        (-distance + first_radius + second_radius)
        * (distance + first_radius - second_radius)
        * (distance - first_radius + second_radius)
        * (distance + first_radius + second_radius)
    );
    return first_radius * first_radius * first_angle
        + second_radius * second_radius * second_angle
        - 0.5 * std::sqrt(radicand);
}

std::vector<double> parse_numbers(const std::string& line) {
    std::vector<double> result;
    const char* cursor = line.c_str();
    char* end = nullptr;
    while (*cursor != '\0') {
        const double value = std::strtod(cursor, &end);
        if (end == cursor) {
            ++cursor;
            continue;
        }
        result.push_back(value);
        cursor = end;
    }
    return result;
}

std::vector<Position> read_positions(const std::filesystem::path& path) {
    std::ifstream stream(path);
    if (!stream) {
        throw std::runtime_error("T31 cannot read " + path.string());
    }
    std::vector<Position> result;
    std::string line;
    while (std::getline(stream, line)) {
        const auto values = parse_numbers(line);
        if (values.empty()) continue;
        if (values.size() != 5) {
            throw std::runtime_error("T31 malformed positions file");
        }
        result.push_back({
            values[0],
            values[1],
            values[2],
            values[3],
            static_cast<int>(std::llround(values[4])),
        });
    }
    return result;
}

std::vector<std::array<double, 3>> read_three_column(
    const std::filesystem::path& path
) {
    std::ifstream stream(path);
    if (!stream) {
        throw std::runtime_error("T31 cannot read " + path.string());
    }
    std::vector<std::array<double, 3>> result;
    std::string line;
    while (std::getline(stream, line)) {
        const auto values = parse_numbers(line);
        if (values.empty()) continue;
        if (values.size() != 3) {
            throw std::runtime_error("T31 malformed three-column file");
        }
        result.push_back({values[0], values[1], values[2]});
    }
    return result;
}

std::vector<int> quotas_for(const char site) {
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
    default: throw std::invalid_argument("T31 invalid official site");
    }
}

int mosetti_quota(const std::string& case_id) {
    if (case_id == "t31_mosetti_di") return 26;
    if (case_id == "t31_mosetti_dplus_i") return 19;
    if (case_id == "t31_mosetti_dplus_iplus") return 15;
    throw std::invalid_argument("T31 invalid Mosetti case");
}

double declared_energy_price(const FoundationMode mode) {
    if (mode == FoundationMode::low_cost) {
        return low_cost_energy_price_eur_per_mwh;
    }
    if (mode == FoundationMode::high_cost) {
        return high_cost_energy_price_eur_per_mwh;
    }
    return std::numeric_limits<double>::infinity();
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

std::size_t pair_count(const int count) {
    return static_cast<std::size_t>(count)
        * static_cast<std::size_t>(count - 1) / 2U;
}

std::size_t pair_index(int first, int second, const int count) {
    if (first == second) {
        throw std::invalid_argument("T31 diagonal pair index");
    }
    if (first > second) std::swap(first, second);
    return static_cast<std::size_t>(first)
            * static_cast<std::size_t>(2 * count - first - 1) / 2U
        + static_cast<std::size_t>(second - first - 1);
}

}  // namespace

class PackedMatrix {
public:
    PackedMatrix(
        const Problem& problem,
        const std::filesystem::path& cache,
        fode::PersistentExecutor& executor,
        double& construction_seconds,
        std::uint64_t& pair_evaluations
    ) : count_(problem.info().available_positions),
        size_(pair_count(count_)) {
        fingerprint_ = problem.matrix_fingerprint();
        if (!cache.empty()) {
            open_mapping(cache);
            if (std::memcmp(header_->magic, "T31PAIR", 7) == 0
                && header_->complete == 1
                && header_->version == 1
                && header_->positions
                    == static_cast<std::uint64_t>(count_)
                && header_->pairs == static_cast<std::uint64_t>(size_)
                && header_->fingerprint == fingerprint_) {
                construction_seconds = 0.0;
                pair_evaluations = 0;
                return;
            }
            initialize_header();
        } else {
            owned_.resize(size_);
            data_ = owned_.data();
        }
        const auto start = Clock::now();
        executor.parallel_for(0, count_, [&](const int first) {
            for (int second = first + 1; second < count_; ++second) {
                data_[pair_index(first, second, count_)] =
                    static_cast<float>(
                        problem.symmetric_pair_value(first, second)
                    );
            }
        });
        construction_seconds = elapsed(start);
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

    void open_mapping(const std::filesystem::path& cache) {
        if (!cache.parent_path().empty()) {
            std::filesystem::create_directories(cache.parent_path());
        }
        file_ = ::open(
            cache.c_str(),
            O_RDWR | O_CREAT,
            S_IRUSR | S_IWUSR | S_IRGRP
        );
        if (file_ < 0) {
            throw std::runtime_error("T31 cannot open matrix cache");
        }
        mapping_bytes_ = matrix_header_bytes + size_ * sizeof(float);
        struct stat state{};
        if (::fstat(file_, &state) != 0) {
            throw std::runtime_error("T31 matrix cache stat");
        }
        if (static_cast<std::size_t>(state.st_size) != mapping_bytes_) {
            if (::ftruncate(
                    file_, static_cast<off_t>(mapping_bytes_)
                ) != 0) {
                throw std::runtime_error("T31 matrix cache resize");
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
            throw std::runtime_error("T31 matrix cache mmap");
        }
        header_ = static_cast<MatrixHeader*>(mapping_);
        data_ = reinterpret_cast<float*>(
            static_cast<std::byte*>(mapping_) + matrix_header_bytes
        );
    }

    void initialize_header() {
        std::memset(mapping_, 0, matrix_header_bytes);
        std::memcpy(header_->magic, "T31PAIR", 7);
        header_->version = 1;
        header_->positions = static_cast<std::uint64_t>(count_);
        header_->pairs = static_cast<std::uint64_t>(size_);
        header_->fingerprint = fingerprint_;
        header_->complete = 0;
    }
};

namespace {

struct SearchState {
    std::vector<int> selected;
    std::vector<unsigned char> occupied;
    double objective = -std::numeric_limits<double>::infinity();
};

double matrix_objective(
    const Problem& problem,
    const PackedMatrix& matrix,
    const std::vector<int>& selected
) {
    double value = 0.0;
    for (std::size_t first = 0; first < selected.size(); ++first) {
        value += problem.diagonal_value(selected[first]);
        for (std::size_t second = first + 1;
             second < selected.size();
             ++second) {
            value += matrix.pair(selected[first], selected[second]);
        }
    }
    return value;
}

bool feasible_candidate(
    const Problem& problem,
    const std::vector<int>& selected,
    const int removed,
    const int candidate,
    const int second_removed = -1,
    const int second_candidate = -1
) {
    const Position& proposed = problem.positions()[candidate];
    const double spacing = problem.minimum_spacing_m();
    if (
        removed >= 0
        && problem.positions()[removed].zone != proposed.zone
    ) {
        return false;
    }
    if (
        second_candidate >= 0
        && candidate != second_candidate
        && std::hypot(
            proposed.x_m - problem.positions()[second_candidate].x_m,
            proposed.y_m - problem.positions()[second_candidate].y_m
        ) < spacing
    ) {
        return false;
    }
    for (const int existing : selected) {
        if (existing == removed || existing == second_removed) continue;
        const Position& current = problem.positions()[existing];
        if (
            std::hypot(
                proposed.x_m - current.x_m,
                proposed.y_m - current.y_m
            ) < spacing
        ) {
            return false;
        }
    }
    return true;
}

double one_delta(
    const Problem& problem,
    const PackedMatrix& matrix,
    const std::vector<int>& selected,
    const int removed,
    const int candidate
) {
    double value = problem.diagonal_value(candidate)
        - problem.diagonal_value(removed);
    for (const int existing : selected) {
        if (existing == removed) continue;
        value += matrix.pair(candidate, existing)
            - matrix.pair(removed, existing);
    }
    return value;
}

double two_delta(
    const Problem& problem,
    const PackedMatrix& matrix,
    const std::vector<int>& selected,
    const int first_removed,
    const int second_removed,
    const int first_candidate,
    const int second_candidate
) {
    double value =
        problem.diagonal_value(first_candidate)
        + problem.diagonal_value(second_candidate)
        - problem.diagonal_value(first_removed)
        - problem.diagonal_value(second_removed);
    value += matrix.pair(first_candidate, second_candidate)
        - matrix.pair(first_removed, second_removed);
    for (const int existing : selected) {
        if (existing == first_removed || existing == second_removed) {
            continue;
        }
        value += matrix.pair(first_candidate, existing)
            + matrix.pair(second_candidate, existing)
            - matrix.pair(first_removed, existing)
            - matrix.pair(second_removed, existing);
    }
    return value;
}

std::vector<int> nearest_candidates(
    const Problem& problem,
    const SearchState& state,
    const int origin,
    const double radius,
    const int cap
) {
    const Position& center = problem.positions()[origin];
    std::vector<std::pair<double, int>> candidates;
    candidates.reserve(256);
    for (int index = 0;
         index < problem.info().available_positions;
         ++index) {
        if (state.occupied[index]) continue;
        const Position& position = problem.positions()[index];
        if (position.zone != center.zone) continue;
        const double distance = std::hypot(
            position.x_m - center.x_m,
            position.y_m - center.y_m
        );
        if (distance <= radius) candidates.emplace_back(distance, index);
    }
    std::stable_sort(
        candidates.begin(),
        candidates.end(),
        [](const auto& left, const auto& right) {
            if (left.first != right.first) return left.first < right.first;
            return left.second < right.second;
        }
    );
    if (static_cast<int>(candidates.size()) > cap) {
        candidates.resize(static_cast<std::size_t>(cap));
    }
    std::vector<int> result;
    result.reserve(candidates.size());
    for (const auto& [distance, index] : candidates) {
        (void)distance;
        result.push_back(index);
    }
    return result;
}

void replace_position(
    SearchState& state,
    const int old_position,
    const int new_position
) {
    const auto iterator = std::find(
        state.selected.begin(), state.selected.end(), old_position
    );
    if (iterator == state.selected.end()) {
        throw std::runtime_error("T31 selected position missing");
    }
    state.occupied[old_position] = 0;
    state.occupied[new_position] = 1;
    *iterator = new_position;
    std::sort(state.selected.begin(), state.selected.end());
}

SearchState pdsp_initial(
    const Problem& problem,
    const PackedMatrix& matrix,
    fode::PersistentExecutor& executor
) {
    const int count = problem.info().available_positions;
    std::vector<unsigned char> active(
        static_cast<std::size_t>(count), 1
    );
    std::vector<double> contribution(static_cast<std::size_t>(count));
    executor.parallel_for(0, count, [&](const int position) {
        double value = problem.diagonal_value(position);
        for (int other = 0; other < count; ++other) {
            if (other != position) value += matrix.pair(position, other);
        }
        contribution[position] = value;
    });
    std::vector<int> active_by_zone(
        problem.info().zone_quotas.size(), 0
    );
    for (const auto& position : problem.positions()) {
        ++active_by_zone[static_cast<std::size_t>(position.zone - 1)];
    }
    while (true) {
        std::vector<int> removed;
        for (int zone = 1;
             zone <= static_cast<int>(active_by_zone.size());
             ++zone) {
            const int quota =
                problem.info().zone_quotas[static_cast<std::size_t>(zone - 1)];
            const int excess =
                active_by_zone[static_cast<std::size_t>(zone - 1)] - quota;
            if (excess <= 0) continue;
            const int batch = std::min(
                excess, std::max(1, excess / 20)
            );
            std::vector<int> order;
            for (int index = 0; index < count; ++index) {
                if (
                    active[index]
                    && problem.positions()[index].zone == zone
                ) {
                    order.push_back(index);
                }
            }
            std::stable_sort(
                order.begin(),
                order.end(),
                [&](const int left, const int right) {
                    if (contribution[left] != contribution[right]) {
                        return contribution[left] < contribution[right];
                    }
                    return left < right;
                }
            );
            order.resize(static_cast<std::size_t>(batch));
            for (const int index : order) {
                active[index] = 0;
                removed.push_back(index);
            }
            active_by_zone[static_cast<std::size_t>(zone - 1)] -= batch;
        }
        if (removed.empty()) break;
        executor.parallel_for(0, count, [&](const int position) {
            if (!active[position]) return;
            double decrement = 0.0;
            for (const int erased : removed) {
                decrement += matrix.pair(position, erased);
            }
            contribution[position] -= decrement;
        });
    }
    SearchState state;
    state.occupied.assign(static_cast<std::size_t>(count), 0);
    for (int position = 0; position < count; ++position) {
        if (active[position]) {
            state.selected.push_back(position);
            state.occupied[position] = 1;
        }
    }
    std::sort(state.selected.begin(), state.selected.end());
    state.objective = matrix_objective(problem, matrix, state.selected);

    // The paper permits an infeasible PDSP seed and relies on the penalty.
    // The declared completion first attempts a minimal single-position repair.
    // If a dense instance has no feasible one-move repair, it deterministically
    // rebuilds the requested zone cardinalities from the PDSP priority order
    // and declared counter-keyed alternative orders.
    auto feasible_rebuild = [&]() -> std::optional<SearchState> {
        const int requested = std::accumulate(
            problem.info().zone_quotas.begin(),
            problem.info().zone_quotas.end(),
            0
        );
        SearchState best_rebuild;
        for (int trial = 0; trial < 32; ++trial) {
            std::vector<int> order(static_cast<std::size_t>(count));
            std::iota(order.begin(), order.end(), 0);
            std::stable_sort(
                order.begin(),
                order.end(),
                [&](const int left, const int right) {
                    if (trial == 0) {
                        if (state.occupied[left] != state.occupied[right]) {
                            return state.occupied[left]
                                > state.occupied[right];
                        }
                        if (contribution[left] != contribution[right]) {
                            return contribution[left]
                                > contribution[right];
                        }
                        return left < right;
                    }
                    const auto key = [trial](const int position) {
                        return mix_hash(
                            0x31f3a51b0dULL
                                ^ static_cast<std::uint64_t>(trial),
                            static_cast<std::uint64_t>(position)
                        );
                    };
                    const std::uint64_t left_key = key(left);
                    const std::uint64_t right_key = key(right);
                    if (left_key != right_key) {
                        return left_key < right_key;
                    }
                    return left < right;
                }
            );
            SearchState rebuilt;
            rebuilt.occupied.assign(static_cast<std::size_t>(count), 0);
            std::vector<int> zone_count(
                problem.info().zone_quotas.size(), 0
            );
            for (const int candidate : order) {
                const int zone = problem.positions()[candidate].zone - 1;
                if (
                    zone_count[static_cast<std::size_t>(zone)]
                    >= problem.info().zone_quotas[
                        static_cast<std::size_t>(zone)
                    ]
                ) {
                    continue;
                }
                bool feasible = true;
                for (const int existing : rebuilt.selected) {
                    if (std::hypot(
                            problem.positions()[candidate].x_m
                                - problem.positions()[existing].x_m,
                            problem.positions()[candidate].y_m
                                - problem.positions()[existing].y_m
                        ) < problem.minimum_spacing_m()) {
                        feasible = false;
                        break;
                    }
                }
                if (!feasible) continue;
                rebuilt.selected.push_back(candidate);
                rebuilt.occupied[candidate] = 1;
                ++zone_count[static_cast<std::size_t>(zone)];
                if (
                    static_cast<int>(rebuilt.selected.size()) == requested
                ) {
                    break;
                }
            }
            if (
                static_cast<int>(rebuilt.selected.size()) != requested
                || zone_count != problem.info().zone_quotas
            ) {
                continue;
            }
            std::sort(rebuilt.selected.begin(), rebuilt.selected.end());
            rebuilt.objective = matrix_objective(
                problem, matrix, rebuilt.selected
            );
            if (
                best_rebuild.selected.empty()
                || rebuilt.objective > best_rebuild.objective
            ) {
                best_rebuild = std::move(rebuilt);
            }
        }
        if (best_rebuild.selected.empty()) return std::nullopt;
        return best_rebuild;
    };

    for (int pass = 0; pass < count; ++pass) {
        int conflict = -1;
        int mate = -1;
        for (std::size_t first = 0;
             first < state.selected.size() && conflict < 0;
             ++first) {
            for (std::size_t second = first + 1;
                 second < state.selected.size();
                 ++second) {
                const int a = state.selected[first];
                const int b = state.selected[second];
                if (std::hypot(
                        problem.positions()[a].x_m
                            - problem.positions()[b].x_m,
                        problem.positions()[a].y_m
                            - problem.positions()[b].y_m
                    ) < problem.minimum_spacing_m()) {
                    conflict = a;
                    mate = b;
                    break;
                }
            }
        }
        if (conflict < 0) break;
        if (
            contribution[mate] < contribution[conflict]
            || (
                contribution[mate] == contribution[conflict]
                && mate < conflict
            )
        ) {
            conflict = mate;
        }
        double best_delta = -std::numeric_limits<double>::infinity();
        int best = -1;
        for (int candidate = 0; candidate < count; ++candidate) {
            if (
                state.occupied[candidate]
                || problem.positions()[candidate].zone
                    != problem.positions()[conflict].zone
                || !feasible_candidate(
                    problem,
                    state.selected,
                    conflict,
                    candidate
                )
            ) {
                continue;
            }
            const double delta = one_delta(
                problem, matrix, state.selected, conflict, candidate
            );
            if (delta > best_delta) {
                best_delta = delta;
                best = candidate;
            }
        }
        if (best < 0) {
            const auto rebuilt = feasible_rebuild();
            if (!rebuilt) {
                throw std::runtime_error(
                    "T31 feasible PDSP reconstruction exhausted"
                );
            }
            return *rebuilt;
        }
        replace_position(state, conflict, best);
        state.objective += best_delta;
    }
    return state;
}

bool one_opt(
    const Problem& problem,
    const PackedMatrix& matrix,
    SearchState& state,
    fode::PersistentExecutor& executor,
    std::atomic<std::uint64_t>& candidates,
    const double radius,
    const Clock::time_point deadline,
    const bool timed
) {
    bool changed = false;
    const std::vector<int> sweep = state.selected;
    for (const int removed : sweep) {
        if (
            timed && Clock::now() >= deadline
        ) {
            break;
        }
        if (!state.occupied[removed]) continue;
        const auto alternatives = nearest_candidates(
            problem,
            state,
            removed,
            radius,
            one_opt_candidate_cap
        );
        std::vector<double> deltas(
            alternatives.size(),
            -std::numeric_limits<double>::infinity()
        );
        auto evaluate_slot = [&](const int slot) {
                const int candidate = alternatives[slot];
                if (feasible_candidate(
                        problem,
                        state.selected,
                        removed,
                        candidate
                    )) {
                    deltas[slot] = one_delta(
                        problem,
                        matrix,
                        state.selected,
                        removed,
                        candidate
                    );
                }
        };
        if (
            alternatives.size() * state.selected.size()
            >= parallel_delta_arithmetic_threshold
        ) {
            executor.parallel_for(
                0,
                static_cast<int>(alternatives.size()),
                evaluate_slot
            );
        } else {
            for (int slot = 0;
                 slot < static_cast<int>(alternatives.size());
                 ++slot) {
                evaluate_slot(slot);
            }
        }
        candidates.fetch_add(
            alternatives.size(), std::memory_order_relaxed
        );
        int best_slot = -1;
        for (int slot = 0;
             slot < static_cast<int>(alternatives.size());
             ++slot) {
            if (
                deltas[slot] > 1.0e-9
                && (
                    best_slot < 0
                    || deltas[slot] > deltas[best_slot]
                    || (
                        deltas[slot] == deltas[best_slot]
                        && alternatives[slot] < alternatives[best_slot]
                    )
                )
            ) {
                best_slot = slot;
            }
        }
        if (best_slot >= 0) {
            replace_position(
                state, removed, alternatives[best_slot]
            );
            state.objective += deltas[best_slot];
            changed = true;
        }
    }
    return changed;
}

bool two_opt(
    const Problem& problem,
    const PackedMatrix& matrix,
    SearchState& state,
    fode::PersistentExecutor& executor,
    std::atomic<std::uint64_t>& candidates,
    const double radius,
    const Clock::time_point deadline,
    const bool timed
) {
    struct Pair {
        double distance = 0.0;
        int first = 0;
        int second = 0;
    };
    std::vector<Pair> pairs;
    for (std::size_t first = 0; first < state.selected.size(); ++first) {
        for (std::size_t second = first + 1;
             second < state.selected.size();
             ++second) {
            const int a = state.selected[first];
            const int b = state.selected[second];
            const double distance = std::hypot(
                problem.positions()[a].x_m
                    - problem.positions()[b].x_m,
                problem.positions()[a].y_m
                    - problem.positions()[b].y_m
            );
            if (distance <= radius) pairs.push_back({distance, a, b});
        }
    }
    std::stable_sort(
        pairs.begin(), pairs.end(), [](const Pair& left, const Pair& right) {
            if (left.distance != right.distance) {
                return left.distance < right.distance;
            }
            if (left.first != right.first) return left.first < right.first;
            return left.second < right.second;
        }
    );
    if (static_cast<int>(pairs.size()) > two_opt_pair_cap) {
        pairs.resize(two_opt_pair_cap);
    }
    for (const Pair pair : pairs) {
        if (timed && Clock::now() >= deadline) break;
        if (
            !state.occupied[pair.first]
            || !state.occupied[pair.second]
        ) {
            continue;
        }
        const auto first_candidates = nearest_candidates(
            problem,
            state,
            pair.first,
            radius,
            two_opt_candidate_cap
        );
        const auto second_candidates = nearest_candidates(
            problem,
            state,
            pair.second,
            radius,
            two_opt_candidate_cap
        );
        const int combinations = static_cast<int>(
            first_candidates.size() * second_candidates.size()
        );
        std::vector<double> deltas(
            static_cast<std::size_t>(combinations),
            -std::numeric_limits<double>::infinity()
        );
        auto evaluate_slot = [&](const int slot) {
            const int first_slot =
                slot / static_cast<int>(second_candidates.size());
            const int second_slot =
                slot % static_cast<int>(second_candidates.size());
            const int first_candidate = first_candidates[first_slot];
            const int second_candidate = second_candidates[second_slot];
            if (
                first_candidate == second_candidate
                || !feasible_candidate(
                    problem,
                    state.selected,
                    pair.first,
                    first_candidate,
                    pair.second,
                    second_candidate
                )
                || !feasible_candidate(
                    problem,
                    state.selected,
                    pair.second,
                    second_candidate,
                    pair.first,
                    first_candidate
                )
            ) {
                return;
            }
            deltas[slot] = two_delta(
                problem,
                matrix,
                state.selected,
                pair.first,
                pair.second,
                first_candidate,
                second_candidate
            );
        };
        if (
            static_cast<std::size_t>(combinations)
                * state.selected.size()
            >= parallel_delta_arithmetic_threshold
        ) {
            executor.parallel_for(0, combinations, evaluate_slot);
        } else {
            for (int slot = 0; slot < combinations; ++slot) {
                evaluate_slot(slot);
            }
        }
        candidates.fetch_add(
            static_cast<std::uint64_t>(combinations),
            std::memory_order_relaxed
        );
        int best_slot = -1;
        for (int slot = 0; slot < combinations; ++slot) {
            if (
                deltas[slot] > 1.0e-9
                && (best_slot < 0 || deltas[slot] > deltas[best_slot])
            ) {
                best_slot = slot;
            }
        }
        if (best_slot >= 0) {
            const int first_slot =
                best_slot / static_cast<int>(second_candidates.size());
            const int second_slot =
                best_slot % static_cast<int>(second_candidates.size());
            const int first_candidate = first_candidates[first_slot];
            const int second_candidate = second_candidates[second_slot];
            state.occupied[pair.first] = 0;
            state.occupied[pair.second] = 0;
            state.occupied[first_candidate] = 1;
            state.occupied[second_candidate] = 1;
            for (int& value : state.selected) {
                if (value == pair.first) value = first_candidate;
                else if (value == pair.second) value = second_candidate;
            }
            std::sort(state.selected.begin(), state.selected.end());
            state.objective += deltas[best_slot];
            return true;
        }
    }
    return false;
}

SearchState shake(
    const Problem& problem,
    const PackedMatrix& matrix,
    const SearchState& parent,
    fode::PersistentExecutor& executor,
    const fode::CounterRng& random,
    const std::uint64_t iteration,
    const int neighborhood,
    const ShakeMode configured_mode
) {
    auto active_modes = [](const ShakeMode mode) {
        switch (mode) {
        case ShakeMode::circular:
            return std::vector<ShakeMode>{ShakeMode::circular};
        case ShakeMode::conic:
            return std::vector<ShakeMode>{ShakeMode::conic};
        case ShakeMode::directional:
            return std::vector<ShakeMode>{ShakeMode::directional};
        case ShakeMode::displacement:
            return std::vector<ShakeMode>{ShakeMode::displacement};
        case ShakeMode::random:
            return std::vector<ShakeMode>{ShakeMode::random};
        case ShakeMode::random_directional:
            return std::vector<ShakeMode>{
                ShakeMode::random, ShakeMode::directional
            };
        case ShakeMode::circular_displacement:
            return std::vector<ShakeMode>{
                ShakeMode::circular, ShakeMode::displacement
            };
        case ShakeMode::random_conic:
            return std::vector<ShakeMode>{
                ShakeMode::random, ShakeMode::conic
            };
        case ShakeMode::directional_conic:
            return std::vector<ShakeMode>{
                ShakeMode::directional, ShakeMode::conic
            };
        case ShakeMode::all:
            return std::vector<ShakeMode>{
                ShakeMode::circular,
                ShakeMode::conic,
                ShakeMode::directional,
                ShakeMode::displacement,
                ShakeMode::random,
            };
        }
        throw std::invalid_argument("T31 shake mode");
    };
    const auto modes = active_modes(configured_mode);
    const ShakeMode mode = modes[static_cast<std::size_t>(random.integer(
        0,
        static_cast<int>(modes.size()),
        iteration,
        309,
        0
    ))];
    SearchState trial = parent;
    const int selected_slot = random.integer(
        0,
        static_cast<int>(trial.selected.size()),
        iteration,
        310,
        0
    );
    const int center_index = trial.selected[selected_slot];
    const Position center = problem.positions()[center_index];
    const double radius = problem.minimum_spacing_m()
        * std::array<double, 3>{3.0, 5.0, 8.0}
            [static_cast<std::size_t>(neighborhood - 1)];
    std::vector<int> affected;
    for (const int position : trial.selected) {
        const double dx = problem.positions()[position].x_m - center.x_m;
        const double dy = problem.positions()[position].y_m - center.y_m;
        const double distance = std::hypot(dx, dy);
        if (distance > radius) continue;
        if (mode == ShakeMode::conic) {
            // The prevailing TNW wind is represented by a deterministic
            // 240-degree coming-from direction; flow points at 60 degrees.
            const double angle = std::atan2(dy, dx) * 180.0
                / std::numbers::pi;
            double difference = std::remainder(angle - 60.0, 360.0);
            if (std::abs(difference) > conic_half_angle_degrees) continue;
        }
        affected.push_back(position);
    }
    if (affected.empty()) affected.push_back(center_index);
    const int move_cap = std::min(
        static_cast<int>(affected.size()), 2 * neighborhood
    );

    auto best_candidate = [&](
        const int removed,
        std::vector<int> alternatives,
        const bool require_feasible,
        const bool directional
    ) {
        if (directional) {
            const Position& origin = problem.positions()[removed];
            constexpr double direction =
                150.0 * std::numbers::pi / 180.0;
            std::erase_if(alternatives, [&](const int candidate) {
                const Position& target = problem.positions()[candidate];
                return (target.x_m - origin.x_m) * std::cos(direction)
                    + (target.y_m - origin.y_m) * std::sin(direction)
                    <= 0.0;
            });
        }
        std::vector<double> deltas(
            alternatives.size(),
            -std::numeric_limits<double>::infinity()
        );
        auto evaluate_slot = [&](const int slot) {
                const int candidate = alternatives[slot];
                if (
                    !require_feasible
                    || feasible_candidate(
                        problem,
                        trial.selected,
                        removed,
                        candidate
                    )
                ) {
                    deltas[slot] = one_delta(
                        problem,
                        matrix,
                        trial.selected,
                        removed,
                        candidate
                    );
                }
        };
        if (
            alternatives.size() * trial.selected.size()
            >= parallel_delta_arithmetic_threshold
        ) {
            executor.parallel_for(
                0,
                static_cast<int>(alternatives.size()),
                evaluate_slot
            );
        } else {
            for (int slot = 0;
                 slot < static_cast<int>(alternatives.size());
                 ++slot) {
                evaluate_slot(slot);
            }
        }
        int best_slot = -1;
        for (int slot = 0;
             slot < static_cast<int>(alternatives.size());
             ++slot) {
            if (
                std::isfinite(deltas[slot])
                && (
                    best_slot < 0
                    || deltas[slot] > deltas[best_slot]
                    || (
                        deltas[slot] == deltas[best_slot]
                        && alternatives[slot] < alternatives[best_slot]
                    )
                )
            ) {
                best_slot = slot;
            }
        }
        return best_slot < 0 ? -1 : alternatives[best_slot];
    };

    if (mode == ShakeMode::displacement) {
        const auto alternatives = nearest_candidates(
            problem,
            trial,
            center_index,
            radius,
            one_opt_candidate_cap
        );
        const int candidate = best_candidate(
            center_index, alternatives, false, false
        );
        if (candidate < 0) return trial;
        const double delta = one_delta(
            problem, matrix, trial.selected, center_index, candidate
        );
        replace_position(trial, center_index, candidate);
        trial.objective += delta;

        // Paper Displacement first permits an infeasible move, then relocates
        // every turbine that became too close.  Exact repair ordering is
        // unpublished; the deterministic reconstruction moves the lowest
        // indexed conflicting non-anchor turbine first.
        for (int repair = 0;
             repair < static_cast<int>(trial.selected.size());
             ++repair) {
            int conflict = -1;
            for (const int position : trial.selected) {
                if (position == candidate) continue;
                if (std::hypot(
                        problem.positions()[position].x_m
                            - problem.positions()[candidate].x_m,
                        problem.positions()[position].y_m
                            - problem.positions()[candidate].y_m
                    ) < problem.minimum_spacing_m()) {
                    conflict = position;
                    break;
                }
            }
            if (conflict < 0) return trial;
            const auto repair_alternatives = nearest_candidates(
                problem,
                trial,
                conflict,
                radius,
                one_opt_candidate_cap
            );
            const int repaired = best_candidate(
                conflict, repair_alternatives, true, false
            );
            if (repaired < 0) return parent;
            const double repair_delta = one_delta(
                problem, matrix, trial.selected, conflict, repaired
            );
            replace_position(trial, conflict, repaired);
            trial.objective += repair_delta;
        }
        return parent;
    }

    for (int move = 0; move < move_cap; ++move) {
        const int removed = affected[static_cast<std::size_t>(
            random.integer(
                0,
                static_cast<int>(affected.size()),
                iteration,
                312,
                move
            )
        )];
        if (!trial.occupied[removed]) continue;
        const auto alternatives = nearest_candidates(
            problem,
            trial,
            removed,
            radius,
            one_opt_candidate_cap
        );
        if (alternatives.empty()) continue;
        int candidate = -1;
        if (mode == ShakeMode::random) {
            const int offset = random.integer(
                0,
                static_cast<int>(alternatives.size()),
                iteration,
                313,
                move
            );
            for (int step = 0;
                 step < static_cast<int>(alternatives.size());
                 ++step) {
                const int proposed = alternatives[
                    static_cast<std::size_t>(
                        (offset + step) % alternatives.size()
                    )
                ];
                if (feasible_candidate(
                        problem,
                        trial.selected,
                        removed,
                        proposed
                    )) {
                    candidate = proposed;
                    break;
                }
            }
        } else {
            candidate = best_candidate(
                removed,
                alternatives,
                true,
                mode == ShakeMode::directional
            );
        }
        if (candidate >= 0) {
            const double delta = one_delta(
                problem, matrix, trial.selected, removed, candidate
            );
            replace_position(trial, removed, candidate);
            trial.objective += delta;
        }
    }
    return trial;
}

}  // namespace

Problem::Problem(
    std::filesystem::path root,
    std::string case_id,
    const FoundationMode mode,
    const int workers
) : mode_(mode) {
    const auto preprocessing_start = Clock::now();
    if (workers < 1) throw std::invalid_argument("T31 workers");
    info_.case_id = std::move(case_id);
    if (info_.case_id.starts_with("t31_mosetti_")) {
        info_.semantic_id = "t31_mosetti_threecase_matrix_v1";
        info_.zone_quotas = {mosetti_quota(info_.case_id)};
        for (int row = 0; row < 10; ++row) {
            for (int column = 0; column < 10; ++column) {
                positions_.push_back({
                    (column + 0.5) * 200.0,
                    (row + 0.5) * 200.0,
                    0.0,
                    0.0,
                    1,
                });
            }
        }
        new_rotor_diameter_m_ = 40.0;
        fixed_rotor_diameter_m_ = 40.0;
        wake_expansion_ = 0.5 / std::log(60.0 / 0.3);
        minimum_spacing_m_ = 200.0;
        mosetti_coordinates_y_down_ = true;
        for (int speed = 0; speed <= 30; ++speed) {
            new_curve_.push_back({
                static_cast<double>(speed),
                std::min(
                    0.0003 * speed * speed * speed,
                    0.630
                ),
                0.88,
            });
        }
        fixed_curve_ = new_curve_;
        if (info_.case_id == "t31_mosetti_di") {
            wind_.push_back({0.0, 12.0, 1.0});
        } else if (info_.case_id == "t31_mosetti_dplus_i") {
            for (int direction = 0; direction < 36; ++direction) {
                wind_.push_back({
                    direction * 10.0, 12.0, 1.0 / 36.0
                });
            }
        } else {
            constexpr double tail_direction[9]{
                1.549,1.841,2.132,3.395,4.029,3.395,2.132,1.841,1.549
            };
            constexpr double tail_speed[9][3]{
                {.836,.578,.135},{.836,.870,.135},{.836,1.161,.135},
                {.836,1.128,1.431},{.836,1.762,1.431},
                {.836,1.128,1.431},{.836,1.161,.135},
                {.836,.870,.135},{.836,.578,.135}
            };
            double total = 27.0 * (.836 + .292 + .135);
            total += std::accumulate(
                std::begin(tail_direction),
                std::end(tail_direction),
                0.0
            );
            constexpr double speeds[3]{8.0,12.0,17.0};
            for (int direction = 0; direction < 36; ++direction) {
                for (int speed = 0; speed < 3; ++speed) {
                    const double weight = direction < 27
                        ? std::array<double,3>{.836,.292,.135}[speed]
                        : tail_speed[direction - 27][speed];
                    wind_.push_back({
                        direction * 10.0,
                        speeds[speed],
                        weight / total,
                    });
                }
            }
        }
    } else if (
        info_.case_id.starts_with("t31_official_")
        && info_.case_id.back() >= 'a'
        && info_.case_id.back() <= 'j'
    ) {
        const char site = static_cast<char>(
            info_.case_id.back() - 'a' + 'A'
        );
        info_.semantic_id = "t31_official_synthetic10_pair_matrix_v1";
        info_.zone_quotas = quotas_for(site);
        const auto site_root = root / "site" / std::string(1, site);
        positions_ = read_positions(site_root / "availablePositions.txt");
        fixed_ = read_positions(site_root / "fixed_wf.txt");
        for (const auto values : read_three_column(root / "wind/RVO_TNW.txt")) {
            if (values[2] > 0.0) {
                wind_.push_back({values[0], values[1], values[2]});
            }
        }
        for (const auto values : read_three_column(
                 root / "wtg/NREL-15-240.txt"
             )) {
            new_curve_.push_back({values[0], values[1], values[2]});
        }
        for (const auto values : read_three_column(
                 root / "wtg/NREL-10-179.txt"
             )) {
            fixed_curve_.push_back({values[0], values[1], values[2]});
        }
        new_rotor_diameter_m_ = new_rotor_diameter_m;
        fixed_rotor_diameter_m_ = fixed_rotor_diameter_m;
        wake_expansion_ = declared_wake_expansion;
        minimum_spacing_m_ =
            declared_spacing_diameters * new_rotor_diameter_m_;
    } else {
        throw std::invalid_argument("T31 invalid case " + info_.case_id);
    }
    info_.available_positions = static_cast<int>(positions_.size());
    info_.fixed_turbines = static_cast<int>(fixed_.size());
    info_.wind_states = static_cast<int>(wind_.size());
    if (
        info_.available_positions < 1
        || info_.wind_states < 1
        || new_curve_.empty()
        || fixed_curve_.empty()
    ) {
        throw std::runtime_error("T31 incomplete problem data");
    }
    std::vector<int> found(info_.zone_quotas.size(), 0);
    for (const auto& position : positions_) {
        if (
            position.zone < 1
            || position.zone > static_cast<int>(found.size())
        ) {
            throw std::runtime_error("T31 invalid zone");
        }
        ++found[static_cast<std::size_t>(position.zone - 1)];
    }
    for (std::size_t zone = 0; zone < found.size(); ++zone) {
        if (found[zone] < info_.zone_quotas[zone]) {
            throw std::runtime_error("T31 zone quota unavailable");
        }
    }

    fixed_deficit_squared_.assign(
        static_cast<std::size_t>(info_.available_positions)
            * wind_.size(),
        0.0F
    );
    free_aep_mwh_.assign(positions_.size(), 0.0);
    auto curve_value = [](const std::vector<CurvePoint>& curve,
                          const double speed,
                          const bool thrust) {
        if (speed < curve.front().speed_mps
            || speed > curve.back().speed_mps) {
            return 0.0;
        }
        const auto upper = std::lower_bound(
            curve.begin(),
            curve.end(),
            speed,
            [](const CurvePoint& point, const double value) {
                return point.speed_mps < value;
            }
        );
        if (upper == curve.begin()) {
            return thrust ? upper->thrust : upper->power_mw;
        }
        if (upper == curve.end()) {
            const auto& value = curve.back();
            return thrust ? value.thrust : value.power_mw;
        }
        const auto& high = *upper;
        const auto& low = *(upper - 1);
        const double fraction = (speed - low.speed_mps)
            / (high.speed_mps - low.speed_mps);
        const double low_value = thrust ? low.thrust : low.power_mw;
        const double high_value = thrust ? high.thrust : high.power_mw;
        return low_value + fraction * (high_value - low_value);
    };
    fode::PersistentExecutor executor(workers);
    executor.parallel_for(
        0, info_.available_positions, [&](const int target) {
            double aep_mw = 0.0;
            for (int state = 0; state < info_.wind_states; ++state) {
                const auto& condition = wind_[state];
                const double angle =
                    condition.direction_degrees
                    * std::numbers::pi / 180.0;
                const double flow_x = mosetti_coordinates_y_down_
                    ? std::sin(angle) : -std::sin(angle);
                const double flow_y = mosetti_coordinates_y_down_
                    ? std::cos(angle) : -std::cos(angle);
                const double cross_x = -flow_y;
                const double cross_y = flow_x;
                double squared = 0.0;
                for (const Position& source : fixed_) {
                    const double dx = positions_[target].x_m - source.x_m;
                    const double dy = positions_[target].y_m - source.y_m;
                    const double downstream = dx * flow_x + dy * flow_y;
                    if (!(downstream > 0.0)) continue;
                    const double crosswind =
                        std::abs(dx * cross_x + dy * cross_y);
                    const double source_radius =
                        0.5 * fixed_rotor_diameter_m_;
                    const double target_radius =
                        0.5 * new_rotor_diameter_m_;
                    const double wake_radius =
                        source_radius + wake_expansion_ * downstream;
                    const double overlap = circle_overlap(
                        crosswind, wake_radius, target_radius
                    ) / (std::numbers::pi
                        * target_radius * target_radius);
                    if (!(overlap > 0.0)) continue;
                    const double ct = curve_value(
                        fixed_curve_, condition.speed_mps, true
                    );
                    const double deficit =
                        (1.0 - std::sqrt(std::max(0.0, 1.0 - ct)))
                        * std::pow(source_radius / wake_radius, 2.0)
                        * overlap;
                    squared += deficit * deficit;
                }
                fixed_deficit_squared_[
                    static_cast<std::size_t>(target) * wind_.size()
                    + static_cast<std::size_t>(state)
                ] = static_cast<float>(squared);
                const double retained = std::max(
                    0.0, 1.0 - std::min(1.0, std::sqrt(squared))
                );
                aep_mw += condition.probability * curve_value(
                    new_curve_, condition.speed_mps * retained, false
                );
            }
            free_aep_mwh_[target] = hours_per_year * aep_mw;
        }
    );
    matrix_fingerprint_ = string_hash(
        "t31_pair_matrix_equations_declared_v2"
    );
    matrix_fingerprint_ = mix_hash(
        matrix_fingerprint_, string_hash(info_.case_id)
    );
    auto mix_double = [&](const double value) {
        matrix_fingerprint_ = mix_hash(
            matrix_fingerprint_, std::bit_cast<std::uint64_t>(value)
        );
    };
    mix_double(new_rotor_diameter_m_);
    mix_double(fixed_rotor_diameter_m_);
    mix_double(wake_expansion_);
    mix_double(minimum_spacing_m_);
    for (const auto& position : positions_) {
        mix_double(position.x_m);
        mix_double(position.y_m);
    }
    for (const auto& position : fixed_) {
        mix_double(position.x_m);
        mix_double(position.y_m);
    }
    for (const auto& state : wind_) {
        mix_double(state.direction_degrees);
        mix_double(state.speed_mps);
        mix_double(state.probability);
    }
    for (const auto& point : new_curve_) {
        mix_double(point.speed_mps);
        mix_double(point.power_mw);
        mix_double(point.thrust);
    }
    for (const float value : fixed_deficit_squared_) {
        matrix_fingerprint_ = mix_hash(
            matrix_fingerprint_,
            static_cast<std::uint64_t>(std::bit_cast<std::uint32_t>(value))
        );
    }
    preprocessing_seconds_ = elapsed(preprocessing_start);
}

const ProblemInfo& Problem::info() const noexcept { return info_; }
const std::vector<Position>& Problem::positions() const noexcept {
    return positions_;
}
double Problem::minimum_spacing_m() const noexcept {
    return minimum_spacing_m_;
}
FoundationMode Problem::foundation_mode() const noexcept { return mode_; }
double Problem::preprocessing_seconds() const noexcept {
    return preprocessing_seconds_;
}
std::uint64_t Problem::matrix_fingerprint() const noexcept {
    return matrix_fingerprint_;
}

double Problem::diagonal_value(const int position) const {
    double value = free_aep_mwh_[position];
    if (mode_ != FoundationMode::none) {
        value -= positions_[position].foundation_eur
            / declared_energy_price(mode_);
    }
    return value;
}

double Problem::symmetric_pair_value(
    const int first,
    const int second
) const {
    const double dx = positions_[second].x_m - positions_[first].x_m;
    const double dy = positions_[second].y_m - positions_[first].y_m;
    const double distance = std::hypot(dx, dy);
    double pair = -spacing_penalty_mwh_per_m * std::max(
        0.0, minimum_spacing_m_ - distance
    );
    auto curve_value = [](const std::vector<CurvePoint>& curve,
                          const double speed,
                          const bool thrust) {
        if (speed < curve.front().speed_mps
            || speed > curve.back().speed_mps) {
            return 0.0;
        }
        const auto upper = std::lower_bound(
            curve.begin(),
            curve.end(),
            speed,
            [](const CurvePoint& point, const double value) {
                return point.speed_mps < value;
            }
        );
        if (upper == curve.begin()) {
            return thrust ? upper->thrust : upper->power_mw;
        }
        if (upper == curve.end()) {
            const auto& value = curve.back();
            return thrust ? value.thrust : value.power_mw;
        }
        const auto& high = *upper;
        const auto& low = *(upper - 1);
        const double fraction = (speed - low.speed_mps)
            / (high.speed_mps - low.speed_mps);
        const double low_value = thrust ? low.thrust : low.power_mw;
        const double high_value = thrust ? high.thrust : high.power_mw;
        return low_value + fraction * (high_value - low_value);
    };
    const double source_radius = 0.5 * new_rotor_diameter_m_;
    const double target_radius = source_radius;
    for (int state = 0; state < info_.wind_states; ++state) {
        const auto& condition = wind_[state];
        const double angle =
            condition.direction_degrees * std::numbers::pi / 180.0;
        const double flow_x = mosetti_coordinates_y_down_
            ? std::sin(angle) : -std::sin(angle);
        const double flow_y = mosetti_coordinates_y_down_
            ? std::cos(angle) : -std::cos(angle);
        const double cross_x = -flow_y;
        const double cross_y = flow_x;
        auto target_loss = [&](const int target, const int source) {
            const double local_dx =
                positions_[target].x_m - positions_[source].x_m;
            const double local_dy =
                positions_[target].y_m - positions_[source].y_m;
            const double downstream =
                local_dx * flow_x + local_dy * flow_y;
            if (!(downstream > 0.0)) return 0.0;
            const double crosswind = std::abs(
                local_dx * cross_x + local_dy * cross_y
            );
            const double wake_radius =
                source_radius + wake_expansion_ * downstream;
            const double overlap = circle_overlap(
                crosswind, wake_radius, target_radius
            ) / (std::numbers::pi * target_radius * target_radius);
            if (!(overlap > 0.0)) return 0.0;
            const double ct = curve_value(
                new_curve_, condition.speed_mps, true
            );
            const double deficit =
                (1.0 - std::sqrt(std::max(0.0, 1.0 - ct)))
                * std::pow(source_radius / wake_radius, 2.0)
                * overlap;
            const double fixed_squared = fixed_deficit_squared_[
                static_cast<std::size_t>(target) * wind_.size()
                + static_cast<std::size_t>(state)
            ];
            const double before_retained = std::max(
                0.0,
                1.0 - std::min(1.0, std::sqrt(fixed_squared))
            );
            const double after_retained = std::max(
                0.0,
                1.0 - std::min(
                    1.0,
                    std::sqrt(fixed_squared + deficit * deficit)
                )
            );
            return hours_per_year * condition.probability * (
                curve_value(
                    new_curve_,
                    condition.speed_mps * after_retained,
                    false
                )
                - curve_value(
                    new_curve_,
                    condition.speed_mps * before_retained,
                    false
                )
            );
        };
        pair += target_loss(first, second)
            + target_loss(second, first);
    }
    return pair;
}

Evaluation Problem::evaluate_selection(
    const std::vector<int>& selected
) const {
    Evaluation result;
    std::vector<int> zone_count(info_.zone_quotas.size(), 0);
    for (std::size_t first = 0; first < selected.size(); ++first) {
        const int position = selected[first];
        if (position < 0 || position >= info_.available_positions) {
            throw std::invalid_argument("T31 selection index");
        }
        ++zone_count[static_cast<std::size_t>(
            positions_[position].zone - 1
        )];
        result.aep_mwh += free_aep_mwh_[position];
        result.foundation_cost_eur += positions_[position].foundation_eur;
        for (std::size_t second = first + 1;
             second < selected.size();
             ++second) {
            const int other = selected[second];
            const double distance = std::hypot(
                positions_[position].x_m - positions_[other].x_m,
                positions_[position].y_m - positions_[other].y_m
            );
            result.spacing_violation_m += std::max(
                0.0, minimum_spacing_m_ - distance
            );
            const double pair = symmetric_pair_value(position, other);
            result.aep_mwh += pair
                + spacing_penalty_mwh_per_m
                    * std::max(0.0, minimum_spacing_m_ - distance);
        }
    }
    if (zone_count != info_.zone_quotas) {
        result.spacing_violation_m =
            std::numeric_limits<double>::infinity();
    }
    result.objective_mwh_equivalent = result.aep_mwh;
    if (mode_ != FoundationMode::none) {
        result.objective_mwh_equivalent -=
            result.foundation_cost_eur / declared_energy_price(mode_);
    }
    result.objective_mwh_equivalent -=
        spacing_penalty_mwh_per_m * result.spacing_violation_m;
    return result;
}

RunResult run(
    const Problem& problem,
    const std::uint64_t seed,
    const RunConfig& config
) {
    if (
        config.workers < 1
        || config.time_limit_seconds <= 0.0
        || problem.foundation_mode() != config.foundation_mode
        || config.foundation_mode != FoundationMode::none
            && config.foundation_mode != FoundationMode::low_cost
            && config.foundation_mode != FoundationMode::high_cost
    ) {
        throw std::invalid_argument("T31 run configuration");
    }
    const auto total_start = Clock::now();
    const bool timed = config.fixed_iterations == 0;
    const auto deadline = total_start
        + std::chrono::duration_cast<Clock::duration>(
            std::chrono::duration<double>(config.time_limit_seconds)
        );
    fode::PersistentExecutor executor(config.workers);
    executor.reset_work_receipt();
    double matrix_seconds = 0.0;
    std::uint64_t pair_evaluations = 0;
    PackedMatrix matrix(
        problem,
        config.matrix_cache,
        executor,
        matrix_seconds,
        pair_evaluations
    );
    const auto initialization_start = Clock::now();
    SearchState best = pdsp_initial(problem, matrix, executor);
    const double initialization_seconds = elapsed(initialization_start);
    const Evaluation initial = problem.evaluate_selection(best.selected);
    if (!std::isfinite(initial.spacing_violation_m)
        || initial.spacing_violation_m > 1.0e-9) {
        throw std::runtime_error("T31 initial solution infeasible");
    }
    const fode::CounterRng random(seed ^ 0x31105588ULL);
    std::vector<double> history;
    std::vector<double> checkpoint_targets = config.checkpoint_seconds;
    std::sort(checkpoint_targets.begin(), checkpoint_targets.end());
    std::erase_if(checkpoint_targets, [](const double target) {
        return !(target > 0.0) || !std::isfinite(target);
    });
    std::vector<TimeCheckpoint> checkpoints;
    std::size_t next_checkpoint = 0;
    std::uint64_t iterations = 0;
    std::atomic<std::uint64_t> delta_candidates{0};
    double shake_seconds = 0.0;
    double local_seconds = 0.0;
    int neighborhood = 1;
    while (
        timed
            ? Clock::now() < deadline
            : iterations < config.fixed_iterations
    ) {
        const auto shake_start = Clock::now();
        SearchState trial = shake(
            problem,
            matrix,
            best,
            executor,
            random,
            iterations + 1,
            neighborhood,
            config.shake_mode
        );
        shake_seconds += elapsed(shake_start);
        const auto local_start = Clock::now();
        const double radius = problem.minimum_spacing_m()
            * std::array<double, 3>{3.0,5.0,8.0}
                [static_cast<std::size_t>(neighborhood - 1)];
        bool improved = true;
        while (
            improved
            && (!timed || Clock::now() < deadline)
        ) {
            improved = one_opt(
                problem,
                matrix,
                trial,
                executor,
                delta_candidates,
                radius,
                deadline,
                timed
            );
            if (!improved) {
                improved = two_opt(
                    problem,
                    matrix,
                    trial,
                    executor,
                    delta_candidates,
                    radius,
                    deadline,
                    timed
                );
            }
        }
        local_seconds += elapsed(local_start);
        if (trial.objective > best.objective + 1.0e-9) {
            best = std::move(trial);
            neighborhood = 1;
        } else {
            neighborhood = neighborhood % 3 + 1;
        }
        ++iterations;
        history.push_back(best.objective);
        const double optimization_elapsed = elapsed(total_start);
        while (
            next_checkpoint < checkpoint_targets.size()
            && optimization_elapsed >= checkpoint_targets[next_checkpoint]
        ) {
            checkpoints.push_back({
                .target_seconds = checkpoint_targets[next_checkpoint],
                .observed_seconds = optimization_elapsed,
                .best_objective_mwh_equivalent = best.objective,
            });
            ++next_checkpoint;
        }
    }
    const auto receipt = executor.work_receipt();
    const Evaluation final = problem.evaluate_selection(best.selected);
    if (
        !std::isfinite(final.spacing_violation_m)
        || final.spacing_violation_m > 1.0e-9
        || final.objective_mwh_equivalent
            + 1.0e-7 < initial.objective_mwh_equivalent
    ) {
        throw std::runtime_error("T31 final semantic validation");
    }
    std::uint64_t hash = 1469598103934665603ULL;
    for (const int position : best.selected) {
        hash = mix_hash(hash, static_cast<std::uint64_t>(position));
    }
    hash = mix_hash(
        hash,
        std::bit_cast<std::uint64_t>(
            final.objective_mwh_equivalent
        )
    );
    hash = mix_hash(hash, iterations);
    hash = mix_hash(
        hash,
        static_cast<std::uint64_t>(config.shake_mode)
    );
    const double optimization_seconds = elapsed(total_start);
    return {
        .case_id = problem.info().case_id,
        .problem_semantic_id = problem.info().semantic_id,
        .method_semantic_id = method_id,
        .protocol_semantic_id = protocol_id,
        .foundation_mode = config.foundation_mode,
        .shake_mode = config.shake_mode,
        .seed = seed,
        .requested_workers = config.workers,
        .observed_workers = receipt.distinct_participants,
        .completed_vns_iterations = iterations,
        .matrix_pair_evaluations = pair_evaluations,
        .wake_state_evaluations = pair_evaluations
            * static_cast<std::uint64_t>(problem.info().wind_states) * 2ULL,
        .delta_candidate_evaluations =
            delta_candidates.load(std::memory_order_relaxed),
        .initial = initial,
        .best = final,
        .best_positions = std::move(best.selected),
        .best_history_mwh = std::move(history),
        .time_checkpoints = std::move(checkpoints),
        .problem_preprocessing_seconds =
            problem.preprocessing_seconds(),
        .matrix_seconds = matrix_seconds,
        .initialization_seconds = initialization_seconds,
        .shake_seconds = shake_seconds,
        .local_search_seconds = local_seconds,
        .optimization_seconds = optimization_seconds,
        .end_to_end_seconds =
            problem.preprocessing_seconds() + optimization_seconds,
        .scientific_hash = hash,
    };
}

std::vector<std::string> paper_case_ids() {
    std::vector<std::string> result{
        "t31_mosetti_di",
        "t31_mosetti_dplus_i",
        "t31_mosetti_dplus_iplus",
    };
    for (char site = 'a'; site <= 'j'; ++site) {
        result.push_back("t31_official_" + std::string(1, site));
    }
    return result;
}

std::string foundation_mode_name(const FoundationMode mode) {
    if (mode == FoundationMode::none) return "none";
    if (mode == FoundationMode::low_cost) return "low_cost";
    if (mode == FoundationMode::high_cost) return "high_cost";
    throw std::invalid_argument("T31 foundation mode");
}

std::string shake_mode_name(const ShakeMode mode) {
    switch (mode) {
    case ShakeMode::circular: return "circular";
    case ShakeMode::conic: return "conic";
    case ShakeMode::directional: return "directional";
    case ShakeMode::displacement: return "displacement";
    case ShakeMode::random: return "random";
    case ShakeMode::random_directional: return "random_directional";
    case ShakeMode::circular_displacement:
        return "circular_displacement";
    case ShakeMode::random_conic: return "random_conic";
    case ShakeMode::directional_conic: return "directional_conic";
    case ShakeMode::all: return "all";
    }
    throw std::invalid_argument("T31 shake mode");
}

}  // namespace core99::t31
