/*
WFLOP IMPLEMENTATION FACT DECLARATION
Implementation unit: BDE WS5/WS6 declared-proxy pure-CPU CLI
Paper title: Discrete Bi-Population Differential Evolution for Optimizing
Complex Wind Farm Layouts in Diverse Terrains
DOI: 10.1016/j.energy.2025.137885
Paper provides: problem cardinalities, metric spacing, L=50, FES=10000,
Imax=400, and BDE equations.
Public author code URL: https://raw.githubusercontent.com/toyamaailab/toyamaailab.github.io/main/resource/BDE-WindFarm_code.zip
Public author code revision or archive hash: sha256:f4a317d4d727a9d452f76376373e2c8ad5546e35ff19530eda0ba328682dd140
Public code/assets provide: WS1-WS4 arrays, mask cross-check, evaluator source,
and BDE transition source.
Known missing information: exact WS5/WS6 arrays and conflict adjudications.
Reconstruction performed here: stable selected-case CLI for the distinct P3
composite problems and paper-Imax400 method.
Method evidence tier: M2_CITATION_PREDECESSOR subtype
paper_equation_direct_source_resolved.
Problem evidence tier: P3_DECLARED_PROXY subtype composite_proxy.
Method semantic ID: bde_paper_equations_imax400_exact_fes_v1
Problem semantic ID: bde2025_ws5_paper250_declared_proxy_v1 or
bde2025_ws6_paper250_declared_proxy_v1, selected by case prefix.
Controlling contracts: shared/contracts/bde_ws56_declared_proxy_contract.json
and shared/contracts/bde_ws56_declared_proxy_cases.json
Claim boundary: development-only P3 composite execution, never original
WS5/WS6 reproduction and never pooled with WS1-WS4.
Last evidence audit date: 2026-07-29
Execution profile ID: bde2025_ws56_p3_paper_schedule_cpu_v1
Default execution: one selected case, exact physical FES, all hardware threads
visible to the job; the CLI cannot load the WS1--WS4 replay manifest.
END WFLOP IMPLEMENTATION FACT DECLARATION
*/
#include "bde_ws56/evolution.hpp"

#include "fode/case.hpp"

#include <algorithm>
#include <iomanip>
#include <iostream>
#include <sstream>
#include <stdexcept>
#include <string>
#include <vector>

namespace {

std::string value(int& index, int argc, char** argv) {
    if (index + 1 >= argc) {
        throw std::runtime_error(
            "missing value for " + std::string(argv[index])
        );
    }
    return argv[++index];
}

std::vector<int> parse_layout(const std::string& text) {
    std::vector<int> result;
    std::size_t begin = 0;
    while (begin <= text.size()) {
        const std::size_t comma = text.find(',', begin);
        const std::size_t end =
            comma == std::string::npos ? text.size() : comma;
        result.push_back(std::stoi(text.substr(begin, end - begin)));
        if (comma == std::string::npos) {
            break;
        }
        begin = comma + 1;
    }
    return result;
}

}  // namespace

int main(int argc, char** argv) {
    try {
        std::string cases_path;
        std::string case_id;
        std::string layout_text;
        bde_ws56::Config config;
        for (int index = 1; index < argc; ++index) {
            const std::string option = argv[index];
            if (option == "--cases") {
                cases_path = value(index, argc, argv);
            } else if (option == "--case") {
                case_id = value(index, argc, argv);
            } else if (option == "--seed") {
                config.seed = std::stoull(value(index, argc, argv));
            } else if (option == "--physical-fes") {
                config.physical_fes =
                    std::stoull(value(index, argc, argv));
            } else if (option == "--workers") {
                config.workers = std::stoi(value(index, argc, argv));
            } else if (option == "--execution-mode") {
                config.execution_mode = value(index, argc, argv);
            } else if (option == "--backend") {
                config.execution_mode = value(index, argc, argv);
            } else if (option == "--evaluate-layout") {
                layout_text = value(index, argc, argv);
            } else if (option == "--help" || option == "-h") {
                std::cout
                    << "usage: bde_ws56_hpc --cases FILE --case ID"
                    << " [--seed N] [--physical-fes N] [--workers N]"
                    << " [--execution-mode cpu|hybrid|gpu|auto]"
                    << " [--evaluate-layout a,b,...]\n";
                return 0;
            } else {
                throw std::runtime_error("unknown option: " + option);
            }
        }
        if (cases_path.empty() || case_id.empty()) {
            throw std::runtime_error("--cases and --case are required");
        }
        const fode::CaseData data = fode::load_case(cases_path, case_id);
        if (!layout_text.empty()) {
            const std::vector<int> layout = parse_layout(layout_text);
            const double objective =
                bde_ws56::evaluate_layout(data, layout, config.workers);
            const double no_wake =
                bde_ws56::no_wake_expected_power_kw(data);
            std::cout
                << "{\"case_id\":\"" << data.case_id
                << "\",\"problem_semantic_id\":\""
                << bde_ws56::problem_semantic_id(data)
                << "\",\"objective_semantics_hash\":\""
                << bde_ws56::objective_semantics_hash(data)
                << "\",\"feasible_set_hash\":\""
                << bde_ws56::feasible_set_hash(data)
                << "\",\"expected_power_kw\":" << std::setprecision(17)
                << objective
                << ",\"no_wake_expected_power_kw\":" << no_wake
                << ",\"conversion_efficiency_percent\":"
                << 100.0 * objective / no_wake << "}\n";
            return 0;
        }
        std::cout << bde_ws56::result_to_json(
            bde_ws56::run(config, data)
        ) << '\n';
        return 0;
    } catch (const std::exception& error) {
        std::cerr << "bde_ws56_hpc: " << error.what() << '\n';
        return 1;
    }
}
