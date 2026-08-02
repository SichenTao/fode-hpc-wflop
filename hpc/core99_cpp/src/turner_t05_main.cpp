/*
WFLOP IMPLEMENTATION FACT DECLARATION
Implementation unit: T05 command-line driver
Paper DOI: 10.1016/j.renene.2013.10.023.
Paper/source/missing/reconstruction/semantic IDs:
hpc/core99_cpp/include/core99/turner_t05.hpp.
Public source: no author code found.
Missing: author CPLEX model and numeric Figure-5 array.
Reconstruction: invoke the versioned pure-C++ open-solver profile.
Claim boundary: academic declared reconstruction, not CPLEX replay.
Last evidence-audit date: 2026-07-31
END WFLOP IMPLEMENTATION FACT DECLARATION
*/
#include "core99/turner_t05.hpp"

#include <fstream>
#include <iomanip>
#include <iostream>
#include <sstream>
#include <stdexcept>
#include <string>

namespace {

std::string escape(const std::string& value) {
    std::ostringstream out;
    for (const char c : value) {
        if (c == '"' || c == '\\') out << '\\';
        out << c;
    }
    return out.str();
}

void write_result(std::ostream& out, const core99::t05::RunResult& run) {
    out << "{"
        << "\"case_id\":\"" << escape(run.case_id) << "\","
        << "\"problem_semantic_id\":\""
        << escape(run.problem_semantic_id) << "\","
        << "\"method_semantic_id\":\""
        << escape(run.method_semantic_id) << "\","
        << "\"formulation\":\"" << escape(run.formulation) << "\","
        << "\"seed\":" << run.seed << ","
        << "\"requested_workers\":" << run.requested_workers << ","
        << "\"observed_workers\":" << run.observed_workers << ","
        << "\"turbine_count\":" << run.turbine_count << ","
        << "\"multistarts\":" << run.multistarts << ","
        << "\"node_limit\":" << run.node_limit << ","
        << "\"explored_nodes\":" << run.explored_nodes << ","
        << "\"local_candidate_evaluations\":"
        << run.local_candidate_evaluations << ","
        << std::setprecision(17)
        << "\"qip_objective\":" << run.qip_objective << ","
        << "\"milp_linearized_objective\":"
        << run.milp_linearized_objective << ","
        << "\"admissible_lower_bound\":"
        << run.admissible_lower_bound << ","
        << "\"relative_gap\":" << run.relative_gap << ","
        << "\"expected_power_kw\":" << run.expected_power_kw << ","
        << "\"published_power_kw\":" << run.published_power_kw << ","
        << "\"exact_certificate\":"
        << (run.exact_certificate ? "true" : "false") << ","
        << "\"best_layout\":[";
    for (std::size_t i = 0; i < run.best_layout.size(); ++i) {
        if (i) out << ",";
        out << run.best_layout[i];
    }
    out << "],"
        << "\"interaction_assembly_seconds\":"
        << run.interaction_assembly_seconds << ","
        << "\"incumbent_search_seconds\":"
        << run.incumbent_search_seconds << ","
        << "\"branch_and_bound_seconds\":"
        << run.branch_and_bound_seconds << ","
        << "\"local_search_seconds\":" << run.local_search_seconds << ","
        << "\"power_evaluation_seconds\":"
        << run.power_evaluation_seconds << ","
        << "\"end_to_end_seconds\":" << run.end_to_end_seconds << ","
        << "\"scientific_hash\":\"" << std::hex
        << run.scientific_hash << std::dec << "\""
        << "}";
}

}  // namespace

int main(int argc, char** argv) {
    try {
        std::string case_id = "t05_case_b_k39";
        std::string output;
        core99::t05::RunConfig config;
        for (int i = 1; i < argc; ++i) {
            const std::string arg = argv[i];
            auto value = [&]() -> std::string {
                if (++i >= argc) throw std::invalid_argument("missing value");
                return argv[i];
            };
            if (arg == "--case") case_id = value();
            else if (arg == "--workers") config.workers = std::stoi(value());
            else if (arg == "--seed") config.seed = std::stoull(value());
            else if (arg == "--multistarts") {
                config.multistarts = std::stoi(value());
            } else if (arg == "--node-limit") {
                config.node_limit = std::stoull(value());
            } else if (arg == "--output") output = value();
            else if (arg == "--mode") (void)value();
            else throw std::invalid_argument("unknown argument " + arg);
        }
        const core99::t05::Problem problem(case_id);
        const auto run = problem.optimize(config);
        std::ostringstream payload;
        payload << "{\"schema_version\":1,\"corpus_id\":\"T05\","
                << "\"runs\":[";
        write_result(payload, run);
        payload << "]}";
        if (output.empty()) {
            std::cout << payload.str() << "\n";
        } else {
            std::ofstream stream(output);
            if (!stream) throw std::runtime_error("cannot open output");
            stream << payload.str() << "\n";
        }
        return 0;
    } catch (const std::exception& error) {
        std::cerr << "core99_t05_hpc: " << error.what() << "\n";
        return 2;
    }
}
