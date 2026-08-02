/*
WFLOP IMPLEMENTATION FACT DECLARATION
Implementation unit: T15 pure-C++ HPC comparison receipt CLI
Paper DOI: 10.2514/6.2019-0540
Public source: https://github.com/byuflowlab/iea37-wflo-casestudies
revision af88908d22795030ac2dfbe37bc38e912aee8ed6
Missing/conflicts and reconstruction: include/core99/iea37_t15.hpp
Method/problem semantic IDs: t15_iea37_comparison_protocol_v1;
t15_iea37_cs1_three_farms_cs2_cross_model_v1
Controlling contract: shared/contracts/core99_t15_iea37_2019.json
Claim boundary: full paper comparison-protocol receipt, not participant methods
Last evidence-audit date: 2026-07-31
END WFLOP IMPLEMENTATION FACT DECLARATION
*/
#include "core99/iea37_t15.hpp"

#include <cmath>
#include <cstdlib>
#include <fstream>
#include <iomanip>
#include <iostream>
#include <sstream>
#include <stdexcept>
#include <string>

namespace {

std::string escape(const std::string& text) {
    std::string result;
    for (const char value : text) {
        if (value == '"' || value == '\\') {
            result.push_back('\\');
        }
        result.push_back(value);
    }
    return result;
}

std::string json(const core99::t15::ComparisonResult& result) {
    std::ostringstream output;
    output << std::setprecision(17)
        << "{\n"
        << "  \"mode\": \"paper_native_fixed_layout_comparison\",\n"
        << "  \"method_semantic_id\": "
           "\"t15_iea37_comparison_protocol_v1\",\n"
        << "  \"problem_semantic_id\": "
           "\"t15_iea37_cs1_three_farms_cs2_cross_model_v1\",\n"
        << "  \"rows\": [\n";
    for (std::size_t index = 0; index < result.rows.size(); ++index) {
        const auto& row = result.rows[index];
        output << "    {\"id\":\"" << escape(row.id)
            << "\",\"turbines\":" << row.turbines
            << ",\"participant\":" << row.participant
            << ",\"rank\":" << row.rank
            << ",\"archived_aep_mwh\":" << row.archived_aep_mwh
            << ",\"recalculated_aep_mwh\":" << row.evaluation.aep_mwh
            << ",\"absolute_error_mwh\":"
            << std::abs(row.evaluation.aep_mwh - row.archived_aep_mwh)
            << ",\"constraint_violation_m\":"
            << row.evaluation.constraint_violation_m << "}";
        if (index + 1 != result.rows.size()) {
            output << ',';
        }
        output << '\n';
    }
    output << "  ],\n"
        << "  \"requested_workers\": " << result.requested_workers << ",\n"
        << "  \"observed_workers\": " << result.observed_workers << ",\n"
        << "  \"evaluator_seconds\": " << result.evaluator_seconds << ",\n"
        << "  \"end_to_end_seconds\": " << result.end_to_end_seconds << ",\n"
        << "  \"scientific_hash\": \"" << std::hex
        << result.scientific_hash << std::dec << "\"\n"
        << "}\n";
    return output.str();
}

}  // namespace

int main(int argc, char** argv) {
    try {
        int workers = 20;
        std::string output_path;
        for (int index = 1; index < argc; ++index) {
            const std::string flag = argv[index];
            if (++index >= argc) {
                throw std::invalid_argument("missing value for " + flag);
            }
            if (flag == "--workers") {
                workers = std::stoi(argv[index]);
            } else if (flag == "--output") {
                output_path = argv[index];
            } else {
                throw std::invalid_argument("unknown T15 flag: " + flag);
            }
        }
        const std::string content = json(core99::t15::run_comparison(workers));
        if (output_path.empty()) {
            std::cout << content;
        } else {
            std::ofstream stream(output_path);
            if (!stream) {
                throw std::runtime_error("cannot open T15 output");
            }
            stream << content;
        }
        return EXIT_SUCCESS;
    } catch (const std::exception& error) {
        std::cerr << "T15 error: " << error.what() << '\n';
        return EXIT_FAILURE;
    }
}
