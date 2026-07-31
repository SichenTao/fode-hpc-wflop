/*
WFLOP IMPLEMENTATION FACT DECLARATION
Implementation unit: T83 multi-scale offshore WFASP problem and solver API
Paper title: Multi-Scale Optimization of the Design of Offshore Wind Farms
Paper DOI: 10.1016/j.apenergy.2022.118830
Paper-provided assets: the WFASP definition; NPV equation; macro rectangular
screening; the new meso-scale constructive shape heuristic; the cited discrete
VNS micro-siting phase; Jensen wakes; 15 MW turbines; four UK Round-4 regions;
eight reported seed coordinates; 0.0625-degree macro cells; 100 turbines;
PtA<=5, density>=3 MW/km2, area<=500 km2, capacity<=1.5 GW and 7 km
restriction clearance; 25 years, six-percent discounting and 39.59 EUR/MWh;
two cable types and export-cable cost; 30-minute micro time limit; Table 1 and
Appendix A numerical anchors.
Public source: the target article is open access under CC BY. Exact-title, DOI,
author and GitHub searches on 2026-07-31 found no target implementation or
machine-readable UK instance. The paper-cited micro optimizer has DOI
10.1016/j.cor.2021.105588 and an official public ten-site dataset with DOI
10.11583/DTU.13134731; both are consumed by the already verified T31 unit.
Missing information: UK Round-4 polygons and restriction masks; extracted
ERA5/GEBCO/existing-farm arrays; exact macro orientations and aspect ratios;
fine-grid resolution and selected meso radius; shape-penalty coefficients;
seed layouts and final layouts; complete CAPEX/OPEX and shore/substation data;
VNS source, random stream and the mapping from the 30-minute limit to work.
Reconstruction: map target roles A--G to the seven official T31 sites D--J;
role H uses an independently offset seed/cell role on site I because only seven
public fields can legally hold 100 turbines at the inherited 1.2 km spacing.
These are declared same-lineage offshore proxy fields, retaining
their public NREL-15MW, RVO wind, fixed-wake, bathymetry/foundation and legal
candidate data. Coarsen the source grid deterministically to 800 m cells; use
the paper's 1.2 km spacing inherited from the cited optimizer, a 15-kilometre
meso radius from its published 2.5--15 km sensitivity range (needed to bridge
the declared proxy legal components), 3D/5D/8D VNS radii and counter events.
Use six rectangle orientations and five side ratios over a local 3x3
0.0625-degree-equivalent screen. Calibrate only one layout-independent missing
cost offset per seed so the selected best rectangular anchor equals Table 1;
all layout-dependent energy, foundation, cable and shape changes remain live.
Target-data substitutions and their hashes are emitted in every result.
Method semantic ID: t83_macro_meso_random_conic_vns_declared_v1
Problem semantic ID: t83_round4_eightseed_same_lineage_proxy_v1
Protocol semantic ID: t83_native_8seed_shape_rectangle_30min_v1
Production backend: pure C++20 CPU-HPC. One persistent all-core team performs
T31 fixed-wake preprocessing, packed candidate-pair construction, independent
macro rectangles, every meso candidate update and every micro neighborhood.
Pairwise AEP deltas, immutable geometry, packed float storage, fixed-index
writes and ordered commits eliminate repeated full-layout wake evaluations and
preserve fixed-work one/all-worker scientific identity. A formal process owns
all Waffle cores; seed roles and the two shape roles execute sequentially.
Controlling contract: shared/contracts/core99_t83_cazzaro_multiscale_2022.json
Claim boundary: academic flexible reconstruction of the paper's complete
three-scale method and eight paper problem roles; not author code, exact UK
Round-4 arrays, target layouts, random stream or numerical optimum replay.
Last evidence-audit date: 2026-07-31
END WFLOP IMPLEMENTATION FACT DECLARATION
*/
#pragma once

#include <cstdint>
#include <filesystem>
#include <memory>
#include <string>
#include <vector>

