/*
WFLOP IMPLEMENTATION FACT DECLARATION
Implementation unit: canonical FODE-E0-L evaluator interface
Paper title and DOI: A State-of-the-Art Fractional Order-Driven Differential
Evolution for Wind Farm Layout Optimization; 10.3390/math13020282
Paper/source basis: paper Eqs. 2-7 and archived MATLAB wf_fitness_m0
Public asset: not publicly redistributed; source hashes are in the paper ledger
Missing/conflicts: power-curve and scenario conventions are synthetic legacy
choices preserved under a distinct problem identity
Reconstruction: C++ transcription with fixed-order reductions
Method/problem semantic IDs: not_applicable_shared_infrastructure;
fode_wflop_e0_legacy_v1
Controlling contract and claim boundary: shared/contracts/benchmark_contract.json;
no corrected-physics or AEP claim
Last evidence-audit date: 2026-07-30
END WFLOP IMPLEMENTATION FACT DECLARATION
*/
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
