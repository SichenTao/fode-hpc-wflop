/*
WFLOP IMPLEMENTATION FACT DECLARATION
Implementation unit: SUGGA paper-native Monte-Carlo response generator
Paper title: Wind farm layout optimization based on support vector regression
guided genetic algorithm with consideration of participation among landowners
DOI: 10.1016/j.enconman.2019.06.082
Paper provides: 10000 random layouts per case, mean cell power response, RBF
SVR response surface, and case-specific frozen models
Public author code URL: recorded in the SUGGA source dossier and source ledger
Missing choices completed here: deterministic feasible-layout sampler, exact
physical-FES ledger, batch size, and zero response for unavailable cells
Reconstruction performed here: pure C++ all-core physical evaluation and
fixed-order cell aggregation; fitting is performed by the paired Python SVR
driver with its parameters recorded in the output receipt
Method semantic ID: sugga_native_train_from_scratch_v1
Problem semantic ID: supplied by the selected immutable case contract
Claim boundary: deterministic paper-guided model training; no unavailable
author response-surface identity or reported-result reproduction claim
Last evidence audit date: 2026-07-30
END WFLOP IMPLEMENTATION FACT DECLARATION
*/

#include "fode/case.hpp"
#include "fode/evaluator.hpp"
#include "fode/executor.hpp"
#include "fode/rng.hpp"

#include <algorithm>
#include <cstddef>
#include <cstdint>
#include <filesystem>
#include <fstream>
#include <iomanip>
#include <iostream>
#include <stdexcept>
#include <string>
#include <utility>
#include <vector>

