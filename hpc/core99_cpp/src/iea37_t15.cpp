/*
WFLOP IMPLEMENTATION FACT DECLARATION
Implementation unit: T15 pure-C++ common evaluator and comparison engine
Paper DOI: 10.2514/6.2019-0540
Public source: https://github.com/byuflowlab/iea37-wflo-casestudies
revision af88908d22795030ac2dfbe37bc38e912aee8ed6
Provided/missing/conflicting facts and Reconstruction decisions:
include/core99/iea37_t15.hpp
Method/problem semantic IDs: t15_iea37_comparison_protocol_v1;
t15_iea37_cs1_three_farms_cs2_cross_model_v1
Controlling contract: shared/contracts/core99_t15_iea37_2019.json
Independent oracle: scripts/validate_core99_t15.py
HPC design: every released layout is an independent task; deterministic
ranking is committed after a persistent all-core batch evaluation
Claim boundary: benchmark/protocol reproduction, not participant methods
Last evidence-audit date: 2026-07-31
END WFLOP IMPLEMENTATION FACT DECLARATION
*/
#include "core99/iea37_t15.hpp"

#include "core99/t15_iea37_data.hpp"

#include <algorithm>
#include <array>
#include <bit>
#include <chrono>
#include <cmath>
#include <cstddef>
#include <numbers>
#include <numeric>
#include <stdexcept>
#include <utility>

