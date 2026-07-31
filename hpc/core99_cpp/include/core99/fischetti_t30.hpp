/*
WFLOP IMPLEMENTATION FACT DECLARATION
Implementation unit: T30 proximity-search method and large random-site WFLOP
Paper DOI: 10.1007/s10732-015-9283-4
Public source: no paper-linked implementation or instance corpus found.
Public supplementary authority: Martina Fischetti, Mixed Integer Programming
Models and Algorithms for Wind Farm Layout, open thesis handle
20.500.12608/17839.
Paper-provided facts: 3000 m square; 400 m spacing; Siemens SWT-2.3-93;
Jensen pairwise cumulative interference; 250000+ confidential real wind
samples clustered to about 500 macro scenarios; interference at most 0.01 MW
discarded; 10 instances at n=1000,5000,10000,15000,20000; iterated O(n)
delta 1-opt, conservative 2-opt, 2000-site reduction, simplified then complete
compact MIP proximity search; CPLEX 12.5.1; 60-3600 second checkpoints.
Missing facts: author C source, 50 instance seeds/coordinates, private
Vattenfall wind observations and clusters, exact SWT curve table, clustering
procedure, proximity theta/U updates and original random bitstreams.
Reconstruction: declare deterministic instance seeds; replace unavailable wind
observations by a frozen 504-state offshore wind rose; reconstruct the stated
3-16 m/s nonlinear SWT curve; use the paper Jensen equations; and replace
non-redistributable CPLEX 12.5.1 with pinned HiGHS revision
04024d701f79feb8e2f18bc3df0dffc04ef05088. Preserve the compact model,
soft cutoff, Hamming proximity objective, 2000-site reduction, simplified/full
staging, 1-opt/2-opt cleanup and every paper size/time checkpoint.
HPC design: one persistent all-core C++ team builds the packed pair matrix and
HiGHS receives all available threads for reduced MIP solves. The ordered O(n)
parametric 1-opt updates and O(n*gamma) 2-opt scans were analyzed and retained
as deterministic contiguous C++ loops: their attempted fine-grained parallel
regions changed the fixed-work path and added more synchronization than useful
work. Matrix construction is cacheable and reported separately because the
paper excludes Step 0 from optimization time. Incumbent commits remain ordered.
Method semantic ID: fischetti2016_proxy_highs_reconstruction_v1
Problem semantic ID: fischetti2016_random50_jensen_declared_v1
Protocol semantic ID: fischetti2016_5x10x7_proxy_v1
Claim boundary: academic paper-first reconstruction, not author C/CPLEX code,
private wind-data replay, original instance replay, or table-number identity.
Contract: shared/contracts/core99_t30_fischetti_proxy_2016.json
Last evidence-audit date: 2026-07-31
END WFLOP IMPLEMENTATION FACT DECLARATION
*/
#pragma once

#include <cstdint>
#include <filesystem>
#include <string>
#include <vector>

namespace core99::t30 {

struct Position {
    double x_m = 0.0;
    double y_m = 0.0;
};

struct Configuration {
    int sites = 1000;
    int instance = 0;
    int workers = 20;
    double time_limit_seconds = 60.0;
    std::uint64_t fixed_moves = 0;
    std::uint64_t seed = 0;
    std::filesystem::path matrix_cache;
};

struct Result {
    int sites = 0;
    int instance = 0;
    int turbines = 0;
    int requested_workers = 0;
    int observed_workers = 0;
    double initial_objective_mw = 0.0;
    double best_objective_mw = 0.0;
    double minimum_spacing_m = 0.0;
    double matrix_seconds = 0.0;
    double local_search_seconds = 0.0;
    double mip_seconds = 0.0;
    double optimization_seconds = 0.0;
    double end_to_end_seconds = 0.0;
    std::uint64_t pair_evaluations = 0;
    std::uint64_t delta_evaluations = 0;
    std::uint64_t moves = 0;
    std::vector<int> selected;
    std::string mip_status;
    std::uint64_t scientific_hash = 0;
};

class Problem {
public:
    Problem(
        int sites,
        int instance,
        int workers,
        const std::filesystem::path& matrix_cache = {}
    );
    [[nodiscard]] int size() const noexcept;
    [[nodiscard]] const std::vector<Position>& positions() const noexcept;
    [[nodiscard]] double free_power_mw(int site) const noexcept;
    [[nodiscard]] double pair_loss_mw(int first, int second) const noexcept;
    [[nodiscard]] bool spacing_conflict(int first, int second) const noexcept;
    [[nodiscard]] double evaluate(const std::vector<int>& selected) const;
    [[nodiscard]] double matrix_seconds() const noexcept;
    [[nodiscard]] int matrix_observed_workers() const noexcept;
    [[nodiscard]] std::uint64_t matrix_hash() const noexcept;

private:
    int sites_ = 0;
    int instance_ = 0;
    std::vector<Position> positions_;
    std::vector<float> packed_loss_;
    double free_power_mw_ = 0.0;
    double matrix_seconds_ = 0.0;
    int matrix_observed_workers_ = 0;
    std::uint64_t matrix_hash_ = 0;
};

[[nodiscard]] Result run(const Problem& problem, const Configuration& config);
[[nodiscard]] std::string to_json(const Result& result);

}  // namespace core99::t30
