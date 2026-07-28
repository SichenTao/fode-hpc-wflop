#pragma once

#include "fode/case.hpp"
#include "fode/executor.hpp"

#include <cstdint>
#include <vector>

namespace fode {

enum class EvaluationDetail {
    TotalOnly,
    TotalAndPerTurbine,
};

// FullStageParallel preserves the shared platform's default behavior.
// GranularityAware keeps the same equations and fixed reduction order, while
// running short stages on the caller so small FODE batches do not wake the
// complete worker team for less work than the dispatch itself.
enum class EvaluationSchedule {
    FullStageParallel,
    GranularityAware,
};

struct Evaluation {
    std::vector<double> fitness;
    std::vector<double> accumulated_turbine_power_kw;
    std::vector<int> turbine_position_order_1based;
    double elapsed_seconds = 0.0;
    int requested_workers = 0;
    int observed_workers = 0;
};

// Each row is a feasible, sorted, one-based vector of grid-cell indices.
// The routine evaluates the complete case-defined wind distribution.
Evaluation evaluate_population_hpc(
    const std::vector<double>& indices,
    int batch_size,
    const CaseData& data,
    int workers,
    EvaluationDetail detail = EvaluationDetail::TotalOnly,
    EvaluationSchedule schedule = EvaluationSchedule::FullStageParallel
);

Evaluation evaluate_population_hpc(
    const std::vector<double>& indices,
    int batch_size,
    const CaseData& data,
    PersistentExecutor& executor,
    EvaluationDetail detail = EvaluationDetail::TotalOnly,
    EvaluationSchedule schedule = EvaluationSchedule::FullStageParallel
);

}  // namespace fode