namespace core99::t83 {

struct Point {
    double x_m = 0.0;
    double y_m = 0.0;
};

struct CaseDefinition {
    char seed_role = 'A';
    double latitude_deg = 0.0;
    double longitude_deg = 0.0;
    std::string region;
    char proxy_site = 'I';
    double paper_shape_npv_meur = 0.0;
    double paper_pta = 0.0;
    double paper_density_mw_km2 = 0.0;
    double paper_rectangle_npv_meur = 0.0;
};

struct Evaluation {
    double aep_mwh = 0.0;
    double foundation_cost_meur = 0.0;
    double interarray_cable_cost_meur = 0.0;
    double export_cable_cost_meur = 0.0;
    double fixed_cost_offset_meur = 0.0;
    double area_km2 = 0.0;
    double perimeter_km = 0.0;
    double perimeter_to_sqrt_area = 0.0;
    double density_mw_km2 = 0.0;
    double minimum_spacing_m = 0.0;
    double spacing_violation_m = 0.0;
    double area_violation_km2 = 0.0;
    double pta_violation = 0.0;
    double density_violation_mw_km2 = 0.0;
    double npv_meur = 0.0;
    bool feasible = false;
};

struct RunConfig {
    int workers = 20;
    std::uint64_t seed = 2022083118830ULL;
    double micro_time_seconds = 1800.0;
    std::uint64_t fixed_micro_cycles = 0;
    int macro_cell_axis_override = 0;
};

struct RunResult {
    std::string case_id;
    std::string method_semantic_id =
        "t83_macro_meso_random_conic_vns_declared_v1";
    std::string problem_semantic_id =
        "t83_round4_eightseed_same_lineage_proxy_v1";
    std::string protocol_semantic_id =
        "t83_native_8seed_shape_rectangle_30min_v1";
    CaseDefinition paper_case;
    std::uint64_t seed = 0;
    int requested_workers = 0;
    int observed_workers = 0;
    int source_candidate_positions = 0;
    int hpc_candidate_positions = 0;
    int turbines = 100;
    int macro_rectangles_evaluated = 0;
    std::uint64_t pair_matrix_evaluations = 0;
    std::uint64_t meso_candidate_evaluations = 0;
    std::uint64_t shape_micro_candidate_evaluations = 0;
    std::uint64_t rectangle_micro_candidate_evaluations = 0;
    std::uint64_t shape_micro_cycles = 0;
    std::uint64_t rectangle_micro_cycles = 0;
    Evaluation macro_rectangle;
    Evaluation meso_shape;
    Evaluation optimized_shape;
    Evaluation optimized_rectangle;
    std::vector<int> meso_positions;
    std::vector<int> optimized_shape_positions;
    std::vector<int> optimized_rectangle_positions;
    double source_preprocessing_seconds = 0.0;
    double candidate_preprocessing_seconds = 0.0;
    double pair_matrix_seconds = 0.0;
    double macro_seconds = 0.0;
    double meso_seconds = 0.0;
    double shape_micro_seconds = 0.0;
    double rectangle_micro_seconds = 0.0;
    double end_to_end_seconds = 0.0;
    std::uint64_t source_matrix_fingerprint = 0;
    std::uint64_t scientific_hash = 0;
};

class Problem {
public:
    class Impl;

    Problem(
        std::filesystem::path t31_dataset_root,
        char seed_role,
        int workers = 20
    );
    ~Problem();
    Problem(Problem&&) noexcept;
    Problem& operator=(Problem&&) noexcept;
    Problem(const Problem&) = delete;
    Problem& operator=(const Problem&) = delete;

    [[nodiscard]] const CaseDefinition& paper_case() const noexcept;
    [[nodiscard]] int source_candidate_positions() const noexcept;
    [[nodiscard]] int candidate_positions() const noexcept;
    [[nodiscard]] double source_preprocessing_seconds() const noexcept;
    [[nodiscard]] double candidate_preprocessing_seconds() const noexcept;
    [[nodiscard]] double pair_matrix_seconds() const noexcept;
    [[nodiscard]] std::uint64_t source_matrix_fingerprint() const noexcept;
    [[nodiscard]] const std::vector<Point>& candidate_points() const noexcept;
    [[nodiscard]] Evaluation evaluate(const std::vector<int>& selected) const;

private:
    std::unique_ptr<Impl> impl_;

    friend RunResult run(const Problem&, const RunConfig&);
};

[[nodiscard]] std::vector<CaseDefinition> paper_cases();
[[nodiscard]] RunResult run(
    const Problem& problem,
    const RunConfig& config = {}
);

}  // namespace core99::t83
