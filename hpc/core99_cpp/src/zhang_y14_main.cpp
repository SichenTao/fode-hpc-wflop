/*
WFLOP IMPLEMENTATION FACT DECLARATION
Implementation unit: Y14 pure-C++ CPU-HPC command line
Paper DOI: 10.1109/TSTE.2026.3661110
Public asset, missing information, conflict, reconstruction, semantic IDs,
production backend, controlling contract and claim boundary:
include/core99/zhang_y14.hpp
Claim boundary: flexible academic reconstruction, not author code or private
Gansu numeric replay.
Last evidence-audit date: 2026-08-01
END WFLOP IMPLEMENTATION FACT DECLARATION
*/
#include "core99/zhang_y14.hpp"

#include <filesystem>
#include <fstream>
#include <iomanip>
#include <iostream>
#include <sstream>
#include <stdexcept>
#include <string>

namespace {

struct Arguments {
    std::string action = "optimize";
    std::string case_id = "Y14_n16_original";
    core99::y14::RunConfig config;
    std::filesystem::path output;
};

Arguments parse(const int argc, char** argv) {
    Arguments result;
    for (int index = 1; index < argc; ++index) {
        const std::string key = argv[index];
        auto value = [&]() {
            if (++index >= argc) throw std::invalid_argument("Y14 missing "+key);
            return std::string(argv[index]);
        };
        if (key == "--action") result.action = value();
        else if (key == "--case") result.case_id = value();
        else if (key == "--seed") result.config.seed = std::stoull(value());
        else if (key == "--workers") result.config.workers = std::stoi(value());
        else if (key == "--population") result.config.population = std::stoi(value());
        else if (key == "--subpopulation") {
            result.config.subpopulation = std::stoi(value());
        } else if (key == "--maximum-evaluation-slots") {
            result.config.maximum_evaluation_slots = std::stoull(value());
        } else if (key == "--crossover-rate") {
            result.config.crossover_rate = std::stod(value());
        } else if (key == "--learning-period") {
            result.config.learning_period = std::stoi(value());
        } else if (key == "--output") result.output = value();
        else throw std::invalid_argument("Y14 unknown option "+key);
    }
    return result;
}

core99::y14::Scenario scenario(const std::string& id) {
    for (const auto& item : core99::y14::paper_scenarios()) {
        if (item.case_id == id) return item;
    }
    throw std::invalid_argument("unknown Y14 case "+id);
}

std::string evaluation_json(const core99::y14::Evaluation& value) {
    std::ostringstream out;
    out << std::setprecision(17)
        << "{\"negative_aep_gwh\":" << value.negative_aep_gwh
        << ",\"aep_gwh\":" << -value.negative_aep_gwh
        << ",\"spl_db\":" << value.spl_db
        << ",\"spacing_violation_m\":" << value.spacing_violation_m
        << ",\"boundary_violation_m\":" << value.boundary_violation_m
        << ",\"feasible\":" << (value.feasible ? "true" : "false") << '}';
    return out.str();
}

std::string layout_json(const std::vector<core99::y14::Point>& layout) {
    std::ostringstream out;
    out << std::setprecision(17) << '[';
    for (std::size_t index = 0; index < layout.size(); ++index) {
        if (index) out << ',';
        out << "[" << layout[index].x_m << ',' << layout[index].y_m << ']';
    }
    return out.str()+"]";
}

std::string metadata_json(const core99::y14::Problem& problem) {
    const auto& item = problem.scenario();
    std::ostringstream out;
    out << std::setprecision(17)
        << "{\"case_id\":\"" << item.case_id << "\""
        << ",\"turbine_count\":" << item.turbine_count
        << ",\"length_m\":" << item.length_m
        << ",\"width_m\":" << item.width_m
        << ",\"receiver_count\":" << problem.receiver_count()
        << ",\"rotor_radius_m\":" << problem.rotor_radius_m()
        << ",\"minimum_spacing_m\":" << problem.minimum_spacing_m()
        << ",\"reference_negative_aep_gwh\":"
        << item.reference_negative_aep_gwh
        << ",\"reference_spl_db\":" << item.reference_spl_db
        << ",\"adjusted_preference\":"
        << (item.adjusted_preference ? "true" : "false")
        << ",\"wind_probabilities\":[";
    for (std::size_t index=0; index<problem.wind_probabilities().size(); ++index) {
        if (index) out << ',';
        out << problem.wind_probabilities()[index];
    }
    return out.str()+"]}";
}

std::string result_json(const core99::y14::RunResult& result) {
    std::ostringstream out;
    out << std::setprecision(17)
        << "{\"case_id\":\"" << result.case_id << "\""
        << ",\"method_semantic_id\":\"" << result.method_semantic_id << "\""
        << ",\"problem_semantic_id\":\"" << result.problem_semantic_id << "\""
        << ",\"protocol_semantic_id\":\"" << result.protocol_semantic_id << "\""
        << ",\"seed\":" << result.seed
        << ",\"requested_workers\":" << result.requested_workers
        << ",\"observed_workers\":" << result.observed_workers
        << ",\"population\":" << result.population
        << ",\"generations\":" << result.generations
        << ",\"nominal_evaluation_slots\":" << result.nominal_evaluation_slots
        << ",\"physical_fes\":" << result.physical_fes
        << ",\"evaluator_seconds\":" << result.evaluator_seconds
        << ",\"algorithm_seconds\":" << result.algorithm_seconds
        << ",\"end_to_end_seconds\":" << result.end_to_end_seconds
        << ",\"scientific_hash\":" << result.scientific_hash
        << ",\"front\":[";
    for (std::size_t index = 0; index < result.front.size(); ++index) {
        if (index) out << ',';
        out << "{\"evaluation\":"
            << evaluation_json(result.front[index].evaluation)
            << ",\"layout\":" << layout_json(result.front[index].layout) << '}';
    }
    return out.str()+"]}";
}

void emit(const std::string& text, const std::filesystem::path& output) {
    if (output.empty()) {
        std::cout << text << '\n';
        return;
    }
    std::filesystem::create_directories(output.parent_path());
    const auto temporary = output.string()+".tmp";
    std::ofstream stream(temporary);
    if (!stream) throw std::runtime_error("cannot open Y14 output");
    stream << text << '\n';
    stream.close();
    std::filesystem::rename(temporary,output);
}

}  // namespace

int main(int argc, char** argv) {
    try {
        const auto arguments = parse(argc,argv);
        if (arguments.action == "list-cases") {
            std::ostringstream out;
            out << '[';
            const auto cases = core99::y14::paper_scenarios();
            for (std::size_t index=0; index<cases.size(); ++index) {
                if (index) out << ',';
                out << '"' << cases[index].case_id << '"';
            }
            emit(out.str()+"]",arguments.output);
            return 0;
        }
        const core99::y14::Problem problem(scenario(arguments.case_id));
        if (arguments.action == "metadata") {
            emit(metadata_json(problem),arguments.output);
        } else if (arguments.action == "evaluate-reference") {
            const auto layout = problem.reference_layout();
            emit(
                "{\"metadata\":"+metadata_json(problem)
                    +",\"evaluation\":"+evaluation_json(problem.evaluate(layout))
                    +",\"layout\":"+layout_json(layout)+"}",
                arguments.output
            );
        } else if (arguments.action == "optimize") {
            emit(result_json(core99::y14::run(problem,arguments.config)),arguments.output);
        } else {
            throw std::invalid_argument("unknown Y14 action "+arguments.action);
        }
    } catch (const std::exception& error) {
        std::cerr << "Y14 error: " << error.what() << '\n';
        return 2;
    }
    return 0;
}
