/*
WFLOP IMPLEMENTATION FACT DECLARATION
Implementation unit: canonical FODE-E0-L case contract interface
Paper title and DOI: A State-of-the-Art Fractional Order-Driven Differential
Evolution for Wind Farm Layout Optimization; 10.3390/math13020282
Paper/source basis: Section 2; archived MATLAB benchmark and wind arrays
Public asset: not publicly redistributed; hashes are in
shared/contracts/paper_implementation_ledger.tsv
Missing/conflicts: legacy physical conventions are frozen in the benchmark
contract and are not upgraded to engineering-realistic claims
Reconstruction: none; literal frozen case identity
Method/problem semantic IDs: not_applicable_shared_infrastructure;
fode_wflop_e0_legacy_v1
Controlling contract and claim boundary: shared/contracts/benchmark_contract.json;
FODE-E0-L synthetic benchmark only
Last evidence-audit date: 2026-07-30
END WFLOP IMPLEMENTATION FACT DECLARATION
*/
#pragma once

#include <string>
#include <vector>

namespace fode {

struct CaseData {
    std::string case_id;
    int rows = 0;
    int cols = 0;
    int turbine_count = 0;
    double cell_width = 0.0;
    std::vector<double> theta;
    std::vector<double> velocity;
    std::vector<double> probability;
    std::vector<int> unavailable_cells_1based;
};

std::vector<CaseData> load_cases(const std::string& path);
CaseData load_case(const std::string& path, const std::string& case_id);

}  // namespace fode
