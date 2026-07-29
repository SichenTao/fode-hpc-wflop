/*
WFLOP IMPLEMENTATION FACT DECLARATION
Implementation unit: shared parameterized scalar/discrete WFLOP case interface
Paper title and DOI: thirteen scalar WFLOP packages; see
docs/scalar_problem_package_registry.tsv
Paper/source basis: paper-native case contracts and hashed source arrays
Public asset: per-paper source and fidelity are recorded in the scalar registry
Missing/conflicts: P3 reconstructions remain distinct semantic identities
Reconstruction: optional per-case physical constants preserve paper models
Method/problem semantic IDs: not_applicable_shared_infrastructure;
registry_defined
Controlling contract and claim boundary:
docs/scalar_problem_package_registry.tsv; loading does not upgrade fidelity
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
    double rotor_diameter = 77.0;
    double hub_height = 80.0;
    double surface_roughness = 0.00025;
    double wake_deficit_coefficient = 2.0 / 3.0;
    double power_curve_cubic_coefficient = 0.3;
    double power_curve_rated_kw = 629.1;
    double power_curve_cutin_mps = 2.0;
    double power_curve_rated_mps = 12.8;
    double power_curve_cutout_mps = 18.0;
    std::vector<double> theta;
    std::vector<double> velocity;
    std::vector<double> probability;
    std::vector<int> unavailable_cells_1based;
};

std::vector<CaseData> load_cases(const std::string& path);
CaseData load_case(const std::string& path, const std::string& case_id);

}  // namespace fode
