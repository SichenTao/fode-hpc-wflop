/*
WFLOP IMPLEMENTATION FACT DECLARATION
Implementation unit: T07 command-line driver
Paper DOI: 10.1016/j.apenergy.2015.03.139.
Public source: no author code/data found.
Missing: author CVX files, CFD arrays and unreported solver fields.
Reconstruction: invoke explicit trust SCP with pinned open QP solver.
Claim boundary: declared paper-equation reconstruction, not author replay.
Last evidence-audit date: 2026-07-31
END WFLOP IMPLEMENTATION FACT DECLARATION
*/
#include "core99/park_t07.hpp"

#include <fstream>
#include <cmath>
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

void evaluation(std::ostream& out, const core99::t07::Evaluation& value) {
    out << "{\"efficiency\":" << std::setprecision(17)
        << value.efficiency
        << ",\"requested_workers\":" << value.requested_workers
        << ",\"observed_workers\":" << value.observed_workers
        << ",\"seconds\":" << value.seconds << "}";
}

void number_or_null(std::ostream& out, const double value) {
    if (std::isfinite(value)) out << value;
    else out << "null";
}

void result(std::ostream& out, const core99::t07::RunResult& run) {
    out << "{\"case_id\":\"" << escape(run.case_id)
        << "\",\"problem_semantic_id\":\""
        << escape(run.problem_semantic_id)
        << "\",\"method_semantic_id\":\""
        << escape(run.method_semantic_id)
        << "\",\"requested_workers\":" << run.requested_workers
        << ",\"observed_workers\":" << run.observed_workers
        << std::setprecision(17)
        << ",\"wake_expansion\":" << run.wake_expansion
        << ",\"published_initial_efficiency\":"
        << run.published_initial_efficiency
        << ",\"published_optimized_efficiency\":"
        << run.published_optimized_efficiency
        << ",\"initial\":";
    evaluation(out, run.initial);
    out << ",\"final\":";
    evaluation(out, run.final);
    out << ",\"final_layout\":[";
    for (std::size_t i = 0; i < run.final_layout.size(); ++i) {
        if (i) out << ",";
        out << "[" << run.final_layout[i].x_m << ","
            << run.final_layout[i].y_m << "]";
    }
    out << "],\"stages\":[";
    for (std::size_t i = 0; i < run.stages.size(); ++i) {
        if (i) out << ",";
        const auto& stage = run.stages[i];
        out << "{\"iteration\":" << stage.iteration
            << ",\"accepted\":" << (stage.accepted ? "true" : "false")
            << ",\"qp_status\":" << stage.qp_status
            << ",\"qp_evaluations\":" << stage.qp_evaluations
            << ",\"trust_radius_m\":" << stage.trust_radius_m
            << ",\"initial_efficiency\":" << stage.initial_efficiency
            << ",\"proposed_efficiency\":" << stage.proposed_efficiency
            << ",\"actual_predicted_ratio\":";
        number_or_null(out, stage.actual_predicted_ratio);
        out
            << ",\"step_norm_m\":" << stage.step_norm_m
            << ",\"maximum_constraint_violation_m\":"
            << stage.maximum_constraint_violation_m
            << ",\"seconds\":" << stage.seconds << "}";
    }
    out << "],\"maximum_constraint_violation_m\":"
        << run.maximum_constraint_violation_m
        << ",\"evaluator_seconds\":" << run.evaluator_seconds
        << ",\"qp_seconds\":" << run.qp_seconds
        << ",\"end_to_end_seconds\":" << run.end_to_end_seconds
        << ",\"scientific_hash\":\"" << std::hex
        << run.scientific_hash << std::dec << "\"}";
}

}  // namespace

int main(int argc, char** argv) {
    try {
        std::string case_id = "t07_expected_k033";
        std::string output;
        core99::t07::RunConfig config;
        for (int i = 1; i < argc; ++i) {
            const std::string arg = argv[i];
            auto next = [&]() -> std::string {
                if (++i >= argc) throw std::invalid_argument("missing value");
                return argv[i];
            };
            if (arg == "--case") case_id = next();
            else if (arg == "--workers") config.workers = std::stoi(next());
            else if (arg == "--scp-iterations") {
                config.maximum_scp_iterations = std::stoi(next());
            } else if (arg == "--qp-evaluations") {
                config.maximum_qp_evaluations = std::stoi(next());
            } else if (arg == "--epsilon-m") {
                config.epsilon_m = std::stod(next());
            } else if (arg == "--output") output = next();
            else if (arg == "--mode") (void)next();
            else if (arg == "--seed") (void)next();
            else throw std::invalid_argument("unknown argument " + arg);
        }
        const core99::t07::Problem problem(case_id);
        const auto run = core99::t07::run(problem, config);
        std::ostringstream payload;
        payload << "{\"schema_version\":1,\"corpus_id\":\"T07\","
                << "\"runs\":[";
        result(payload, run);
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
        std::cerr << "core99_t07_hpc: " << error.what() << "\n";
        return 2;
    }
}