namespace core99::t15 {
namespace {

using Clock = std::chrono::steady_clock;
constexpr std::array<double, 16> kDirections = {
    0.0, 22.5, 45.0, 67.5, 90.0, 112.5, 135.0, 157.5,
    180.0, 202.5, 225.0, 247.5, 270.0, 292.5, 315.0, 337.5,
};
constexpr std::array<double, 16> kFrequencies = {
    0.025, 0.024, 0.029, 0.036, 0.063, 0.065, 0.100, 0.122,
    0.063, 0.038, 0.039, 0.083, 0.213, 0.046, 0.032, 0.022,
};
constexpr double kWindSpeed = 9.8;
constexpr double kDiameter = 130.0;
constexpr double kMinimumSpacing = 260.0;

double seconds_since(const Clock::time_point start) {
    return std::chrono::duration<double>(Clock::now() - start).count();
}

double boundary_radius(int turbines) {
    if (turbines == 16) {
        return 1300.0;
    }
    if (turbines == 36) {
        return 2000.0;
    }
    if (turbines == 64) {
        return 3000.0;
    }
    throw std::invalid_argument("T15 supports 16, 36, or 64 turbines");
}

double direction_power_w(
    const std::vector<Point>& layout,
    double direction_deg
) {
    constexpr double thrust = 8.0 / 9.0;
    constexpr double expansion = 0.0324555;
    const double angle = (270.0 - direction_deg) * std::numbers::pi / 180.0;
    std::vector<double> along(layout.size(), 0.0);
    std::vector<double> across(layout.size(), 0.0);
    for (std::size_t index = 0; index < layout.size(); ++index) {
        along[index] = std::cos(angle) * layout[index].x
            + std::sin(angle) * layout[index].y;
        across[index] = -std::sin(angle) * layout[index].x
            + std::cos(angle) * layout[index].y;
    }
    double power = 0.0;
    for (std::size_t downstream = 0; downstream < layout.size(); ++downstream) {
        double squared_loss = 0.0;
        for (std::size_t upstream = 0; upstream < layout.size(); ++upstream) {
            const double x = along[downstream] - along[upstream];
            if (x <= 0.0) {
                continue;
            }
            const double y = across[downstream] - across[upstream];
            const double sigma = expansion * x + kDiameter / std::sqrt(8.0);
            const double radical = 1.0
                - thrust / (8.0 * sigma * sigma / (kDiameter * kDiameter));
            const double loss = (1.0 - std::sqrt(radical))
                * std::exp(-0.5 * y * y / (sigma * sigma));
            squared_loss += loss * loss;
        }
        const double effective = kWindSpeed * (1.0 - std::sqrt(squared_loss));
        if (effective >= 4.0 && effective < 9.8) {
            power += 3350000.0 * std::pow((effective - 4.0) / 5.8, 3.0);
        } else if (effective >= 9.8 && effective < 25.0) {
            power += 3350000.0;
        }
    }
    return power;
}

std::uint64_t hash_rows(const std::vector<ComparisonRow>& rows) {
    std::uint64_t hash = 1469598103934665603ULL;
    auto consume = [&](double value) {
        hash ^= std::bit_cast<std::uint64_t>(value);
        hash *= 1099511628211ULL;
    };
    for (const auto& row : rows) {
        consume(row.evaluation.aep_mwh);
        consume(row.evaluation.constraint_violation_m);
        consume(static_cast<double>(row.rank));
    }
    return hash;
}

}  // namespace

std::vector<Record> paper_records() {
    std::vector<Record> result;
    result.reserve(data::kIdentifiers.size());
    for (std::size_t record = 0; record < data::kIdentifiers.size(); ++record) {
        Record value;
        value.id = std::string(data::kIdentifiers[record]);
        value.turbines = data::kTurbines[record];
        value.participant = data::kParticipants[record];
        value.archived_aep_mwh = data::kExpectedAepMWh[record];
        for (
            std::size_t point = data::kOffsets[record];
            point < data::kOffsets[record + 1];
            ++point
        ) {
            value.layout.push_back({
                data::kCoordinates[2 * point],
                data::kCoordinates[2 * point + 1],
            });
        }
        result.push_back(std::move(value));
    }
    return result;
}

Evaluation evaluate(const Record& record) {
    if (static_cast<int>(record.layout.size()) != record.turbines) {
        throw std::invalid_argument("T15 record turbine count mismatch");
    }
    double aep = 0.0;
    for (std::size_t direction = 0; direction < kDirections.size(); ++direction) {
        aep += 8760.0 * kFrequencies[direction]
            * direction_power_w(record.layout, kDirections[direction]) / 1.0e6;
    }
    double violation = 0.0;
    const double radius = boundary_radius(record.turbines);
    for (const Point& point : record.layout) {
        violation += std::max(0.0, std::hypot(point.x, point.y) - radius);
    }
    for (std::size_t first = 0; first < record.layout.size(); ++first) {
        for (std::size_t second = first + 1; second < record.layout.size(); ++second) {
            violation += std::max(
                0.0,
                kMinimumSpacing - std::hypot(
                    record.layout[first].x - record.layout[second].x,
                    record.layout[first].y - record.layout[second].y
                )
            );
        }
    }
    return {aep, violation};
}

ComparisonResult run_comparison(int workers) {
    const Clock::time_point total_start = Clock::now();
    fode::PersistentExecutor executor(workers);
    executor.reset_work_receipt();
    const std::vector<Record> records = paper_records();
    std::vector<Evaluation> evaluations(records.size());
    const Clock::time_point evaluator_start = Clock::now();
    executor.parallel_for(0, static_cast<int>(records.size()), [&](int raw) {
        evaluations[static_cast<std::size_t>(raw)] = evaluate(
            records[static_cast<std::size_t>(raw)]
        );
    });
    ComparisonResult result;
    result.evaluator_seconds = seconds_since(evaluator_start);
    result.rows.reserve(records.size());
    for (std::size_t index = 0; index < records.size(); ++index) {
        result.rows.push_back({
            records[index].id,
            records[index].turbines,
            records[index].participant,
            0,
            records[index].archived_aep_mwh,
            evaluations[index],
        });
    }
    for (const int turbines : {16, 36, 64}) {
        std::vector<std::size_t> order;
        for (std::size_t index = 0; index < result.rows.size(); ++index) {
            if (result.rows[index].turbines == turbines) {
                order.push_back(index);
            }
        }
        std::sort(order.begin(), order.end(), [&](std::size_t left, std::size_t right) {
            return result.rows[left].evaluation.aep_mwh
                > result.rows[right].evaluation.aep_mwh;
        });
        for (std::size_t rank = 0; rank < order.size(); ++rank) {
            result.rows[order[rank]].rank = static_cast<int>(rank + 1);
        }
    }
    const fode::ExecutorWorkReceipt receipt = executor.work_receipt();
    result.requested_workers = workers;
    result.observed_workers = receipt.distinct_participants;
    result.end_to_end_seconds = seconds_since(total_start);
    result.scientific_hash = hash_rows(result.rows);
    return result;
}

}  // namespace core99::t15
