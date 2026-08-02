/*
WFLOP IMPLEMENTATION FACT DECLARATION
Implementation unit: T28 native C++/LibTorch CPU/CUDA command-line driver
Paper DOI: 10.5194/wes-10-1661-2025
Public source/data: WINDFLOWER v1.0.0, DOI 10.5281/zenodo.13946931.
Missing/conflicts/resolution/HPC/claim boundary:
include/core99/nguyen_t28.hpp.
Last evidence-audit date: 2026-07-31
END WFLOP IMPLEMENTATION FACT DECLARATION
*/
#include "core99/nguyen_t28.hpp"

#include <fstream>
#include <iostream>
#include <stdexcept>
#include <string>

int main(int argc, char** argv) {
    try {
        core99::t28::Configuration config;
        std::string output;
        for (int i = 1; i < argc; ++i) {
            const std::string option = argv[i];
            if (option == "--help") {
                std::cout << "core99_t28_hpc --data DIR --backend auto|cpu|cuda "
                             "--objective aep|daem|jerm --year 2023 "
                             "--evaluation-year 2023|2024 "
                             "--samples 20..150 --forecasts 10 --iterations 2000 "
                             "--workers 20 --seed 0..4 --reserve-limit 50|117|221.4\n";
                return 0;
            }
            if (++i >= argc) throw std::invalid_argument("missing value for " + option);
            const std::string value = argv[i];
            if (option == "--data") config.data_directory = value;
            else if (option == "--backend") config.backend = value;
            else if (option == "--year") config.year = std::stoi(value);
            else if (option == "--evaluation-year") config.evaluation_year = std::stoi(value);
            else if (option == "--samples") config.samples_per_iteration = std::stoi(value);
            else if (option == "--forecasts") config.forecasts = std::stoi(value);
            else if (option == "--iterations") config.iterations = std::stoi(value);
            else if (option == "--evaluation-limit") config.evaluation_limit = std::stoi(value);
            else if (option == "--workers") config.workers = std::stoi(value);
            else if (option == "--seed") config.seed = std::stoi(value);
            else if (option == "--reserve-limit") config.reserve_limit_mw = std::stod(value);
            else if (option == "--learning-rate") config.learning_rate_m = std::stod(value);
            else if (option == "--output") output = value;
            else if (option == "--objective") {
                if (value == "aep") config.objective = core99::t28::Objective::Aep;
                else if (value == "daem") config.objective = core99::t28::Objective::Daem;
                else if (value == "jerm") config.objective = core99::t28::Objective::Jerm;
                else throw std::invalid_argument("invalid T28 objective");
            } else throw std::invalid_argument("unknown T28 option " + option);
        }
        if (config.data_directory.empty()) throw std::invalid_argument("--data required");
        const auto payload = core99::t28::to_json(core99::t28::run(config), config);
        if (output.empty()) std::cout << payload << "\n";
        else {
            std::ofstream stream(output);
            if (!stream) throw std::runtime_error("cannot open T28 output");
            stream << payload << "\n";
        }
        return 0;
    } catch (const std::exception& error) {
        std::cerr << "core99_t28_error: " << error.what() << "\n";
        return 2;
    }
}
