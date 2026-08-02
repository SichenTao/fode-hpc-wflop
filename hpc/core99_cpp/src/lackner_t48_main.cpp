/*
WFLOP IMPLEMENTATION FACT DECLARATION
Implementation unit: T48 pure-C++ evaluation and optimization CLI
Paper title and DOI: An Analytical Framework for Offshore Wind Farm Layout
Optimization, 10.1260/030952407780811401.
Public source: no paper-linked author code or numeric archive was located.
Missing fields and Reconstruction: include/core99/lackner_t48.hpp.
Semantic IDs and Contract: shared/contracts/core99_t48_lackner_2007.json.
Claim boundary: academic declared reconstruction, not author source or exact
190-step replay.
Last evidence-audit date: 2026-07-31
END WFLOP IMPLEMENTATION FACT DECLARATION
*/
#include "core99/lackner_t48.hpp"

#include <cstdlib>
#include <fstream>
#include <iomanip>
#include <iostream>
#include <sstream>
#include <stdexcept>
#include <string>
#include <vector>

namespace {

struct Arguments {
    std::string mode = "optimize";
    std::string layout = "initial";
    std::string variables;
    std::string output;
    std::uint64_t seed = 20260731;
    int iterations = 190;
    int workers = 20;
};

Arguments parse(const int argc, char** argv) {
    Arguments args;
    for (int index = 1; index < argc; ++index) {
        const std::string flag = argv[index];
        auto value = [&]() -> std::string {
            if (++index >= argc) {
                throw std::invalid_argument("missing value for " + flag);
            }
            return argv[index];
        };
        if (flag == "--mode") {
            args.mode = value();
        } else if (flag == "--layout") {
            args.layout = value();
        } else if (flag == "--variables") {
            args.variables = value();
        } else if (flag == "--output") {
            args.output = value();
        } else if (flag == "--seed") {
            args.seed = std::stoull(value());
        } else if (flag == "--iterations") {
            args.iterations = std::stoi(value());
        } else if (flag == "--workers") {
            args.workers = std::stoi(value());
        } else {
            throw std::invalid_argument("unknown flag: " + flag);
        }
    }
    return args;
}

std::vector<double> parse_vector(const std::string& text) {
    std::vector<double> result;
    std::stringstream stream(text);
    std::string token;
    while (std::getline(stream, token, ',')) {
        if (!token.empty()) {
            result.push_back(std::stod(token));
        }
    }
    return result;
}

std::string vector_json(const std::vector<double>& values) {
    std::ostringstream out;
    out << std::setprecision(17) << '[';
    for (std::size_t index = 0; index < values.size(); ++index) {
        if (index != 0) {
            out << ',';
        }
        out << values[index];
    }
    out << ']';
    return out.str();
}

std::string fields(const core99::t48::Evaluation& value) {
    std::ostringstream out;
    out << std::setprecision(17)
        << "\"lcoe_dollars_per_kwh\":"
        << value.lcoe_dollars_per_kwh << ','
        << "\"capital_cost_dollars\":"
        << value.capital_cost_dollars << ','
        << "\"annual_energy_kwh\":"
        << value.annual_energy_kwh << ','
        << "\"capacity_factor\":" << value.capacity_factor << ','
        << "\"wake_loss_fraction\":" << value.wake_loss_fraction << ','
        << "\"constraint_violation\":" << value.constraint_violation;
    return out.str();
}

void emit(const std::string& payload, const std::string& path) {
    if (path.empty()) {
        std::cout << payload;
        return;
    }
    std::ofstream stream(path);
    if (!stream) {
        throw std::runtime_error("cannot open output: " + path);
    }
    stream << payload;
}

}  // namespace

int main(int argc, char** argv) {
    try {
        const auto args = parse(argc, argv);
        const core99::t48::Problem problem;
        std::ostringstream out;
        out << std::setprecision(17);
        if (args.mode == "evaluate") {
            std::vector<double> variables;
            if (!args.variables.empty()) {
                variables = parse_vector(args.variables);
            } else if (args.layout == "initial") {
                variables = core99::t48::paper_initial_layout();
            } else if (args.layout == "paper-final") {
                variables = core99::t48::paper_reported_final_layout();
            } else {
                throw std::invalid_argument("unknown T48 layout");
            }
            const auto value = problem.evaluate(variables);
            out << "{\"mode\":\"evaluation\","
                << "\"problem_semantic_id\":"
                   "\"t48_lackner_two_turbine_lcoe_declared_v1\","
                << "\"variables\":" << vector_json(variables) << ','
                << fields(value) << "}\n";
        } else if (args.mode == "optimize") {
            const auto result = core99::t48::run(
                problem,
                args.seed,
                args.iterations,
                args.workers
            );
            out << "{\"mode\":\"optimization\","
                << "\"method_semantic_id\":"
                   "\"t48_lackner_gradient_coordinate_reconstruction_v1\","
                << "\"problem_semantic_id\":"
                   "\"t48_lackner_two_turbine_lcoe_declared_v1\","
                << "\"initial_variables\":"
                << vector_json(result.initial_variables) << ','
                << "\"initial_evaluation\":{"
                << fields(result.initial_evaluation) << "},"
                << "\"best_variables\":"
                << vector_json(result.best_variables) << ','
                << "\"best_evaluation\":{"
                << fields(result.best_evaluation) << "},"
                << "\"seed\":" << result.seed << ','
                << "\"iterations\":" << result.iterations << ','
                << "\"physical_fes\":" << result.physical_fes << ','
                << "\"requested_workers\":"
                << result.requested_workers << ','
                << "\"observed_workers\":" << result.observed_workers << ','
                << "\"evaluator_seconds\":"
                << result.evaluator_seconds << ','
                << "\"algorithm_seconds\":"
                << result.algorithm_seconds << ','
                << "\"end_to_end_seconds\":"
                << result.end_to_end_seconds << ','
                << "\"scientific_hash\":\"" << std::hex
                << result.scientific_hash << std::dec << "\"}\n";
        } else {
            throw std::invalid_argument("unknown T48 mode");
        }
        emit(out.str(), args.output);
        return EXIT_SUCCESS;
    } catch (const std::exception& error) {
        std::cerr << "core99_t48_hpc error: " << error.what() << '\n';
        return EXIT_FAILURE;
    }
}
