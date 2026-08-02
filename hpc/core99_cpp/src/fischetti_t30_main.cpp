/*
WFLOP IMPLEMENTATION FACT DECLARATION
Implementation unit: T30 pure-C++/HiGHS command-line driver
Paper DOI: 10.1007/s10732-015-9283-4
Public source: none found; open author thesis 20.500.12608/17839.
Missing/conflicts/reconstruction/HPC/claim boundary:
include/core99/fischetti_t30.hpp.
Semantic IDs and Contract: shared/contracts/core99_t30_fischetti_proxy_2016.json.
Last evidence-audit date: 2026-07-31
END WFLOP IMPLEMENTATION FACT DECLARATION
*/
#include "core99/fischetti_t30.hpp"

#include <fstream>
#include <iostream>
#include <stdexcept>
#include <string>

int main(int argc, char** argv) {
    try {
        core99::t30::Configuration config;
        std::string output;
        for (int i = 1; i < argc; ++i) {
            const std::string option = argv[i];
            if (option == "--help") {
                std::cout
                    << "core99_t30_hpc --sites 1000|5000|10000|15000|20000 "
                    << "--instance 0..9 --workers 20 --time-limit 60..3600 "
                    << "--fixed-moves N --seed N --matrix-cache FILE "
                    << "--output FILE\n";
                return 0;
            }
            if (++i >= argc) {
                throw std::invalid_argument("missing value for " + option);
            }
            const std::string value = argv[i];
            if (option == "--sites") config.sites = std::stoi(value);
            else if (option == "--instance") config.instance = std::stoi(value);
            else if (option == "--workers") config.workers = std::stoi(value);
            else if (option == "--time-limit") config.time_limit_seconds = std::stod(value);
            else if (option == "--fixed-moves") config.fixed_moves = std::stoull(value);
            else if (option == "--seed") config.seed = std::stoull(value);
            else if (option == "--matrix-cache") config.matrix_cache = value;
            else if (option == "--output") output = value;
            else throw std::invalid_argument("unknown T30 option " + option);
        }
        core99::t30::Problem problem(
            config.sites, config.instance, config.workers, config.matrix_cache
        );
        const std::string payload = core99::t30::to_json(
            core99::t30::run(problem, config)
        );
        if (output.empty()) std::cout << payload << "\n";
        else {
            std::ofstream stream(output);
            if (!stream) throw std::runtime_error("cannot open T30 output");
            stream << payload << "\n";
        }
        return 0;
    } catch (const std::exception& error) {
        std::cerr << "core99_t30_error: " << error.what() << "\n";
        return 2;
    }
}