namespace {

struct Arguments {
    std::string cases;
    std::string output_directory;
    std::uint64_t seed = 2019060820260730ULL;
    std::uint64_t samples = 10000;
    int workers = 0;
    int batch_size = 256;
};

Arguments parse(int argc, char** argv) {
    Arguments result;
    for (int index = 1; index < argc; ++index) {
        const std::string option = argv[index];
        if (option == "--help") {
            std::cout
                << "usage: sugga_mc_response --cases FILE --output-dir DIR "
                << "[--samples 10000] [--seed N] [--workers 0] "
                << "[--batch-size 256]\n";
            std::exit(0);
        }
        if (index + 1 >= argc) {
            throw std::invalid_argument("missing value for " + option);
        }
        const std::string value = argv[++index];
        if (option == "--cases") {
            result.cases = value;
        } else if (option == "--output-dir") {
            result.output_directory = value;
        } else if (option == "--samples") {
            result.samples = std::stoull(value);
        } else if (option == "--seed") {
            result.seed = std::stoull(value);
        } else if (option == "--workers") {
            result.workers = std::stoi(value);
        } else if (option == "--batch-size") {
            result.batch_size = std::stoi(value);
        } else {
            throw std::invalid_argument("unknown option " + option);
        }
    }
    if (result.cases.empty() || result.output_directory.empty()) {
        throw std::invalid_argument("--cases and --output-dir are required");
    }
    if (result.samples == 0 || result.batch_size <= 0) {
        throw std::invalid_argument("samples and batch size must be positive");
    }
    return result;
}

std::vector<int> allowed_cells(const fode::CaseData& data) {
    std::vector<char> blocked(
        static_cast<std::size_t>(data.rows * data.cols), 0
    );
    for (const int cell : data.unavailable_cells_1based) {
        blocked[static_cast<std::size_t>(cell - 1)] = 1;
    }
    std::vector<int> result;
    for (int cell = 1; cell <= data.rows * data.cols; ++cell) {
        if (blocked[static_cast<std::size_t>(cell - 1)] == 0) {
            result.push_back(cell);
        }
    }
    if (result.size() < static_cast<std::size_t>(data.turbine_count)) {
        throw std::runtime_error(data.case_id + ": too few feasible cells");
    }
    return result;
}

std::uint64_t case_hash(const std::string& text) {
    std::uint64_t value = 1469598103934665603ULL;
    for (const unsigned char byte : text) {
        value ^= static_cast<std::uint64_t>(byte);
        value *= 1099511628211ULL;
    }
    return value;
}

std::vector<double> generate_batch(
    const fode::CaseData& data,
    const std::vector<int>& allowed,
    const fode::CounterRng& rng,
    std::uint64_t first_sample,
    int batch_size
) {
    std::vector<double> layouts(
        static_cast<std::size_t>(batch_size * data.turbine_count), 0.0
    );
    for (int row = 0; row < batch_size; ++row) {
        const std::uint64_t sample =
            first_sample + static_cast<std::uint64_t>(row);
        std::vector<std::pair<double, int>> keyed;
        keyed.reserve(allowed.size());
        for (std::size_t index = 0; index < allowed.size(); ++index) {
            keyed.emplace_back(
                rng.uniform(sample, 1, index),
                allowed[index]
            );
        }
        std::partial_sort(
            keyed.begin(),
            keyed.begin() + data.turbine_count,
            keyed.end()
        );
        std::vector<int> selected;
        selected.reserve(static_cast<std::size_t>(data.turbine_count));
        for (int d = 0; d < data.turbine_count; ++d) {
            selected.push_back(keyed[static_cast<std::size_t>(d)].second);
        }
        std::sort(selected.begin(), selected.end());
        for (int d = 0; d < data.turbine_count; ++d) {
            layouts[static_cast<std::size_t>(
                row * data.turbine_count + d
            )] = static_cast<double>(
                selected[static_cast<std::size_t>(d)]
            );
        }
    }
    return layouts;
}

void train_case(
    const fode::CaseData& data,
    const Arguments& arguments,
    fode::PersistentExecutor& executor
) {
    const int cells = data.rows * data.cols;
    const auto allowed = allowed_cells(data);
    fode::CounterRng rng(
        arguments.seed
        ^ case_hash(data.case_id)
    );
    std::vector<double> power_sum(static_cast<std::size_t>(cells), 0.0);
    std::vector<std::uint64_t> count(static_cast<std::size_t>(cells), 0);
    std::uint64_t completed = 0;
    while (completed < arguments.samples) {
        const int batch = static_cast<int>(
            std::min<std::uint64_t>(
                static_cast<std::uint64_t>(arguments.batch_size),
                arguments.samples - completed
            )
        );
        const auto layouts = generate_batch(
            data, allowed, rng, completed, batch
        );
        const auto evaluated = fode::evaluate_population_hpc(
            layouts,
            batch,
            data,
            executor,
            fode::EvaluationDetail::TotalAndPerTurbine,
            fode::EvaluationSchedule::GranularityAware
        );
        for (int row = 0; row < batch; ++row) {
            for (int rank = 0; rank < data.turbine_count; ++rank) {
                const std::size_t offset = static_cast<std::size_t>(
                    row * data.turbine_count + rank
                );
                const int cell =
                    evaluated.turbine_position_order_1based[offset];
                power_sum[static_cast<std::size_t>(cell - 1)] +=
                    evaluated.accumulated_turbine_power_kw[offset];
                ++count[static_cast<std::size_t>(cell - 1)];
            }
        }
        completed += static_cast<std::uint64_t>(batch);
    }

    const std::filesystem::path output =
        std::filesystem::path(arguments.output_directory)
        / (data.case_id + ".mc.tsv");
    std::ofstream stream(output);
    if (!stream) {
        throw std::runtime_error("cannot write " + output.string());
    }
    stream << "cell_1based\tx\ty\tmean_power_kw\tobservations\n";
    stream << std::setprecision(17);
    for (int cell = 1; cell <= cells; ++cell) {
        const std::size_t index = static_cast<std::size_t>(cell - 1);
        const int y = (cell - 1) / data.cols;
        const int x = (cell - 1) - y * data.cols;
        const double response = count[index] == 0
            ? 0.0
            : power_sum[index] / static_cast<double>(count[index]);
        stream << cell << '\t' << x << '\t' << y << '\t'
               << response << '\t' << count[index] << '\n';
    }
    std::cerr << "sugga_mc_case_complete case=" << data.case_id
              << " physical_fes=" << completed << '\n';
}

}  // namespace

int main(int argc, char** argv) {
    try {
        const Arguments arguments = parse(argc, argv);
        std::filesystem::create_directories(arguments.output_directory);
        fode::PersistentExecutor executor(arguments.workers);
        const auto cases = fode::load_cases(arguments.cases);
        for (const auto& data : cases) {
            train_case(data, arguments, executor);
        }
        std::cout << "{\"case_count\":" << cases.size()
                  << ",\"samples_per_case\":" << arguments.samples
                  << ",\"training_physical_fes\":"
                  << arguments.samples * cases.size()
                  << ",\"observed_workers\":" << executor.thread_count()
                  << "}\n";
        return 0;
    } catch (const std::exception& error) {
        std::cerr << "error: " << error.what() << '\n';
        return 1;
    }
}
