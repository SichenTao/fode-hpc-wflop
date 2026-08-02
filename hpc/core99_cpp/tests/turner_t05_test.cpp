/*
WFLOP IMPLEMENTATION FACT DECLARATION
Implementation unit: T05 independent analytic invariants
Paper DOI: 10.1016/j.renene.2013.10.023.
Missing: author code and CPLEX model files.
Reconstruction: test paper cardinality, QIP/MILP equivalence and replay.
Claim boundary: equation/invariant test, not author numerical replay.
Last evidence-audit date: 2026-07-31
END WFLOP IMPLEMENTATION FACT DECLARATION
*/
#include "core99/turner_t05.hpp"

#include <algorithm>
#include <cmath>
#include <iostream>
#include <stdexcept>

int main() {
    try {
        if (core99::t05::paper_case_ids().size() != 6) {
            throw std::runtime_error("T05 paper case count");
        }
        core99::t05::Problem problem("t05_case_a_k26");
        core99::t05::Layout layout;
        for (int cell = 0; cell < 100; cell += 4) layout.push_back(cell);
        layout.push_back(99);
        std::sort(layout.begin(), layout.end());
        if (!problem.feasible(layout)) {
            throw std::runtime_error("T05 fixture feasibility");
        }
        const double qip = problem.qip_objective(layout);
        const double milp = problem.milp_linearized_objective(layout);
        if (!std::isfinite(qip) || std::abs(qip - milp) > 1.0e-13) {
            throw std::runtime_error("T05 QIP/MILP equivalence");
        }
        const double power = problem.expected_power_kw(layout);
        if (!(power > 0.0) || !std::isfinite(power)) {
            throw std::runtime_error("T05 power fixture");
        }
        std::cout << "t05_cpp_test_pass qip=" << qip
                  << " power_kw=" << power << "\n";
        return 0;
    } catch (const std::exception& error) {
        std::cerr << error.what() << "\n";
        return 1;
    }
}
