/*
WFLOP IMPLEMENTATION FACT DECLARATION
Implementation unit: T-MOEA Eq. (16) complement-set regression fixture
Paper title: A Topology-Driven Multi-Objective Evolutionary Algorithm for
Offshore Wind Farm Layout Optimization
DOI: 10.1109/CPEEE69412.2026.11521465
Paper provides: Eq. (16) defines the relocation set as candidates absent from
the complete pre-mutation layout.
Public author code URL: no public T-MOEA implementation was found.
Public author code revision or archive hash: not available.
Public code/assets provide: no T-MOEA transition fixture.
Known missing information: original candidate set and implementation.
Reconstruction performed here: a synthetic direct fixture distinguishes the
historical released-site behavior from the paper-corrected complement set.
Method evidence tier: M3_DECLARED_COMPLETION.
Problem evidence tier: fixture only; no experimental problem claim.
Method semantic ID: tmoea_nysted_gga_asset_reconstruction_paper_eq16_v2
Problem semantic ID: synthetic_eq16_complement_fixture_v1
Controlling contracts:
shared/contracts/tmoea_nysted_paper_eq16_r4_execution_contract.json
Claim boundary: operator-semantic regression only.
Last evidence audit date: 2026-07-29
END WFLOP IMPLEMENTATION FACT DECLARATION
*/
#define main gga_cpp_hpc_embedded_main
#include "../src/main.cpp"
#undef main

#include <iostream>

int main() {
    Problem problem;
    problem.candidates = {
        {0.0, 0.0},
        {10.0, 0.0},
        {1.0, 0.0}
    };
    const std::vector<int> pre_mutation_layout{0, 1};
    const Point centroid{0.0, 0.0};

    const int historical = tmoea_replacement_candidate(
        pre_mutation_layout,
        0,
        centroid,
        problem,
        false
    );
    if (historical != 0) {
        std::cerr << "historical v1 fixture no longer exposes old-site reuse\n";
        return 1;
    }

    const int corrected = tmoea_replacement_candidate(
        pre_mutation_layout,
        0,
        centroid,
        problem,
        true
    );
    if (corrected != 2) {
        std::cerr << "Eq. (16) complement admitted an occupied old site\n";
        return 1;
    }
    if (std::find(
            pre_mutation_layout.begin(),
            pre_mutation_layout.end(),
            corrected
        ) != pre_mutation_layout.end()) {
        std::cerr << "Eq. (16) replacement belongs to pre-mutation layout\n";
        return 1;
    }

    std::cout
        << "tmoea_topology_eq16_fixture_pass "
        << "historical=0 corrected=2\n";
    return 0;
}
