/*
WFLOP IMPLEMENTATION FACT DECLARATION
Implementation unit: T31 official-data, matrix, VNS and deterministic-HPC test
Paper DOI: 10.1016/j.cor.2021.105588
Dataset DOI: 10.11583/DTU.13134731.
Missing/completions/claim boundary: include/core99/cazzaro_t31.hpp.
Last evidence-audit date: 2026-07-31
END WFLOP IMPLEMENTATION FACT DECLARATION
*/
#include "core99/cazzaro_t31.hpp"

#include <filesystem>
#include <iostream>
#include <stdexcept>
#include <tuple>

int main(int argc, char** argv) {
    try {
        if (argc != 2) {
            throw std::runtime_error("T31 extracted data root required");
        }
        if (core99::t31::paper_case_ids().size() != 13) {
            throw std::runtime_error("T31 paper case count");
        }
        const core99::t31::Problem official(
            std::filesystem::path(argv[1]),
            "t31_official_a",
            core99::t31::FoundationMode::none,
            4
        );
        if (
            official.info().available_positions != 3196
            || official.info().fixed_turbines != 42
            || official.info().zone_quotas
                != std::vector<int>({26, 14})
            || official.info().wind_states < 100
            || official.minimum_spacing_m() != 1200.0
        ) {
            throw std::runtime_error("T31 official dataset identity");
        }
        const std::vector<std::tuple<
            std::string,
            int,
            int,
            std::vector<int>
        >> official_identity{
            {"t31_official_a",3196,42,{26,14}},
            {"t31_official_b",6974,15,{99}},
            {"t31_official_c",7090,8,{60,30}},
            {"t31_official_d",10398,45,{170}},
            {"t31_official_e",11478,40,{7,94,36}},
            {"t31_official_f",11536,12,{132,26}},
            {"t31_official_g",14602,35,{140}},
            {"t31_official_h",19458,40,{158,30}},
            {"t31_official_i",20211,36,{313}},
            {"t31_official_j",21634,75,{136,74,25}},
        };
        for (const auto& [case_id, positions, fixed, quotas]
             : official_identity) {
            const core99::t31::Problem instance(
                std::filesystem::path(argv[1]),
                case_id,
                core99::t31::FoundationMode::none,
                4
            );
            if (
                instance.info().available_positions != positions
                || instance.info().fixed_turbines != fixed
                || instance.info().zone_quotas != quotas
                || instance.info().wind_states != 177
                || !(instance.preprocessing_seconds() > 0.0)
            ) {
                throw std::runtime_error(
                    "T31 official ten-site identity " + case_id
                );
            }
        }
        const core99::t31::Problem mosetti(
            {},
            "t31_mosetti_di",
            core99::t31::FoundationMode::none,
            4
        );
        core99::t31::RunConfig config;
        config.foundation_mode = core99::t31::FoundationMode::none;
        config.time_limit_seconds = 30.0;
        config.fixed_iterations = 1;
        config.workers = 1;
        const auto serial = core99::t31::run(mosetti, 311, config);
        config.workers = 4;
        const auto parallel = core99::t31::run(mosetti, 311, config);
        if (
            serial.scientific_hash != parallel.scientific_hash
            || serial.best_positions.size() != 26
            || serial.completed_vns_iterations != 1
            || serial.matrix_pair_evaluations != 4950
            || serial.best.spacing_violation_m > 1.0e-9
            || serial.best.objective_mwh_equivalent
                + 1.0e-8 < serial.initial.objective_mwh_equivalent
            || parallel.observed_workers != 4
        ) {
            throw std::runtime_error("T31 deterministic VNS lifecycle");
        }
        std::cout << "t31_cpp_test_pass hash=" << std::hex
                  << serial.scientific_hash << std::dec
                  << " official_positions="
                  << official.info().available_positions << "\n";
        return 0;
    } catch (const std::exception& error) {
        std::cerr << error.what() << "\n";
        return 1;
    }
}
