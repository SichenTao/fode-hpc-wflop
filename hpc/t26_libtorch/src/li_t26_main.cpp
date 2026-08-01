/*
WFLOP IMPLEMENTATION FACT DECLARATION
Implementation unit: T26 native C++/LibTorch CPU/CUDA CLI and JSON receipts
Paper/DOI: Li et al.; 10.1016/j.apenergy.2025.125908.
Public source provenance, Missing information, Reconstruction, semantic IDs,
production backend, controlling Contract and Claim boundary:
include/core99/li_t26.hpp. This is a project-native implementation.
Last evidence-audit date: 2026-08-01
END WFLOP IMPLEMENTATION FACT DECLARATION
*/
#include "core99/li_t26.hpp"

#include <fstream>
#include <iostream>
#include <stdexcept>
#include <string>

namespace {

struct Arguments {
    std::string action = "optimize";
    std::string backend = "auto";
    std::string artifact;
    std::string output;
    int workers = 20;
    int iterations = 10000;
    int batch_size = 1024;
    int generations = 1000;
    int population = 300;
    int evaluation_limit = 0;
    std::uint64_t seed = 26001;
    bool smoke = false;
};

Arguments parse(const int argc, char** argv) {
    Arguments result;
    for (int index = 1; index < argc; ++index) {
        const std::string flag = argv[index];
        auto value = [&]() {
            if (++index >= argc) throw std::invalid_argument("T26 missing " + flag);
            return std::string(argv[index]);
        };
        if (flag == "--action") result.action = value();
        else if (flag == "--backend") result.backend = value();
        else if (flag == "--artifact") result.artifact = value();
        else if (flag == "--output") result.output = value();
        else if (flag == "--workers") result.workers = std::stoi(value());
        else if (flag == "--iterations") result.iterations = std::stoi(value());
        else if (flag == "--batch-size") result.batch_size = std::stoi(value());
        else if (flag == "--generations") result.generations = std::stoi(value());
        else if (flag == "--population") result.population = std::stoi(value());
        else if (flag == "--evaluation-limit") result.evaluation_limit = std::stoi(value());
        else if (flag == "--seed") result.seed = std::stoull(value());
        else if (flag == "--smoke") result.smoke = true;
        else throw std::invalid_argument("T26 unknown flag " + flag);
    }
    return result;
}

void emit(const std::string& text, const std::string& path) {
    if (!path.empty()) {
        std::ofstream stream(path);
        if (!stream) throw std::runtime_error("T26 cannot write output " + path);
        stream << text << '\n';
    }
    std::cout << text << '\n';
}

}  // namespace

int main(int argc, char** argv) {
    try {
        const auto arguments = parse(argc, argv);
        if (arguments.action == "train") {
            core99::t26::TrainingConfig config;
            config.backend = arguments.backend;
            config.artifact = arguments.artifact;
            config.iterations = arguments.iterations;
            config.batch_size = arguments.batch_size;
            config.workers = arguments.workers;
            config.seed = arguments.seed;
            config.smoke = arguments.smoke;
            emit(core99::t26::training_json(core99::t26::train_pidnn(config), config),
                 arguments.output);
            return 0;
        }
        if (arguments.action == "optimize") {
            core99::t26::OptimizationConfig config;
            config.backend = arguments.backend;
            config.artifact = arguments.artifact;
            config.generations = arguments.generations;
            config.population = arguments.population;
            config.workers = arguments.workers;
            config.seed = arguments.seed;
            config.evaluation_limit = arguments.evaluation_limit;
            config.smoke = arguments.smoke;
            emit(core99::t26::optimization_json(core99::t26::run_gtde(config), config),
                 arguments.output);
            return 0;
        }
        throw std::invalid_argument("T26 action must be train or optimize");
    } catch (const std::exception& error) {
        std::cerr << "T26 error: " << error.what() << '\n';
        return 1;
    }
}
