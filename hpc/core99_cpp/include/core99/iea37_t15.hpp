/*
WFLOP IMPLEMENTATION FACT DECLARATION
Implementation unit: T15 IEA37 case-study comparison protocol
Paper/DOI: Best Practices for Wake Model and Optimization Algorithm Selection
in Wind Farm Layout Optimization; 10.2514/6.2019-0540
Public source: https://github.com/byuflowlab/iea37-wflo-casestudies
tag v2.0.1, revision af88908d22795030ac2dfbe37bc38e912aee8ed6
Provided assets: case-1 Python evaluator, turbine/wind definitions, example
layouts, submitted layouts and AEPs; case-2 submitted layouts and
participant-reported cross-comparison data
Missing/conflicts: participant optimizer implementations are not released;
case study 2 intentionally has no common wake model; the current repository
adds participants 11--12 that are absent from the paper tables
Reconstruction and resolution: faithfully reproduce the paper's common
case-1 evaluator and full participant-1--10 comparison matrix; register case 2
as a cross-model protocol/data asset without fabricating its five missing
participant wake models; later participants 11--12 are excluded from the
paper-native result
Method/problem semantic IDs: t15_iea37_comparison_protocol_v1;
t15_iea37_cs1_three_farms_cs2_cross_model_v1
Controlling contract: shared/contracts/core99_t15_iea37_2019.json
Production backend: pure C++ CPU-HPC with persistent layout-batch execution
Claim boundary: academic reproduction of the paper benchmark/comparison
contribution, not participant optimizer or wake-model implementations
Last evidence-audit date: 2026-07-31
END WFLOP IMPLEMENTATION FACT DECLARATION
*/
#pragma once

#include "fode/executor.hpp"

#include <cstdint>
#include <string>
#include <vector>

namespace core99::t15 {

struct Point {
    double x = 0.0;
    double y = 0.0;
};

struct Record {
    std::string id;
    int turbines = 0;
    int participant = 0;
    std::vector<Point> layout;
    double archived_aep_mwh = 0.0;
};

struct Evaluation {
    double aep_mwh = 0.0;
    double constraint_violation_m = 0.0;
};

struct ComparisonRow {
    std::string id;
    int turbines = 0;
    int participant = 0;
    int rank = 0;
    double archived_aep_mwh = 0.0;
    Evaluation evaluation;
};

struct ComparisonResult {
    std::vector<ComparisonRow> rows;
    int requested_workers = 0;
    int observed_workers = 0;
    double evaluator_seconds = 0.0;
    double end_to_end_seconds = 0.0;
    std::uint64_t scientific_hash = 0;
};

[[nodiscard]] std::vector<Record> paper_records();
[[nodiscard]] Evaluation evaluate(const Record& record);
[[nodiscard]] ComparisonResult run_comparison(int workers);

}  // namespace core99::t15
