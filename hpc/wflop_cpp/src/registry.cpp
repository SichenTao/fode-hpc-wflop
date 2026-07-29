/*
WFLOP IMPLEMENTATION FACT DECLARATION
Implementation unit: multipaper algorithm/problem/training/backend registries
Paper title and DOI: multipaper; one row per scoped DOI in
docs/paper_package_completion.tsv
Paper/source basis: paper protocol, source dossier, and semantic ledgers
Public asset: per-paper URL, revision, hash, and license records
Missing/conflicts: variants are registered independently and incompatibility
fails before physical FES
Reconstruction: machine-queryable descriptors and compatibility decisions
Method/problem semantic IDs: registry_defined; registry_defined
Controlling contract and claim boundary: docs/paper_package_completion.tsv;
registered, executable, and formally complete remain distinct states
Last evidence-audit date: 2026-07-30
END WFLOP IMPLEMENTATION FACT DECLARATION
*/
#include "wflop/algorithms.hpp"

#include <algorithm>
#include <stdexcept>

namespace wflop {

const std::vector<ProblemDescriptor>& problem_descriptors() {
    static const std::vector<ProblemDescriptor> descriptors{
        {
            "fode_e0_common",
            "FODE-E0-L common 50-case benchmark",
            "fode_wflop_e0_legacy_v1",
            "maximize expected complete-farm power under the frozen "
            "Jensen/Park discrete-grid model"
        },
        {
            "agpso_aiga_hgpso_landuse_156",
            "AGPSO/AIGA/HGPSO paper-native 156-case benchmark",
            "agpso_aiga_hgpso_landuse_156_v1",
            "maximize expected farm power over four wind scenarios, three "
            "turbine counts, and thirteen land-use patterns"
        },
        {
            "clshade_landuse_117",
            "CLSHADE paper-native 117-case benchmark",
            "clshade_landuse_117_v1",
            "maximize expected farm power over D1-D3, three turbine counts, "
            "and thirteen land-use patterns at the paper model constants"
        },
        {
            "ise_landuse_117",
            "ISE paper-native 117-case benchmark",
            "ise_landuse_117_v1",
            "maximize expected farm power over P1-P3, three turbine counts, "
            "and thirteen land-use patterns with 154 m cells"
        },
        {
            "alshade_complex_wake_117",
            "A-LSHADE paper-native 117-case benchmark",
            "alshade_complex_wake_117_v1",
            "maximize expected farm power over WC1-WC3, three turbine "
            "counts, and thirteen land-use patterns"
        },
        {
            "cgpso_complex_large_16",
            "CGPSO paper-native large-farm 16-case benchmark",
            "cgpso_complex_large_16_v1",
            "maximize expected farm power on a 21 by 21 grid for four "
            "complex wind arrays and 40 to 100 turbines"
        },
        {
            "ciga_native_declared_4",
            "CIGA four-condition declared reconstruction",
            "ciga_native_declared_4_p3_v1",
            "maximize expected farm power for the paper-declared 12 by 12, "
            "15-turbine setting with unavailable masks explicitly omitted"
        },
        {
            "lsde_large_declared_12",
            "LSDE large-farm declared reconstruction",
            "lsde_large_declared_12_p3_v1",
            "maximize expected farm power on the paper 15 by 15 scale using "
            "same-lineage 4-to-7-direction wind arrays"
        },
        {
            "wfadde_native_declared_24",
            "WFADDE eight-condition declared reconstruction",
            "wfadde_native_declared_24_p3_v1",
            "maximize expected farm power over eight same-lineage wind "
            "arrays and the paper turbine counts"
        },
        {
            "msshade_native_declared_16",
            "MS-SHADE Weibull-condition declared reconstruction",
            "msshade_native_declared_16_p3_v1",
            "maximize expected farm power over reconstructed 2-to-5-"
            "direction Weibull scenarios and four turbine counts"
        },
        {
            "bde2025_source_replay_ws1_ws4",
            "BDE official-source WS1-WS4 replay problem",
            "bde2025_source_replay_ws1_ws4_v1",
            "maximize expected complete-farm power on the official-source "
            "standard and Daegwallyeong discrete grids"
        },
        {
            "rpso2024_source_problem_ws1_ws4",
            "RPSO official-source WS1-WS4 problem",
            "rpso2024_source_problem_ws1_ws4_v1",
            "maximize expected complete-farm power on the official-source "
            "21 by 21 standard grid"
        },
        {
            "alga_guishan_3d_declared_proxy_v1",
            "ALGA Guishan-family declared 3D reconstruction",
            "alga_guishan_3d_declared_proxy_v1",
            "maximize terrain-aware expected farm power on the paper-visible "
            "12 by 12 grid using a declared analytic Guishan-family terrain, "
            "four ideal winds, four seasonal wind completions, and the "
            "paper's 3D Gaussian wake structure"
        },
        {
            "alga_guishan_planar_transfer",
            "ALGA declared planar Guishan-wind transfer",
            "alga_guishan_planar_wind_fode_evaluator_transfer_v1",
            "maximize expected complete-farm power on the paper-visible "
            "12 by 12 ideal grid using public WFLO-GGA Guishan annual "
            "wind probabilities and the audited FODE-E0 evaluator"
        },
        {
            "taae_zhangbei_structured_declared_proxy_v1",
            "TAAE structured 3D energy-noise declared proxy",
            "taae_zhangbei_structured_declared_proxy_v1",
            "minimize reciprocal expected power and A-weighted noise "
            "under a declared land-and-turbine cost constraint"
        }
    };
    return descriptors;
}

const ProblemDescriptor& problem_descriptor(const std::string& id) {
    const auto& descriptors = problem_descriptors();
    const auto found = std::find_if(
        descriptors.begin(),
        descriptors.end(),
        [&](const ProblemDescriptor& descriptor) {
            return descriptor.id == id;
        }
    );
    if (found == descriptors.end()) {
        throw std::invalid_argument("unknown problem: " + id);
    }
    return *found;
}

const std::vector<AlgorithmDescriptor>& algorithm_descriptors() {
    static const std::vector<AlgorithmDescriptor> descriptors = [] {
        std::vector<AlgorithmDescriptor> values{
        {
            "fode",
            "FODE",
            "10.3390/math13020282",
            "archived_matlab_source",
            "fode_e0_physical_fes",
            {"fode_e0_common"}
        },
        {
            "aga",
            "AGA",
            "10.1016/j.apenergy.2019.04.084",
            "paper_first_archived_matlab_completed",
            "aga_paper_first_e0_physical_fes_v1",
            {"fode_e0_common"}
        },
        {
            "sugga",
            "SUGGA",
            "10.1016/j.enconman.2019.06.082",
            "archived_matlab_source_and_frozen_surrogate",
            "sugga_frozen_surrogate_e0_physical_fes_v1",
            {"fode_e0_common"}
        },
        {
            "ise",
            "ISE",
            "10.1016/j.engappai.2023.106198",
            "paper_derived",
            "ise_paper_derived_e0_physical_fes_v1",
            {"fode_e0_common", "ise_landuse_117"}
        },
        {
            "agpso",
            "AGPSO",
            "10.1016/j.enconman.2022.116174",
            "paper_first_source_completed",
            "agpso_paper_staged_parallel_e0_physical_fes_v1",
            {"fode_e0_common", "agpso_aiga_hgpso_landuse_156"}
        },
        {
            "cgpso",
            "CGPSO",
            "10.1109/jas.2023.123387",
            "paper_first_source_completed",
            "cgpso_paper_staged_parallel_e0_physical_fes_v1",
            {
                "fode_e0_common",
                "rpso2024_source_problem_ws1_ws4",
                "cgpso_complex_large_16"
            }
        },
        {
            "lshade",
            "LSHADE",
            "10.1109/CEC.2014.6900380",
            "paper_first_archived_matlab_completed",
            "lshade_paper_first_e0_physical_fes_v1",
            {"fode_e0_common"}
        },
        {
            "clshade",
            "CLSHADE",
            "10.1016/j.asoc.2023.110306",
            "paper_derived",
            "clshade_paper_derived_e0_physical_fes_v1",
            {"fode_e0_common", "clshade_landuse_117"}
        },
        {
            "cede",
            "CEDE",
            "10.3390/math12233762",
            "paper_first_local_source_discrepancy_registered",
            "cede_paper_equations_physical_fes_v1",
            {"fode_e0_common"}
        },
        {
            "msshade",
            "MS-SHADE",
            "10.3390/electronics13163196",
            "paper_first_local_source_discrepancy_registered",
            "msshade_paper_equations_physical_fes_v1",
            {"fode_e0_common", "msshade_native_declared_16"}
        },
        {
            "bde",
            "BDE",
            "10.1016/j.energy.2025.137885",
            "paper_first_official_source_discrepancy_registered",
            "bde_paper_equations_physical_fes_v1",
            {"fode_e0_common", "bde2025_source_replay_ws1_ws4"}
        },
        {
            "hgpso",
            "HGPSO",
            "10.26599/tst.2026.9010059",
            "paper_first_local_source_discrepancy_registered",
            "hgpso_paper_equations_physical_fes_v1",
            {"fode_e0_common", "agpso_aiga_hgpso_landuse_156"}
        },
        {
            "aiga",
            "AIGA",
            "10.1007/s42235-024-00498-3",
            "paper_derived_explicit_reconstruction",
            "aiga_paper_derived_e0_physical_fes_v1",
            {"fode_e0_common", "agpso_aiga_hgpso_landuse_156"}
        },
        {
            "ciga",
            "CIGA",
            "10.1145/3766671.3766786",
            "paper_derived_explicit_reconstruction",
            "ciga_paper_derived_e0_physical_fes_v1",
            {"fode_e0_common", "ciga_native_declared_4"}
        },
        {
            "lsde",
            "LSDE",
            "10.1049/cit2.70150",
            "paper_derived_explicit_reconstruction",
            "lsde_paper_derived_e0_physical_fes_v1",
            {"fode_e0_common", "lsde_large_declared_12"}
        },
        {
            "wfadde",
            "WFADDE",
            "10.2139/ssrn.6135326",
            "preprint_derived_explicit_reconstruction",
            "wfadde_preprint_derived_e0_physical_fes_v1",
            {"fode_e0_common", "wfadde_native_declared_24"}
        },
        {
            "alshade",
            "A-LSHADE",
            "10.1109/pic62406.2024.10892732",
            "paper_derived_explicit_reconstruction",
            "alshade_paper_derived_e0_physical_fes_v1",
            {"fode_e0_common", "alshade_complex_wake_117"}
        },
        {
            "ppga",
            "PPGA",
            "10.1109/jas.2025.125351",
            "paper_derived_declared_problem_transfer",
            "ppga_declared_reconstruction_fode_e0_v1",
            {"fode_e0_common"}
        },
        {
            "pso",
            "PSO",
            "10.1109/ICNN.1995.488968",
            "canonical_paper_reconstruction_for_wflop_integer_encoding",
            "pso_canonical_integer_wflop_v1",
            {}
        },
        {
            "de",
            "DE/rand/1/bin",
            "10.1023/A:1008202821328",
            "canonical_paper_reconstruction_for_wflop_integer_encoding",
            "de_rand_1_bin_integer_wflop_v1",
            {}
        },
        {
            "shade",
            "SHADE",
            "10.1109/CEC.2013.6557555",
            "canonical_paper_reconstruction_for_wflop_integer_encoding",
            "shade_success_history_integer_wflop_v1",
            {}
        },
        {
            "cjade",
            "CJADE",
            "10.1016/j.asoc.2023.110306",
            "archived_source_behavior_reconstructed_under_target_paper",
            "cjade_archived_source_reconstruction_v1",
            {}
        },
        {
            "scjade",
            "SCJADE",
            "10.1016/j.engappai.2023.106198",
            "target_paper_table_and_cjade_predecessor_reconstruction",
            "scjade_paper_guided_reconstruction_v1",
            {}
        },
        {
            "lshadecnepsin",
            "LSHADE-cnEpSin",
            "10.1016/j.asoc.2023.110306",
            "target_paper_parameter_table_and_lshade_reconstruction",
            "lshadecnepsin_paper_guided_reconstruction_v1",
            {}
        },
        {
            "se",
            "Spherical Evolution",
            "10.1016/j.engappai.2023.106198",
            "ise_paper_predecessor_equations_reconstruction",
            "se_spherical_paper_guided_reconstruction_v1",
            {}
        },
        {
            "algsa",
            "ALGSA",
            "10.1016/j.enconman.2022.116174",
            "archived_source_behavior_reconstruction",
            "algsa_archived_source_reconstruction_v1",
            {}
        },
        {
            "hgsa",
            "HGSA",
            "10.1109/jas.2023.123387",
            "archived_source_behavior_reconstruction",
            "hgsa_archived_source_reconstruction_v1",
            {}
        },
        {
            "glpso",
            "GLPSO",
            "10.1109/jas.2023.123387",
            "archived_source_behavior_reconstruction",
            "glpso_archived_source_reconstruction_v1",
            {}
        },
        {
            "clpso",
            "CLPSO",
            "10.1109/jas.2023.123387",
            "archived_source_behavior_reconstruction",
            "clpso_archived_source_reconstruction_v1",
            {}
        },
        {
            "siga",
            "SIGA",
            "10.1016/j.apenergy.2019.04.084",
            "paper_guided_information_guidance_reconstruction",
            "siga_information_guided_integer_wflop_v1",
            {}
        },
        {
            "alga_attention_declared_reconstruction_v1",
            "ALGA attention engineering reconstruction",
            "10.1016/j.swevo.2025.102018",
            "paper_guided_citation_predecessor_derived_declared_completion",
            "alga_attention_declared_reconstruction_v1",
            {
                "alga_guishan_3d_declared_proxy_v1",
                "alga_guishan_planar_transfer",
                "fode_e0_common"
            }
        },
        {
            "rlpso_compact_policy_declared_reconstruction_v1",
            "RPSO-derived compact policy engineering proxy",
            "10.1016/j.energy.2024.134050",
            "paper_guided_declared_compact_policy_engineering_proxy",
            "rlpso_compact_policy_declared_reconstruction_v1",
            {"rpso2024_source_problem_ws1_ws4"}
        },
        {
            "rlpso_paper_corrected_training_reconstruction_v1",
            "RPSO paper-corrected seeded PPO reconstruction",
            "10.1016/j.energy.2024.134050",
            "paper_corrected_seeded_full_ppo_declared_reconstruction",
            "rlpso_paper_corrected_training_reconstruction_v1",
            {"rpso2024_source_problem_ws1_ws4"}
        },
        {
            "rlpso_literal_official_source_replay_v1",
            "RPSO literal official-source replay",
            "10.1016/j.energy.2024.134050",
            "literal_official_matlab_python_source_replay_with_seeded_rng",
            "rlpso_literal_official_source_replay_v1",
            {"rpso2024_source_problem_ws1_ws4"}
        },
        {
            "fqfode_seeded_training_declared_reconstruction_v1",
            "FQFODE seeded offline-training reconstruction",
            "10.3390/math13182935",
            "paper_guided_seeded_offline_qtable_declared_reconstruction",
            "fqfode_seeded_training_declared_reconstruction_v1",
            {"fode_e0_common"}
        },
        {
            "taae_transformer_evolution_declared_reconstruction_v1",
            "TAAE Transformer-evolution reconstruction",
            "10.1109/jas.2026.126233",
            "paper_guided_training_from_scratch_declared_reconstruction",
            "taae_transformer_evolution_declared_reconstruction_v1",
            {}
        },
        {
            "agpso_source_replay_v1",
            "AGPSO archived-source replay",
            "10.1016/j.enconman.2022.116174",
            "archived_matlab_source_no_license_behavior_oracle",
            "agpso_source_replay_v1",
            {}
        },
        {
            "cgpso_source_replay_v1",
            "CGPSO archived-source replay",
            "10.1109/jas.2023.123387",
            "archived_matlab_source_no_license_behavior_oracle",
            "cgpso_source_replay_v1",
            {}
        },
        {
            "cede_source_replay_v1",
            "CEDE local-source replay",
            "10.3390/math12233762",
            "local_source_no_license_behavior_oracle",
            "cede_source_replay_v1",
            {}
        },
        {
            "msshade_source_replay_v1",
            "MS-SHADE local-source replay",
            "10.3390/electronics13163196",
            "local_source_no_license_behavior_oracle",
            "msshade_source_replay_v1",
            {}
        },
        {
            "hgpso_source_replay_v1",
            "HGPSO local-source replay",
            "10.26599/tst.2026.9010059",
            "local_source_no_license_behavior_oracle",
            "hgpso_source_replay_v1",
            {}
        }
        };
        const std::vector<std::string> scalar_algorithms{
            "fode", "aga", "sugga", "ise", "agpso", "cgpso", "lshade",
            "clshade", "cede", "msshade", "bde", "hgpso", "aiga",
            "ciga", "lsde", "wfadde", "alshade", "ppga"
            , "pso", "de", "shade", "cjade", "scjade",
            "lshadecnepsin", "se", "algsa", "hgsa", "glpso", "clpso",
            "siga"
        };
        const std::vector<std::string> scalar_problems{
            "fode_e0_common",
            "agpso_aiga_hgpso_landuse_156",
            "clshade_landuse_117",
            "ise_landuse_117",
            "alshade_complex_wake_117",
            "cgpso_complex_large_16",
            "ciga_native_declared_4",
            "lsde_large_declared_12",
            "wfadde_native_declared_24",
            "msshade_native_declared_16",
        };
        for (auto& descriptor : values) {
            if (std::find(
                    scalar_algorithms.begin(),
                    scalar_algorithms.end(),
                    descriptor.id
                ) == scalar_algorithms.end()) {
                continue;
            }
            for (const auto& problem : scalar_problems) {
                if (std::find(
                        descriptor.compatible_problem_ids.begin(),
                        descriptor.compatible_problem_ids.end(),
                        problem
                    ) == descriptor.compatible_problem_ids.end()) {
                    descriptor.compatible_problem_ids.push_back(problem);
                }
            }
        }
        return values;
    }();
    return descriptors;
}

const AlgorithmDescriptor& algorithm_descriptor(const std::string& id) {
    const auto& descriptors = algorithm_descriptors();
    const auto found = std::find_if(
        descriptors.begin(),
        descriptors.end(),
        [&](const AlgorithmDescriptor& descriptor) {
            return descriptor.id == id;
        }
    );
    if (found == descriptors.end()) {
        throw std::invalid_argument("unknown algorithm: " + id);
    }
    return *found;
}

const std::vector<std::string>& algorithm_ids() {
    static const std::vector<std::string> ids = [] {
        std::vector<std::string> result;
        result.reserve(algorithm_descriptors().size());
        for (const auto& descriptor : algorithm_descriptors()) {
            result.push_back(descriptor.id);
        }
        return result;
    }();
    return ids;
}

const std::vector<TrainingDescriptor>& training_descriptors() {
    static const std::vector<TrainingDescriptor> descriptors{
        {
            "sugga_native_train_from_scratch_v1",
            "sugga",
            "generate paper-native layout/cell training corpus; fit and "
            "freeze one case-semantic SVR artifact; optimize",
            true,
        },
        {
            "alga_attention_train_from_scratch_v1",
            "alga_attention_declared_reconstruction_v1",
            "generate deterministic generation corpus; train attention; "
            "freeze artifact; optimize",
            false,
        },
        {
            "rlpso_compact_policy_train_from_scratch_v1",
            "rlpso_compact_policy_declared_reconstruction_v1",
            "train online policy; validate; freeze run state; optimize",
            true,
        },
        {
            "rlpso_paper_corrected_train_from_scratch_v1",
            "rlpso_paper_corrected_training_reconstruction_v1",
            "train seeded PPO; validate; freeze run state; optimize",
            true,
        },
        {
            "rlpso_literal_source_train_from_scratch_v1",
            "rlpso_literal_official_source_replay_v1",
            "reinitialize the released PPO topology on every MATLAB outer "
            "iteration; execute 100x100 source interactions with the 0.01 "
            "action step, argmax-as-logprob defect, and uncleared memory",
            true,
        },
        {
            "fqfode_train_from_scratch_v1",
            "fqfode_seeded_training_declared_reconstruction_v1",
            "train four Q tables; validate; freeze artifact; optimize",
            true,
        },
        {
            "taae_train_from_scratch_v1",
            "taae_transformer_evolution_declared_reconstruction_v1",
            "generate corpus; pretrain Transformer; fine tune; freeze "
            "artifact; optimize latent representation",
            false,
        },
    };
    return descriptors;
}

const std::vector<BackendDescriptor>& backend_descriptors() {
    static const std::vector<BackendDescriptor> descriptors{
        {"cpu", "optimized pure C++ persistent-worker CPU path", true},
        {"auto", "resolves only to a measured executable backend", false},
        {"hybrid", "registered CPU+GPU capability; no admitted kernels", false},
        {"gpu", "registered GPU capability; no admitted kernels", false},
    };
    return descriptors;
}

const BackendDescriptor& backend_descriptor(const std::string& id) {
    const std::string normalized = id == "cpu+gpu" ? "hybrid" : id;
    const auto& descriptors = backend_descriptors();
    const auto found = std::find_if(
        descriptors.begin(),
        descriptors.end(),
        [&](const BackendDescriptor& descriptor) {
            return descriptor.id == normalized;
        }
    );
    if (found == descriptors.end()) {
        throw std::invalid_argument("unknown backend: " + id);
    }
    return *found;
}

CompatibilityDescriptor explain_compatibility(
    const std::string& algorithm_id,
    const std::string& problem_id
) {
    const auto& algorithm = algorithm_descriptor(algorithm_id);
    const auto& problem = problem_descriptor(problem_id);
    const bool admitted = std::find(
        algorithm.compatible_problem_ids.begin(),
        algorithm.compatible_problem_ids.end(),
        problem_id
    ) != algorithm.compatible_problem_ids.end();
    CompatibilityDescriptor result;
    result.algorithm_id = algorithm.id;
    result.problem_id = problem.id;
    result.compatible = admitted;
    if (admitted) {
        result.reason =
            "registered method and problem semantic identities preserve "
            "decision encoding, objective, constraints, evaluator detail, "
            "and physical-FES meaning";
    } else {
        result.reason =
            "method semantic identity does not admit this problem; no "
            "objective, decision, constraint, or physics coercion is allowed";
    }
    return result;
}

bool algorithm_supports_problem(
    const std::string& algorithm_id,
    const std::string& problem_id
) {
    return explain_compatibility(algorithm_id, problem_id).compatible;
}

}  // namespace wflop
