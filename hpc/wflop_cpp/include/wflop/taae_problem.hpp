/*
WFLOP IMPLEMENTATION FACT DECLARATION
Implementation unit: TAAE structured declared-proxy problem interface
Paper title: Transformer Autoencoder-Assisted Evolutionary Framework for Constrained Multiobjective 3D Wind Farm Layout Optimization
DOI: 10.1109/JAS.2026.126233
Paper SHA-256: 243dd96dfa94a3d596f375a6c62e58015c735171958778d816b6afdbf99cd35b
Public author problem code/data URL: unavailable as recorded in docs/source-dossiers/Y36.json
Reconstruction performed here: typed interface for the separately identified P3 structured problem proxy and P4 formula fixture
Method evidence tier: not_applicable_problem_interface
Problem evidence tier: P3_DECLARED_PROXY
Method semantic ID: not_applicable_problem_interface
Problem semantic ID: taae_zhangbei_structured_declared_proxy_v1
Controlling contract: shared/contracts/taae_zhangbei_structured_declared_proxy_contract.json
Claim boundary: no TAAE Transformer method, Zhangbei numerical-case, reported-front, or speedup claim
Last evidence audit date: 2026-07-29
END WFLOP IMPLEMENTATION FACT DECLARATION
*/
#pragma once

#include "fode/case.hpp"
#include "fode/executor.hpp"

#include <cstdint>
#include <string>
#include <vector>

namespace wflop::taae {

struct CompleteEvaluation {
    double reciprocal_expected_power_per_kw = 0.0;
    double expected_power_kw = 0.0;
    double average_a_weighted_noise_dba = 0.0;
    double total_cost_units = 0.0;
    double normalized_constraint_violation = 0.0;
};

struct BatchEvaluation {
    std::vector<CompleteEvaluation> values;
    std::uint64_t complete_layout_evaluations = 0;
    int configured_workers = 0;
    double elapsed_seconds = 0.0;
    std::string problem_semantic_hash;
};

BatchEvaluation evaluate_structured_proxy(
    const std::vector<int>& layouts_1based,
    int batch_size,
    const fode::CaseData& data,
    fode::PersistentExecutor& executor
);

std::string structured_proxy_semantic_hash(const fode::CaseData& data);
void check_formula_fixture();

}  // namespace wflop::taae
